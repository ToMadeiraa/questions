# 36. Как написать собственный std::shared_ptr?

## Что вообще требуется от `shared_ptr`

`std::shared_ptr<T>` — это **не владелец указателя**, а владелец *разделяемого ресурса*. Его ключевые свойства:

- несколько `shared_ptr` могут указывать на один объект и совместно им владеть;
- объект уничтожается ровно один раз — когда исчезает последний сильный владелец;
- `weak_ptr` позволяет наблюдать за объектом, не продлевая его жизнь;
- можно задать свой deleter, не меняя тип `T`;
- копирование/уничтожение разных экземпляров `shared_ptr` можно делать из разных потоков без гонок;
- есть «алиасящий» конструктор, когда хранимый указатель и владеемый объект — разные вещи.

Всё это удобно реализуется через **контрольный блок** (control block), который живёт отдельно от объекта.

---

## 1. Ключевая идея: объект + контрольный блок

```
        shared_ptr<Base>                              control_block
   ┌───────────────────────┐                 ┌──────────────────────────────┐
   │ element_type* ptr ────┼──► Derived      │ atomic<long> shared_ = 2     │
   │ control_block* cb ────┼────────────────►│ atomic<long> weak_   = 3     │
   └───────────────────────┘                 │ virtual void dispose()       │
                                             │ virtual void destroy()       │
        weak_ptr<Base>                       │ Deleter (стёрт по типу)      │
   ┌───────────────────────┐                 └──────────────────────────────┘
   │ element_type* ptr ────┼──► Derived
   │ control_block* cb ────┼────────────────►
   └───────────────────────┘
```

Почему именно блок, а не счётчик внутри объекта?

- **Неинтрузивность.** Мы можем владеть `int`, `FILE*`, чужим классом, массивом — ничего в них не добавляя. (Счётчик внутри объекта — это `boost::intrusive_ptr`, он требует менять класс.)
- **Один объект — одна «группа владения».** Два независимых контрольных блока на один `new T` — это классический double delete; блок гарантирует, что владение создаётся один раз.
- **Type erasure.** Deleter и аллокатор «стираются» в блоке, поэтому `shared_ptr<T>` остаётся размером в два указателя.
- **Слабые ссылки.** `weak_ptr` должен уметь проверить «жив ли объект» уже после его destruction — значит, нужен объект, переживающий `T`.

### Два счётчика и «неявная слабая ссылка»

- `shared_` — число `shared_ptr`;
- `weak_` — число `weak_ptr` **плюс один**.

Этот «плюс один» — неявная слабая ссылка, которую коллективно держат все сильные владельцы. Она нужна, чтобы блок гарантированно дожил до момента, когда последний `shared_ptr` уничтожит объект и сам решит, не пора ли освободить блок.

Жизненный цикл:

| Событие | `shared_` | `weak_` |
|---|---|---|
| создан блок (`make_shared`/`new`) | 1 | 1 |
| скопировали `shared_ptr` | 2 | 1 |
| создали `weak_ptr` | 2 | 2 |
| умер один `shared_ptr` | 1 | 2 |
| умер последний `shared_ptr` | 0 → `dispose()` | 1 |
| умер последний `weak_ptr` | 0 | 0 → `destroy()` |

Отсюда два разных действия:

- `dispose()` — **разрушить объект** `T` (вызвать deleter или `~T`);
- `destroy()` — **освободить память самого блока**.

Для `make_shared` объект лежит внутри блока, поэтому `dispose()` разрушает `T`, а `destroy()` освобождает общую память — до этого момента `weak_ptr` ещё может смотреть на счётчики.

---

## 2. Реализация

Ниже — полный учебный вариант на C++17 (порядок объявлений важен: `weak_ptr` определяется до `shared_ptr`, а часть функций `weak_ptr` — уже после него, потому что они используют внутренности `shared_ptr`).

### 2.1. Контрольный блок

```cpp
#include <atomic>
#include <cstddef>
#include <functional>   // std::less
#include <memory>       // std::default_delete, std::bad_weak_ptr
#include <type_traits>
#include <utility>

namespace my {

template<class T> class shared_ptr;
template<class T> class weak_ptr;
template<class T> class enable_shared_from_this;
template<class T, class... Args> shared_ptr<T> make_shared(Args&&...);

class control_block {
public:
    control_block() noexcept = default;
    control_block(const control_block&) = delete;
    control_block& operator=(const control_block&) = delete;

    // Разрушить управляемый объект (сильных ссылок больше нет).
    virtual void dispose() noexcept = 0;
    // Освободить память самого блока (слабых ссылок тоже нет).
    virtual void destroy() noexcept = 0;

    void add_shared() noexcept { shared_.fetch_add(1, std::memory_order_relaxed); }
    void add_weak()   noexcept { weak_.fetch_add(1, std::memory_order_relaxed); }

    void release_shared() noexcept {
        // acq_rel: release — чтобы записи в объект были видны разрушителю,
        //          acquire — чтобы разрушитель увидел все записи владельцев.
        if (shared_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            dispose();
            release_weak();          // снимаем неявную слабую ссылку
        }
    }

    void release_weak() noexcept {
        if (weak_.fetch_sub(1, std::memory_order_acq_rel) == 1)
            destroy();
    }

    // Атомарно: "если shared_ != 0, то ++shared_". Нужно для weak_ptr::lock().
    bool try_add_shared() noexcept {
        long n = shared_.load(std::memory_order_relaxed);
        do {
            if (n == 0) return false;
        } while (!shared_.compare_exchange_weak(
                     n, n + 1,
                     std::memory_order_acq_rel,     // успех
                     std::memory_order_relaxed));   // провал
        return true;
    }

    long use_count() const noexcept {
        return shared_.load(std::memory_order_relaxed);
    }

protected:
    ~control_block() = default;   // через базовый указатель не удаляем

private:
    std::atomic<long> shared_{1};  // сильные ссылки
    std::atomic<long> weak_{1};    // слабые ссылки + 1 неявная
};
```

### 2.2. Два вида блоков

```cpp
// Владеет сырым указателем и произвольным deleter'ом.
template<class T, class Deleter>
class ptr_control_block final : public control_block {
public:
    ptr_control_block(T* p, Deleter d) : ptr_(p), deleter_(std::move(d)) {}

    void dispose() noexcept override { deleter_(ptr_); }  // deleter не должен бросать
    void destroy() noexcept override { delete this; }

private:
    T*      ptr_;
    Deleter deleter_;
};

// Объект сконструирован прямо внутри блока: одна аллокация на всё (make_shared).
template<class T>
class inplace_control_block final : public control_block {
public:
    template<class... Args>
    explicit inplace_control_block(Args&&... args) {
        ::new (static_cast<void*>(storage_)) T(std::forward<Args>(args)...);
    }

    T* get() noexcept {
        return std::launder(reinterpret_cast<T*>(storage_));
    }

    void dispose() noexcept override { get()->~T(); }
    void destroy() noexcept override { delete this; }

private:
    alignas(T) std::byte storage_[sizeof(T)];
};
```

### 2.3. `weak_ptr`

```cpp
template<class T>
class weak_ptr {
public:
    using element_type = std::remove_extent_t<T>;

    constexpr weak_ptr() noexcept = default;

    weak_ptr(const weak_ptr& o) noexcept : ptr_(o.ptr_), cb_(o.cb_) {
        if (cb_) cb_->add_weak();
    }

    template<class Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    weak_ptr(const weak_ptr<Y>& o) noexcept : ptr_(o.ptr_), cb_(o.cb_) {
        if (cb_) cb_->add_weak();
    }

    weak_ptr(weak_ptr&& o) noexcept : ptr_(o.ptr_), cb_(o.cb_) {
        o.ptr_ = nullptr; o.cb_ = nullptr;
    }

    template<class Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    weak_ptr(weak_ptr<Y>&& o) noexcept : ptr_(o.ptr_), cb_(o.cb_) {
        o.ptr_ = nullptr; o.cb_ = nullptr;
    }

    // Определения — ниже, после shared_ptr:
    template<class Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    weak_ptr(const shared_ptr<Y>& o) noexcept;

    weak_ptr& operator=(const weak_ptr& o) noexcept {
        weak_ptr(o).swap(*this);
        return *this;
    }
    weak_ptr& operator=(weak_ptr&& o) noexcept {
        weak_ptr(std::move(o)).swap(*this);
        return *this;
    }
    template<class Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    weak_ptr& operator=(const shared_ptr<Y>& o) noexcept;   // ниже

    ~weak_ptr() { if (cb_) cb_->release_weak(); }

    long use_count() const noexcept { return cb_ ? cb_->use_count() : 0; }
    bool expired()   const noexcept { return use_count() == 0; }

    shared_ptr<T> lock() const noexcept;   // ниже

    void reset() noexcept { weak_ptr().swap(*this); }

    void swap(weak_ptr& o) noexcept {
        std::swap(ptr_, o.ptr_);
        std::swap(cb_,  o.cb_);
    }

    template<class U>
    bool owner_before(const weak_ptr<U>& o) const noexcept {
        return std::less<control_block*>{}(cb_, o.cb_);
    }
    template<class U>
    bool owner_before(const shared_ptr<U>& o) const noexcept;   // ниже

private:
    template<class> friend class shared_ptr;
    template<class> friend class weak_ptr;

    element_type*  ptr_ = nullptr;
    control_block* cb_  = nullptr;
};
```

Зачем `weak_ptr` хранит `ptr_`? Для `lock()` (нужно вернуть `shared_ptr` с тем же хранимым указателем, в том числе для алиасящих `shared_ptr`) и для сравнения владения. Так же поступает и стандартная библиотека.

### 2.4. `enable_shared_from_this`

```cpp
template<class T>
class enable_shared_from_this {
protected:
    constexpr enable_shared_from_this() noexcept = default;
    enable_shared_from_this(const enable_shared_from_this&) noexcept {}
    enable_shared_from_this& operator=(const enable_shared_from_this&) noexcept { return *this; }
    ~enable_shared_from_this() = default;

public:
    shared_ptr<T>       shared_from_this();          // определения — ниже
    shared_ptr<const T> shared_from_this() const;

    weak_ptr<T>       weak_from_this()       noexcept { return weak_this_; }
    weak_ptr<const T> weak_from_this() const noexcept { return weak_this_; }

private:
    template<class> friend class shared_ptr;
    mutable weak_ptr<T> weak_this_;
};
```

### 2.5. Сам `shared_ptr`

```cpp
template<class T>
class shared_ptr {
public:
    using element_type = std::remove_extent_t<T>;
    using weak_type    = weak_ptr<T>;

private:
    struct adopt_t {};   // тег "блок уже создан, ссылку не добавлять"

public:
    constexpr shared_ptr() noexcept = default;
    constexpr shared_ptr(std::nullptr_t) noexcept {}

    // ---------- владение сырым указателем ----------
    template<class Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    explicit shared_ptr(Y* p) : ptr_(p) {
        try {
            cb_ = new ptr_control_block<Y, std::default_delete<Y>>(
                      p, std::default_delete<Y>{});
        } catch (...) {
            delete p;          // блок не создан — указатель наш
            throw;
        }
        init_weak_this(p);
    }

    template<class Y, class D,
              std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    shared_ptr(Y* p, D d) : ptr_(p) {
        try {
            cb_ = new ptr_control_block<Y, D>(p, d);   // копируем deleter
        } catch (...) {
            d(p);              // по стандарту: при исключении deleter вызывается
            throw;
        }
        init_weak_this(p);
    }

    template<class Y, class D>
    shared_ptr(std::unique_ptr<Y, D>&& r)
        : shared_ptr(r.release(), std::move(r.get_deleter())) {}

    // ---------- копирование / перемещение ----------
    shared_ptr(const shared_ptr& o) noexcept : ptr_(o.ptr_), cb_(o.cb_) {
        if (cb_) cb_->add_shared();
    }

    template<class Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    shared_ptr(const shared_ptr<Y>& o) noexcept : ptr_(o.ptr_), cb_(o.cb_) {
        if (cb_) cb_->add_shared();
    }

    shared_ptr(shared_ptr&& o) noexcept : ptr_(o.ptr_), cb_(o.cb_) {
        o.ptr_ = nullptr;
        o.cb_  = nullptr;
    }

    template<class Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    shared_ptr(shared_ptr<Y>&& o) noexcept : ptr_(o.ptr_), cb_(o.cb_) {
        o.ptr_ = nullptr;
        o.cb_  = nullptr;
    }

    // ---------- алиасящий конструктор ----------
    template<class Y>
    shared_ptr(const shared_ptr<Y>& o, element_type* p) noexcept
        : ptr_(p), cb_(o.cb_) {
        if (cb_) cb_->add_shared();
    }

    template<class Y>
    shared_ptr(shared_ptr<Y>&& o, element_type* p) noexcept
        : ptr_(p), cb_(o.cb_) {
        o.ptr_ = nullptr;
        o.cb_  = nullptr;
    }

    // ---------- из weak_ptr ----------
    template<class Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    explicit shared_ptr(const weak_ptr<Y>& w) : ptr_(w.ptr_), cb_(w.cb_) {
        if (cb_ && cb_->try_add_shared()) {
            // владение получено, ptr_ уже нужный
        } else {
            ptr_ = nullptr;
            cb_  = nullptr;
            throw std::bad_weak_ptr();      // объект уже разрушен
        }
    }

    ~shared_ptr() { if (cb_) cb_->release_shared(); }

    // Copy-and-swap: один оператор покрывает копирование, перемещение
    // и преобразование shared_ptr<Y> -> shared_ptr<T>.
    shared_ptr& operator=(shared_ptr o) noexcept {
        swap(o);
        return *this;
    }

    template<class Y, class D>
    shared_ptr& operator=(std::unique_ptr<Y, D>&& r) {
        shared_ptr(std::move(r)).swap(*this);   // может бросить bad_alloc
        return *this;
    }

    void swap(shared_ptr& o) noexcept {
        std::swap(ptr_, o.ptr_);
        std::swap(cb_,  o.cb_);
    }

    void reset() noexcept { shared_ptr().swap(*this); }

    template<class Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    void reset(Y* p) { shared_ptr(p).swap(*this); }

    template<class Y, class D,
              std::enable_if_t<std::is_convertible_v<Y*, T*>, int> = 0>
    void reset(Y* p, D d) { shared_ptr(p, std::move(d)).swap(*this); }

    // ---------- наблюдатели ----------
    element_type* get() const noexcept { return ptr_; }
    element_type& operator*()  const noexcept { return *ptr_; }
    element_type* operator->() const noexcept { return ptr_; }

    long use_count() const noexcept { return cb_ ? cb_->use_count() : 0; }
    bool unique()    const noexcept { return use_count() == 1; }

    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    template<class U>
    bool owner_before(const shared_ptr<U>& o) const noexcept {
        return std::less<control_block*>{}(cb_, o.cb_);
    }
    template<class U>
    bool owner_before(const weak_ptr<U>& o) const noexcept {
        return std::less<control_block*>{}(cb_, o.cb_);
    }

private:
    template<class> friend class shared_ptr;
    template<class> friend class weak_ptr;
    template<class U, class... Args> friend shared_ptr<U> make_shared(Args&&...);

    // Вызывается только там, где блок создаётся впервые.
    template<class Y>
    void init_weak_this(Y* p) noexcept {
        using U = std::remove_cv_t<Y>;
        if constexpr (std::is_class_v<U>) {
            if constexpr (std::is_base_of_v<enable_shared_from_this<U>, U>) {
                if (p)
                    p->weak_this_ = shared_ptr<U>(*this, p);  // алиасящий ctor
            }
        }
    }

    shared_ptr(adopt_t, element_type* p, control_block* cb) noexcept
        : ptr_(p), cb_(cb) {}

    element_type*  ptr_ = nullptr;
    control_block* cb_  = nullptr;
};
```

Обратите внимание: `adopt_t`-конструктор **не** увеличивает счётчик — он нужен для `make_shared` и `weak_ptr::lock()`, которые уже «владеют» ссылкой.

### 2.6. Отложенные определения и `make_shared`

```cpp
template<class T>
template<class Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int>>
weak_ptr<T>::weak_ptr(const shared_ptr<Y>& o) noexcept
    : ptr_(o.ptr_), cb_(o.cb_) {
    if (cb_) cb_->add_weak();
}

template<class T>
template<class Y, std::enable_if_t<std::is_convertible_v<Y*, T*>, int>>
weak_ptr<T>& weak_ptr<T>::operator=(const shared_ptr<Y>& o) noexcept {
    weak_ptr(o).swap(*this);
    return *this;
}

template<class T>
shared_ptr<T> weak_ptr<T>::lock() const noexcept {
    if (!cb_ || !cb_->try_add_shared())
        return shared_ptr<T>();                 // объект уже уничтожен
    return shared_ptr<T>(typename shared_ptr<T>::adopt_t{}, ptr_, cb_);
}

template<class T>
template<class U>
bool weak_ptr<T>::owner_before(const shared_ptr<U>& o) const noexcept {
    return std::less<control_block*>{}(cb_, o.cb_);
}

template<class T>
shared_ptr<T> enable_shared_from_this<T>::shared_from_this() {
    return shared_ptr<T>(weak_this_);
}

template<class T>
shared_ptr<const T> enable_shared_from_this<T>::shared_from_this() const {
    return shared_ptr<const T>(weak_this_);
}

// ---------- make_shared: одна аллокация ----------
template<class T, class... Args>
shared_ptr<T> make_shared(Args&&... args) {
    auto* cb = new inplace_control_block<T>(std::forward<Args>(args)...);
    shared_ptr<T> sp(typename shared_ptr<T>::adopt_t{}, cb->get(), cb);
    sp.init_weak_this(cb->get());   // make_shared тоже должен уметь ESFT
    return sp;
}

} // namespace my
```

---

## 3. Проверка работоспособности

```cpp
#include <cassert>
#include <cstdio>
#include <utility>

struct Base {
    virtual ~Base() = default;
    virtual const char* name() const { return "Base"; }
};

struct Derived : Base {
    const char* name() const override { return "Derived"; }
};

struct Session : my::enable_shared_from_this<Session> {
    int id = 7;
    ~Session() { std::puts("~Session"); }
};

int main() {
    // 1. Совместное владение и подсчёт ссылок.
    {
        my::shared_ptr<Base> p = my::make_shared<Derived>();
        assert(p.use_count() == 1);
        {
            my::shared_ptr<Base> q = p;
            assert(p.use_count() == 2);
            assert(q->name()[0] == 'D');
        }
        assert(p.use_count() == 1);
    } // ~Derived

    // 2. weak_ptr не продлевает жизнь.
    {
        my::shared_ptr<Session> s = my::make_shared<Session>();
        my::weak_ptr<Session> w = s;
        assert(!w.expired());
        {
            my::shared_ptr<Session> locked = w.lock();
            assert(locked && locked.use_count() == 2 && locked->id == 7);
        }
        s.reset();
        assert(w.expired());
        assert(!w.lock());
    }

    // 3. enable_shared_from_this.
    {
        my::shared_ptr<Session> s = my::make_shared<Session>();
        my::shared_ptr<Session> self = s->shared_from_this();
        assert(self.get() == s.get());
        assert(s.use_count() == 2);
    }

    // 4. Алиасящий конструктор: владеем pair, храним указатель на элемент.
    {
        auto owner = my::make_shared<std::pair<int, int>>(1, 2);
        my::shared_ptr<int> second(owner, &owner->second);
        assert(second.use_count() == 2);
        assert(*second == 2);
        assert(owner.use_count() == 2);
    }

    // 5. Собственный deleter.
    {
        int calls = 0;
        {
            my::shared_ptr<int> p(new int(5),
                                  [&calls](int* q) { ++calls; delete q; });
            assert(*p == 5);
        }
        assert(calls == 1);
    }

    std::puts("ok");
}
```

---

## 4. Разбор тонких мест

### 4.1. Почему `fetch_sub` — это правильный decrement

Для сильного счётчика проверка «я последний?» **неотделима** от уменьшения, и `fetch_sub` возвращает старое значение — этого достаточно:

```cpp
if (shared_.fetch_sub(1, memory_order_acq_rel) == 1) dispose();
```

Наивный вариант

```cpp
if (--count == 0) ...   // с обычным int
```

потребовал бы внешней синхронизации: два потока могли бы одновременно увидеть 0 или один — увидеть 1, а другой — уже 0.

А вот для `weak_ptr::lock()` одной атомарной операции мало: нужно **условное** увеличение «если не ноль». Здесь и появляется CAS-цикл:

```cpp
long n = shared_.load(relaxed);
do {
    if (n == 0) return false;          // объект уже мёртв
} while (!shared_.compare_exchange_weak(n, n + 1, acq_rel, relaxed));
```

Если бы мы сделали «проверить `use_count() != 0`, потом `++`», между проверкой и инкрементом объект мог бы быть разрушен — это и есть гонка, из-за которой `expired()` нельзя использовать как защиту, а нужен `lock()`.

### 4.2. Memory order: что и зачем

- **Инкременты (`add_shared`, `add_weak`)** — `relaxed`. Нужна только атомарность самой операции; объект заведомо жив, никакого «упорядочивания» не требуется.
- **Декременты (`release_shared`, `release_weak`)** — `acq_rel`.
  - `release`-половина: все записи в управляемый объект, сделанные этим потоком, должны стать видимы тому, кто выполнит финальный декремент и вызовет `dispose()`. Иначе деструктор мог бы работать с устаревшим состоянием.
  - `acquire`-половина: поток, выполняющий финальный декремент, должен увидеть записи **всех** предыдущих владельцев. Цепочка RMW-операций (release sequence) как раз это и обеспечивает.
- **`try_add_shared`** — `acquire` при успехе (чтобы «увидеть» объект в опубликованном состоянии), `relaxed` при провале (нас интересует только факт «ноль/не ноль»).

Более экономный вариант, который используют некоторые реализации:

```cpp
if (shared_.fetch_sub(1, std::memory_order_release) == 1) {
    std::atomic_thread_fence(std::memory_order_acquire);
    dispose();
}
```

`acquire` нужен только тому потоку, который выполняет **последний** декремент, — поэтому его можно «вынести» в fence под ветку. Вариант с `acq_rel` на всех декрементах чуть дороже, но проще и безопаснее для учебного кода.

### 4.3. Почему `weak_` начинается с единицы

Если бы `weak_` считал только `weak_ptr`, то в момент `shared_ == 0` нужно было бы как-то решить: «а вдруг `weak_ptr` ещё есть?» — и для этого всё равно понадобилась бы дополнительная ссылка. Неявная слабая ссылка превращает этот вопрос в обычный `fetch_sub`: блок разрушает тот, кто первым опустил `weak_` до нуля — либо последний `shared_ptr` (когда слабых нет), либо последний `weak_ptr`.

### 4.4. `make_shared`: одна аллокация вместо двух

`shared_ptr<T>(new T)` делает две аллокации: под `T` и под контрольный блок. `make_shared` кладёт `T` прямо в блок:

```
[ control_block | storage для T ]
```

Плюсы: меньше аллокаций, лучше локальность, безопаснее по исключениям (нет «дыры» между `new T` и захватом во владение).

Минусы, о которых стоит помнить:

- память под `T` освобождается только когда исчезнут **все** `weak_ptr` (объект уже разрушен, но память блока ещё нужна);
- нельзя передать свой deleter;
- для очень больших объектов `weak_ptr` держит большой кусок памяти.

Именно поэтому `dispose()` и `destroy()` разделены.

### 4.5. Алиасящий конструктор

```cpp
template<class Y>
shared_ptr(const shared_ptr<Y>& o, element_type* p) noexcept;
```

Он берёт **контрольный блок** от `o`, но хранит указатель `p`. Это позволяет:

- владеть объектом, а «представлять» его подобъект (`&owner->second`);
- возвращать `shared_ptr<void>` или `shared_ptr<Handle>`, которые держат жизнь совсем другого объекта;
- делать `enable_shared_from_this` типобезопасно.

Важно: деструктор работает по блоку, а не по `ptr_`, поэтому `get()` и владение могут указывать на разные адреса. Побочный эффект: `shared_ptr<T>(shared_ptr<Y>(), p)` с непустым `p` даёт `operator bool == true` при `use_count() == 0` — это соответствует стандарту.

### 4.6. `enable_shared_from_this`

Идея: объект хранит `weak_ptr` на самого себя, а `shared_ptr` при **создании нового блока** «заглядывает» в объект и инициализирует этот `weak_ptr`. Поэтому:

- инициализация выполняется только в конструкторах, создающих блок (из `Y*`, из `unique_ptr`, в `make_shared`);
- **не** выполняется в копирующих, перемещающих и алиасящих конструкторах — иначе `weak_this_` перезаписывался бы и была бы гонка;
- если объект не принадлежит `shared_ptr`, `shared_from_this()` бросит `std::bad_weak_ptr`.

В нашем упрощённом варианте поддержан только точный тип `enable_shared_from_this<U>`; стандарт умеет и случай, когда базой является `enable_shared_from_this<Base>` для производного `U`.

### 4.7. Исключения и deleter

- Если не удалось выделить контрольный блок, `shared_ptr(Y*)` обязана удалить `p` — иначе утечка.
- Если исключение произошло при создании блока с deleter'ом, по стандарту вызывается `d(p)`. Поэтому deleter в конструкторе блока копируется, а не перемещается: наш исходный `d` остаётся валидным.
- `dispose()` помечен `noexcept`: бросающий deleter — это `std::terminate` (стандарт тоже требует, чтобы deleter не бросал).
- `ptr_control_block` уничтожает блок через `delete this`; для `inplace_control_block` к этому моменту `T` уже разрушен в `dispose()`, и деструктор блока не трогает `storage_`.

---

## 5. Потокобезопасность: что именно гарантируется

Гарантии такие же, как у `std::shared_ptr` (и они часто понимаются неправильно):

1. **Разные экземпляры** `shared_ptr`/`weak_ptr`, владеющие одним объектом, можно одновременно копировать, присваивать и уничтожать в разных потоках — счётчики атомарны.
2. **Один и тот же экземпляр** `shared_ptr` нельзя одновременно читать и писать из разных потоков без синхронизации — это обычная гонка данных на полях `ptr_`/`cb_`.
3. **Управляемый объект** не становится потокобезопасным. `shared_ptr` синхронизирует только время жизни и счётчики.
4. `use_count()` в многопоточной программе — приблизительное значение (в `relaxed`-чтении), использовать его для логики нельзя.
5. Для атомарной замены самого указателя `shared_ptr` в C++20 есть `std::atomic<std::shared_ptr<T>>`; до него — мьютекс или (устаревшие) `atomic_load`/`atomic_store`.

Контрольный блок — единственная точка синхронизации; в нашей реализации он использует только атомики (`std::atomic<long>`), без мьютексов. Некоторые реализации стандартных библиотек для `lock()` используют маленький мьютекс/спинлок (libstdc++ исторически), другие — CAS-цикл (libc++); CAS-вариант lock-free, но под высокой конкуренцией может «крутиться».

---

## 6. Классические подводные камни

- **Два `shared_ptr` из одного `new`:**

  ```cpp
  T* raw = new T;
  my::shared_ptr<T> a(raw), b(raw);   // два блока → double delete
  ```

  Правильно: `auto a = make_shared<T>(); auto b = a;`.

- **Циклы.** Два объекта, владеющих друг другом через `shared_ptr`, не уничтожатся никогда. Для обратных ссылок нужен `weak_ptr`.
- **`shared_ptr<T>(this)`** внутри метода — почти всегда ошибка; используйте `enable_shared_from_this`.
- **`shared_ptr` на объект со стека или на член другого объекта** — приведёт к `delete` не той памяти.
- **Смешивание владения с `unique_ptr`:** передавайте владение через `std::move(up)`, не через `up.get()`.
- **`get()` + ручное удаление** — почти гарантированный double delete.
- **Порядок аргументов до C++17:** `f(shared_ptr<T>(new T), g())` мог утечь, если `g()` бросал; `make_shared` эту проблему снимает.
- **`shared_ptr<void>`** допустим, но `operator*` для `void` использовать нельзя.

---

## 7. Чего не хватает до полноценного `std::shared_ptr`

Наш вариант — учебный. В стандартной библиотеке дополнительно есть:

| Возможность | Комментарий |
|---|---|
| Аллокаторы | `shared_ptr(Y*, D, A)`, `allocate_shared`, `allocator_traits` для блока |
| Массивы | специализация `shared_ptr<T[]>`, `operator[]`, `default_delete<T[]>` |
| Приведение указателей | `static_pointer_cast`, `dynamic_pointer_cast`, `const_pointer_cast`, `reinterpret_pointer_cast` |
| Сравнения | `operator==`, `operator<=>`, `std::hash`, `owner_less` |
| Атомарные операции | `std::atomic<std::shared_ptr<T>>`, `atomic_load`/`atomic_store` |
| Полный `enable_shared_from_this` | поддержка базового класса `enable_shared_from_this<Base>` |
| Точные `noexcept`/SFINAE | десятки мелких ограничений на конструкторы и `reset` |
| Оптимизации | `release` + `acquire`-fence вместо `acq_rel`, sized delete, отсутствие лишних инкрементов |
| `in_place`-конструкторы | `make_shared` с `std::in_place` и `initializer_list` |

Также в стандарте жёстко оговорено, что `T` может быть неполным типом в ряде конструкторов, что deleter должен быть `CopyConstructible`, и что `use_count()` — лишь приблизительная величина при наличии гонок.

---

## Итог

Минимальный корректный `shared_ptr` — это:

1. **контрольный блок** с двумя атомарными счётчиками, виртуальными `dispose()`/`destroy()` и стёртыми по типу deleter'ом/аллокатором;
2. **`shared_ptr`** = `{ptr, cb}` с инкрементом при копировании и `fetch_sub(..., acq_rel)` при уничтожении;
3. **`weak_ptr`** = `{ptr, cb}` с отдельным счётчиком и `lock()`, который делает «условный инкремент» через CAS;
4. **`make_shared`**, который размещает `T` внутри блока (одна аллокация);
5. **алиасящий конструктор** и **`enable_shared_from_this`** как «надстройки» над той же моделью владения.

Если вы понимаете, зачем нужны два счётчика, почему `weak_` начинается с единицы, почему `lock()` обязан быть CAS-циклом и зачем разделены `dispose()` и `destroy()`, — вы понимаете `std::shared_ptr` целиком; остальное в стандартной библиотеке — это аккуратность, аллокаторы и совместимость.