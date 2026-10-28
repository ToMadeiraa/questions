# 173. Реализуйте класс vector

## Постановка задачи

Требуется реализовать собственный шаблонный класс `vector`, аналогичный по поведению `std::vector` из стандартной библиотеки C++. Класс должен обеспечивать динамическое хранение элементов в непрерывном участке памяти, автоматическое управление памятью (RAII), корректную работу с копированием/перемещением и базовый набор операций.

## Основные требования к классу

1. **Шаблонность** — `template <typename T> class vector`, чтобы хранить элементы любого типа.
2. **Непрерывная память** — элементы хранятся в динамическом массиве `T*`, доступ по индексу за O(1).
3. **Три указателя (или size/capacity)**:
   - `data_` — указатель на начало буфера;
   - `size_` — текущее число элементов;
   - `capacity_` — вместимость буфера.
4. **RAII**: память освобождается в деструкторе, копирование/перемещение управляются явно (rule of five).
5. **Разделение создания объектов и выделения памяти**: для корректной работы с нетривиальными типами нужно использовать `operator new` + placement new для создания элементов и явный вызов деструкторов при удалении (а не `new[]`/`delete[]`).
6. **Гарантия строгой безопасности исключений** (strong exception guarantee) хотя бы на уровне `push_back` — если создание элемента бросит исключение, вектор не должен измениться.

## Пример реализации

```cpp
#include <cstddef>
#include <utility>
#include <stdexcept>
#include <algorithm>
#include <initializer_list>
#include <new>        // std::launder, placement new
#include <memory>     // std::allocator_traits (опционально)

template <typename T>
class vector {
public:
    using value_type      = T;
    using size_type       = std::size_t;
    using reference       = T&;
    using const_reference = const T&;
    using pointer         = T*;
    using const_pointer   = const T*;
    using iterator        = T*;
    using const_iterator  = const T*;

    // ---------- Конструкторы / деструктор ----------
    vector() noexcept = default;

    explicit vector(size_type n)
        : data_(allocate(n)), size_(0), capacity_(n) {
        // создаём n значений по умолчанию
        try {
            for (; size_ < n; ++size_)
                new (data_ + size_) T();
        } catch (...) {
            destroy_range(data_, data_ + size_);
            deallocate(data_);
            throw;
        }
    }

    vector(size_type n, const T& value)
        : data_(allocate(n)), size_(0), capacity_(n) {
        try {
            for (; size_ < n; ++size_)
                new (data_ + size_) T(value);
        } catch (...) {
            destroy_range(data_, data_ + size_);
            deallocate(data_);
            throw;
        }
    }

    vector(std::initializer_list<T> il)
        : data_(allocate(il.size())), size_(0), capacity_(il.size()) {
        try {
            for (const auto& v : il)
                new (data_ + size_++) T(v);
        } catch (...) {
            destroy_range(data_, data_ + size_);
            deallocate(data_);
            throw;
        }
    }

    // Копирование
    vector(const vector& other)
        : data_(allocate(other.size_)), size_(0), capacity_(other.size_) {
        try {
            for (; size_ < other.size_; ++size_)
                new (data_ + size_) T(other.data_[size_]);
        } catch (...) {
            destroy_range(data_, data_ + size_);
            deallocate(data_);
            throw;
        }
    }

    // Перемещение
    vector(vector&& other) noexcept
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_     = nullptr;
        other.size_     = 0;
        other.capacity_ = 0;
    }

    ~vector() {
        clear();
        deallocate(data_);
    }

    // ---------- Присваивание ----------
    vector& operator=(const vector& other) {
        if (this != &other)
            vector(other).swap(*this);   // copy-and-swap
        return *this;
    }

    vector& operator=(vector&& other) noexcept {
        if (this != &other) {
            clear();
            deallocate(data_);
            data_     = other.data_;
            size_     = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = other.capacity_ = 0;
        }
        return *this;
    }

    void swap(vector& other) noexcept {
        std::swap(data_,     other.data_);
        std::swap(size_,     other.size_);
        std::swap(capacity_, other.capacity_);
    }

    // ---------- Доступ ----------
    reference       operator[](size_type i)       { return data_[i]; }
    const_reference operator[](size_type i) const { return data_[i]; }

    reference at(size_type i) {
        if (i >= size_) throw std::out_of_range("vector::at");
        return data_[i];
    }
    const_reference at(size_type i) const {
        if (i >= size_) throw std::out_of_range("vector::at");
        return data_[i];
    }

    reference       front()       { return data_[0]; }
    const_reference front() const { return data_[0]; }
    reference       back()        { return data_[size_ - 1]; }
    const_reference back()  const { return data_[size_ - 1]; }

    pointer       data()       noexcept { return data_; }
    const_pointer data() const noexcept { return data_; }

    // ---------- Итераторы ----------
    iterator       begin()       noexcept { return data_; }
    iterator       end()         noexcept { return data_ + size_; }
    const_iterator begin() const noexcept { return data_; }
    const_iterator end()   const noexcept { return data_ + size_; }

    // ---------- Размеры ----------
    size_type size()     const noexcept { return size_; }
    size_type capacity() const noexcept { return capacity_; }
    bool      empty()    const noexcept { return size_ == 0; }

    // ---------- Модификаторы ----------
    void reserve(size_type new_cap) {
        if (new_cap <= capacity_) return;
        T* new_data = allocate(new_cap);
        size_type i = 0;
        try {
            for (; i < size_; ++i)
                new (new_data + i) T(std::move_if_noexcept(data_[i]));
        } catch (...) {
            destroy_range(new_data, new_data + i);
            deallocate(new_data);
            throw;
        }
        // переносим элементы и уничтожаем старые
        for (size_type j = 0; j < size_; ++j)
            data_[j].~T();
        deallocate(data_);
        data_     = new_data;
        capacity_ = new_cap;
    }

    void push_back(const T& value) {
        if (size_ == capacity_)
            reserve(capacity_ ? capacity_ * 2 : 1);
        new (data_ + size_) T(value);   // если бросит — размер не менялся
        ++size_;
    }

    void push_back(T&& value) {
        if (size_ == capacity_)
            reserve(capacity_ ? capacity_ * 2 : 1);
        new (data_ + size_) T(std::move(value));
        ++size_;
    }

    template <typename... Args>
    reference emplace_back(Args&&... args) {
        if (size_ == capacity_)
            reserve(capacity_ ? capacity_ * 2 : 1);
        new (data_ + size_) T(std::forward<Args>(args)...);
        return data_[size_++];
    }

    void pop_back() {
        data_[--size_].~T();
    }

    void clear() noexcept {
        destroy_range(data_, data_ + size_);
        size_ = 0;
    }

private:
    // ---------- Служебные ----------
    static T* allocate(size_type n) {
        return n ? static_cast<T*>(::operator new(n * sizeof(T))) : nullptr;
    }
    static void deallocate(T* p) noexcept {
        ::operator delete(p);
    }
    static void destroy_range(T* first, T* last) noexcept {
        for (; first != last; ++first)
            first->~T();
    }

    T*        data_     = nullptr;
    size_type size_     = 0;
    size_type capacity_ = 0;
};
```

## Ключевые моменты, которые проверяют на собеседовании / в задании

### 1. Почему не `new T[n]` / `delete[]`?
- `new T[n]` сразу создаёт `n` объектов (вызывает конструкторы по умолчанию) — не подходит, когда вместимость больше размера.
- Нельзя освободить «часть» массива.
- Поэтому используют сырую память через `::operator new(n * sizeof(T))` + **placement new** для создания отдельных объектов и явный вызов `~T()` для уничтожения.

### 2. Правило пяти
Если класс управляет ресурсом (динамическая память), нужно явно определить:
- деструктор;
- копирующий конструктор;
- копирующее присваивание;
- перемещающий конструктор;
- перемещающее присваивание.

Идиома **copy-and-swap** (`vector(other).swap(*this)`) даёт строгую гарантию безопасности исключений при копирующем присваивании.

### 3. Строгая гарантия исключений в `push_back` / `reserve`
- Сначала выделяем новую память и конструируем в ней элементы. Если на этом этапе исключение — старый буфер не тронут.
- Только после успешного копирования/перемещения уничтожаем старые элементы.
- `std::move_if_noexcept` — используем перемещение только если перемещающий конструктор не бросает исключений (иначе — копирование, чтобы сохранить strong guarantee).

### 4. Амортизированная сложность
`push_back` в среднем O(1): удвоение `capacity_` даёт амортизированную константу. `reserve` — O(n). `operator[]` — O(1). `insert`/`erase` в середину — O(n).

### 5. `noexcept` для перемещения и `swap`
Перемещающий конструктор и `swap` помечены `noexcept` — это позволяет `std::vector<vector<T>>` (и нашему) использовать перемещение при реаллокации, а не копирование.

### 6. `std::launder`
При работе с placement new поверх уже существующих объектов в C++17+ формально требуется `std::launder` для доступа к «новому» объекту через указатель. В упрощённой учебной реализации этим часто пренебрегают; в строгом коде — используют.

## Что ещё можно добавить (для «плюса» на защите)

- `insert`, `erase`, `emplace`, `resize`, `shrink_to_fit`;
- `operator==`, `operator!=` (лексикографическое сравнение);
- поддержку аллокаторов (`std::allocator_traits`) — как в `std::vector`;
- `const_reverse_iterator`, `rbegin/rend`;
- обработку случая `size_type` переполнения при `capacity_ * 2`.

## Итог

Класс `vector` — это учебный пример применения RAII, rule of five, placement new, гарантий исключений и амортизированного роста. Главное — разделять **выделение памяти** (`::operator new`) и **время жизни объектов** (placement new / явный деструктор), иначе реализация будет либо неверной для нетривиальных типов, либо неэффективной.