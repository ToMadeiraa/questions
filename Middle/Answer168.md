# 169. Напишите свою реализацию std::atomic

> **Дисклеймер.** Ниже — учебная реализация «как это устроено внутри», а не замена стандартной библиотеке: в реальном коде всегда берите `std::atomic`. Код рассчитан на бэкенд GCC/Clang (`__atomic_*` встроенные функции); для MSVC см. § 13, там таблица соответствий. Ничего не компилировал — это «бумажный» разбор, как и просили.

---

## 1. Что именно требуется воспроизвести

`std::atomic<T>` — это шаблон, объекты которого:

| Требование | Смысл |
|---|---|
| **Атомарность** | `load/store/exexchange/CAS/fetch_*` не рвутся на части, гонки данных (data race) нет by design |
| **Порядки памяти** | каждый доступ принимает `std::memory_order` (`relaxed/acquire/release/acq_rel/seq_cst`) |
| **Отсутствие копирования** | копировать/перемещать нельзя, только загружать/записывать значение |
| **`noexcept`** | ни одна операция не бросает |
| **Только тривиально копируемые `T`** | `std::atomic<T>` требует `trivially copyable` (в C++20 — иначе программа некорректна) |
| **`is_lock_free()` / `is_always_lock_free`** | честный ответ, реализовано ли через одну аппаратную инструкцию |
| **`fetch_add/or/and/xor`** | только для целых и указателей; для знаковых — переполнение по модулю 2^N, без UB |
| **`compare_exchange`** | с записью фактического значения в `expected` при неудаче |

Ключевая мысль для ответа: **атомарность и порядок — разные задачи**. Атомарность даёт «железо» (одна инструкция) или блокировка; порядок памяти — барьеры/атрибуты инструкций.

---

## 2. Три стратегии реализации

1. **Lock-free на интринзиках** — `__atomic_load_n`, `__atomic_store_n`, `__atomic_exchange_n`, `__atomic_compare_exchange_n`, `__atomic_fetch_add` (GCC/Clang) или `_InterlockedExchange`/`_InterlockedCompareExchange` (MSVC). Одна инструкция CPU → честный lock-free. Применимо к целым, указателям, `bool` — т.е. к тому, что влезает в регистр.
2. **На спинлоке (`atomic_flag`)** — универсально для любого `T`, но не lock-free: поток может быть вытеснен, держа замок.
3. **Гибрид (как в настоящей библиотеке)** — на этапе компиляции выбираем специализацию: целые/указатели → интринзики; всё остальное → generic-версия, у которой GCC для «больших» типов вызывает `libatomic` (там внутри таблица блокировок по адресу). Именно так устроен `std::atomic<BigStruct>`.

Ниже реализуем все три «кирпича» и соберём их в один шаблон `my::atomic<T>`.

---

## 3. Служебный слой: `memory_order` → интринзики, барьеры

`std::memory_order` — это часть *интерфейса*, а не реализации, поэтому берём его из `<atomic>` и отображаем на внутренние константы компилятора.

```cpp
// ======================= my_atomic.hpp =======================
#pragma once

#include <atomic>       // нужен только std::memory_order
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace my {
namespace detail {

// std::memory_order: relaxed(0), consume(1), acquire(2),
//                    release(3), acq_rel(4), seq_cst(5)
constexpr int to_builtin(std::memory_order m) noexcept {
    switch (m) {
    case std::memory_order_relaxed: return __ATOMIC_RELAXED;
    case std::memory_order_consume: return __ATOMIC_ACQUIRE; // consume устарел, все так делают
    case std::memory_order_acquire: return __ATOMIC_ACQUIRE;
    case std::memory_order_release: return __ATOMIC_RELEASE;
    case std::memory_order_acq_rel: return __ATOMIC_ACQ_REL;
    case std::memory_order_seq_cst: return __ATOMIC_SEQ_CST;
    }
    return __ATOMIC_SEQ_CST; // недостижимо
}

// Полный барьер нужен ТОЛЬКО для seq_cst: acquire/release уже даёт
// сама блокировка, а вот единый глобальный порядок всех seq_cst-операций — нет.
inline void seq_cst_fence_if(std::memory_order m) noexcept {
    if (m == std::memory_order_seq_cst)
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

// Порядок "на неудаче" в compare_exchange: запрещены release и acq_rel
// и нельзя быть строже порядка "на успехе".
inline std::memory_order
failure_order(std::memory_order success, std::memory_order failure) noexcept {
    assert(failure != std::memory_order_release);
    assert(failure != std::memory_order_acq_rel);
    assert(static_cast<int>(failure) <= static_cast<int>(success)); // приблизительно
    return failure;
}

} // namespace detail

inline void atomic_thread_fence(std::memory_order m) noexcept {
    __atomic_thread_fence(detail::to_builtin(m));
}
inline void atomic_signal_fence(std::memory_order m) noexcept {
    __atomic_signal_fence(detail::to_builtin(m));   // барьер только для компилятора
}
```

Почему `consume → acquire`: `memory_order_consume` фактически провалился (C++17 — «не рекомендуется», C++20 — «не использовать»); GCC/Clang и MSVC реализуют его как `acquire`.

---

## 4. `atomic_flag` — единственный гарантированно lock-free примитив

По стандарту **только `atomic_flag`** обязан быть всегда lock-free. На нём и строят мьютексы/спинлоки.

```cpp
class atomic_flag {
public:
    constexpr atomic_flag() noexcept = default;          // в C++11/14 требовался ATOMIC_FLAG_INIT

    atomic_flag(const atomic_flag&) = delete;
    atomic_flag& operator=(const atomic_flag&) = delete;

    bool test_and_set(std::memory_order order = std::memory_order_seq_cst) noexcept {
        return __atomic_exchange_n(&f_, true, detail::to_builtin(order));
    }

    void clear(std::memory_order order = std::memory_order_seq_cst) noexcept {
        __atomic_store_n(&f_, false, detail::to_builtin(order));
    }

    bool test(std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return __atomic_load_n(&f_, detail::to_builtin(order));
    }

private:
    bool f_ = false;
};
```

В настоящем `std::atomic_flag` под `test_and_set` стоит `xchg` (x86) или `stxr`-цикл (ARM), а под `clear` — обычная запись `stlr`.

---

## 5. Спинлок — «мьютекс» для универсальной реализации

```cpp
class spinlock {
public:
    void lock(std::memory_order order = std::memory_order_acquire) noexcept {
        while (flag_.test_and_set(order)) {
            // «вежливое» ожидание: не долбим шину/кэш, пока замок занят
            while (flag_.test(std::memory_order_relaxed)) {
#if defined(__i386__) || defined(__x86_64__)
                __builtin_ia32_pause();     // PAUSE: подсказка Hyper-Threading
#elif defined(__aarch64__)
                asm volatile("yield" ::: "memory");
#endif
            }
        }
    }

    bool try_lock(std::memory_order order = std::memory_order_acquire) noexcept {
        return !flag_.test_and_set(order);
    }

    void unlock(std::memory_order order = std::memory_order_release) noexcept {
        flag_.clear(order);
    }

private:
    atomic_flag flag_;
};

namespace detail {
class spinlock_guard {                       // RAII
public:
    spinlock_guard(spinlock& l, std::memory_order order) noexcept : l_(l) { l_.lock(order); }
    ~spinlock_guard() { l_.unlock(std::memory_order_release); }
    spinlock_guard(const spinlock_guard&) = delete;
    spinlock_guard& operator=(const spinlock_guard&) = delete;
private:
    spinlock& l_;
};
} // namespace detail
```

`unlock` обязан быть `release`, `lock` — `acquire`: именно эта пара и публикует изменения, сделанные под замком.

---

## 6. Универсальный `atomic_locked<T>` — для любого trivially copyable `T`

```cpp
template <class T>
class atomic_locked {
    static_assert(std::is_trivially_copyable_v<T>,
                  "std::atomic<T>: T должен быть trivially copyable");

public:
    using value_type = T;

    atomic_locked() noexcept : value_{} {}          // C++20: value-initialized; в C++11/17 у std — нет
    explicit atomic_locked(T desired) noexcept : value_(desired) {}

    atomic_locked(const atomic_locked&) = delete;
    atomic_locked& operator=(const atomic_locked&) = delete;

    T load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
        detail::seq_cst_fence_if(order);            // для seq_cst барьер ДО захвата
        return load_locked();
    }

    void store(T desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
        {
            const detail::spinlock_guard g{lock_, std::memory_order_acquire};
            value_ = desired;
        }                                           // unlock(release) в ~spinlock_guard
        detail::seq_cst_fence_if(order);            // для seq_cst барьер ПОСЛЕ отпускания
    }

    T exchange(T desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
        detail::seq_cst_fence_if(order);
        T old = exchange_locked(desired);
        detail::seq_cst_fence_if(order);
        return old;
    }

    bool compare_exchange_weak(T& expected, T desired,
                               std::memory_order success = std::memory_order_seq_cst,
                               std::memory_order failure = std::memory_order_seq_cst) noexcept {
        const std::memory_order fail = detail::failure_order(success, failure);
        detail::seq_cst_fence_if(success);
        const bool ok = cas_locked(expected, desired);
        detail::seq_cst_fence_if(ok ? success : fail);
        return ok;
    }

    // strong = weak: "сильная" версия имеет право не сбоить ложно,
    // а наша и не сбоит — значит, обеим годится одна реализация.
    bool compare_exchange_strong(T& expected, T desired,
                                 std::memory_order success = std::memory_order_seq_cst,
                                 std::memory_order failure = std::memory_order_seq_cst) noexcept {
        return compare_exchange_weak(expected, desired, success, failure);
    }

    bool is_lock_free() const noexcept { return false; }
    static constexpr bool is_always_lock_free = false;

    T operator=(T desired) noexcept { store(desired); return desired; }
    operator T() const noexcept { return load(); }

private:
    T load_locked() const noexcept {
        const detail::spinlock_guard g{lock_, std::memory_order_acquire};
        return value_;                    // копия сделана под замком, до unlock
    }

    T exchange_locked(T desired) noexcept {
        const detail::spinlock_guard g{lock_, std::memory_order_acquire};
        T old = value_;
        value_ = desired;
        return old;
    }

    bool cas_locked(T& expected, T desired) noexcept {
        const detail::spinlock_guard g{lock_, std::memory_order_acquire};
        // сравниваем ПРЕДСТАВЛЕНИЕ объекта, как настоящий std::atomic
        // (там тоже memcmp: T вовсе не обязан иметь operator==)
        if (__builtin_memcmp(&value_, &expected, sizeof(T)) != 0) {
            __builtin_memcpy(&expected, &value_, sizeof(T));  // требование стандарта!
            return false;
        }
        value_ = desired;
        return true;
    }

    mutable spinlock lock_;
    T value_;
};
```

Три момента, которые любят спрашивать на защите:

* **`expected` при неудаче перезаписывается** фактическим значением — иначе не работал бы классический цикл `while (!cas(expected, ...)) {}`.
* **Сравнение по представлению (`memcmp`), а не по `==`** — в стандарте `T` не обязан быть сравнимым; отсюда же известная проблема типов с «дырами» (padding) в представлении.
* **`seq_cst` через мьютекс сам не получается.** Мьютекс даёт `acquire/release`, но два *разных* `my::atomic` (у каждого свой спинлок) могут переставиться «store → load», и запрещённое для SC поведение (литмус SB) станет возможным. Лечится полным барьером снаружи критической секции: `store` — барьер после `unlock`, `load` — барьер перед `lock` (стандартный приём, известный по работам Boehm'а). Альтернатива «в лоб» — один глобальный замок на все атомики: тогда SC получается даром, но масштабируемость умирает.

---

## 7. Lock-free специализация для целых

Здесь всё сводится к трём интринзикам: загрузка, запись, RMW.

```cpp
template <class T>
class atomic_integral {
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>,
                  "целые, кроме bool");

public:
    using value_type = T;
    using difference_type = T;

    constexpr atomic_integral() noexcept : v_{} {}
    constexpr explicit atomic_integral(T desired) noexcept : v_(desired) {}

    atomic_integral(const atomic_integral&) = delete;
    atomic_integral& operator=(const atomic_integral&) = delete;

    T load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return __atomic_load_n(&v_, detail::to_builtin(order));      // x86: mov
    }

    void store(T desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
        __atomic_store_n(&v_, desired, detail::to_builtin(order));   // x86: mov; seq_cst -> xchg
    }

    T exchange(T desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return __atomic_exchange_n(&v_, desired, detail::to_builtin(order)); // xchg
    }

    bool compare_exchange_weak(T& expected, T desired,
                               std::memory_order success = std::memory_order_seq_cst,
                               std::memory_order failure = std::memory_order_seq_cst) noexcept {
        const std::memory_order fail = detail::failure_order(success, failure);
        // внимание: у интринзика ровно та же семантика, что у std::atomic,
        // включая запись фактического значения в expected при неудаче
        return __atomic_compare_exchange_n(&v_, &expected, desired, /*weak=*/true,
                                           detail::to_builtin(success),
                                           detail::to_builtin(fail));   // lock cmpxchg
    }

    bool compare_exchange_strong(T& expected, T desired,
                                 std::memory_order success = std::memory_order_seq_cst,
                                 std::memory_order failure = std::memory_order_seq_cst) noexcept {
        const std::memory_order fail = detail::failure_order(success, failure);
        return __atomic_compare_exchange_n(&v_, &expected, desired, /*weak=*/false,
                                           detail::to_builtin(success),
                                           detail::to_builtin(fail));
    }

    T fetch_add(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return __atomic_fetch_add(&v_, arg, detail::to_builtin(order)); // lock xadd
    }
    T fetch_sub(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return __atomic_fetch_sub(&v_, arg, detail::to_builtin(order));
    }
    T fetch_and(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return __atomic_fetch_and(&v_, arg, detail::to_builtin(order));
    }
    T fetch_or(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return __atomic_fetch_or(&v_, arg, detail::to_builtin(order));
    }
    T fetch_xor(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return __atomic_fetch_xor(&v_, arg, detail::to_builtin(order));
    }

    // Составные операторы возвращают НОВОЕ значение — потому "+ 1".
    T operator++()    noexcept { return static_cast<T>(fetch_add(1) + 1); }
    T operator++(int) noexcept { return fetch_add(1); }
    T operator--()    noexcept { return static_cast<T>(fetch_sub(1) - 1); }
    T operator--(int) noexcept { return fetch_sub(1); }
    T operator+=(T arg) noexcept { return static_cast<T>(fetch_add(arg) + arg); }
    T operator-=(T arg) noexcept { return static_cast<T>(fetch_sub(arg) - arg); }
    T operator&=(T arg) noexcept { return static_cast<T>(fetch_and(arg) & arg); }
    T operator|=(T arg) noexcept { return static_cast<T>(fetch_or(arg)  | arg); }
    T operator^=(T arg) noexcept { return static_cast<T>(fetch_xor(arg) ^ arg); }

    T operator=(T desired) noexcept { store(desired); return desired; }
    operator T() const noexcept { return load(); }

    bool is_lock_free() const noexcept {
        return __atomic_is_lock_free(sizeof(T), const_cast<T*>(&v_));
    }
    // compile-time ответ, без вызова в рантайме
    static constexpr bool is_always_lock_free =
        __atomic_always_lock_free(sizeof(T), static_cast<T*>(nullptr));

private:
    T v_;
};
```

Тонкость: на x86-64 все `T` размером 1/2/4/8 байт действительно lock-free. На **32-битном** x86 `atomic<long long>` уже не lock-free — интринзик превратится в вызов `__atomic_load_8` из `libatomic` (нужен `-latomic`). Поэтому `is_always_lock_free` считается, а не предполагается.

---

## 8. Специализация для указателей

```cpp
template <class T>
class atomic_pointer {
public:
    using value_type = T*;
    using difference_type = std::ptrdiff_t;

    constexpr atomic_pointer() noexcept = default;
    constexpr explicit atomic_pointer(T* desired) noexcept : p_(desired) {}

    atomic_pointer(const atomic_pointer&) = delete;
    atomic_pointer& operator=(const atomic_pointer&) = delete;

    T* load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return __atomic_load_n(&p_, detail::to_builtin(order));
    }
    void store(T* desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
        __atomic_store_n(&p_, desired, detail::to_builtin(order));
    }
    T* exchange(T* desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return __atomic_exchange_n(&p_, desired, detail::to_builtin(order));
    }

    bool compare_exchange_weak(T*& expected, T* desired,
                               std::memory_order success = std::memory_order_seq_cst,
                               std::memory_order failure = std::memory_order_seq_cst) noexcept {
        const std::memory_order fail = detail::failure_order(success, failure);
        return __atomic_compare_exchange_n(&p_, &expected, desired, true,
                                           detail::to_builtin(success),
                                           detail::to_builtin(fail));
    }
    bool compare_exchange_strong(T*& expected, T* desired,
                                 std::memory_order success = std::memory_order_seq_cst,
                                 std::memory_order failure = std::memory_order_seq_cst) noexcept {
        const std::memory_order fail = detail::failure_order(success, failure);
        return __atomic_compare_exchange_n(&p_, &expected, desired, false,
                                           detail::to_builtin(success),
                                           detail::to_builtin(fail));
    }

    // Арифметика указателей — в ЭЛЕМЕНТАХ, а не в байтах.
    // Реализуем CAS-циклом: переносимо и без алиасинга.
    T* fetch_add(difference_type n, std::memory_order order = std::memory_order_seq_cst) noexcept {
        T* old = load(std::memory_order_relaxed);
        while (!compare_exchange_weak(old, old + n, order, std::memory_order_relaxed)) {
            // old уже перезаписан фактическим значением — просто пробуем снова
        }
        return old;
    }
    T* fetch_sub(difference_type n, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return fetch_add(-n, order);
    }

    T* operator++()    noexcept { return fetch_add(1) + 1; }
    T* operator++(int) noexcept { return fetch_add(1); }
    T* operator--()    noexcept { return fetch_sub(1) - 1; }
    T* operator--(int) noexcept { return fetch_sub(1); }
    T* operator+=(difference_type n) noexcept { return fetch_add(n) + n; }
    T* operator-=(difference_type n) noexcept { return fetch_sub(n) - n; }

    T* operator=(T* desired) noexcept { store(desired); return desired; }
    operator T*() const noexcept { return load(); }

    bool is_lock_free() const noexcept { return __atomic_is_lock_free(sizeof(T*), nullptr); }
    static constexpr bool is_always_lock_free =
        __atomic_always_lock_free(sizeof(T*), nullptr);

private:
    T* p_ = nullptr;
};
```

---

## 9. Специализация `bool`

`bool` — тоже 1 байт, но `__atomic_*` на `bool*` работает не везде одинаково аккуратно, поэтому храним `unsigned char`.

```cpp
class atomic_bool {
public:
    using value_type = bool;

    constexpr atomic_bool() noexcept = default;
    constexpr explicit atomic_bool(bool desired) noexcept : v_(desired ? 1u : 0u) {}

    atomic_bool(const atomic_bool&) = delete;
    atomic_bool& operator=(const atomic_bool&) = delete;

    bool load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return __atomic_load_n(&v_, detail::to_builtin(order)) != 0;
    }
    void store(bool desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
        __atomic_store_n(&v_, static_cast<unsigned char>(desired), detail::to_builtin(order));
    }
    bool exchange(bool desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return __atomic_exchange_n(&v_, static_cast<unsigned char>(desired),
                                   detail::to_builtin(order)) != 0;
    }

    bool compare_exchange_weak(bool& expected, bool desired,
                               std::memory_order success = std::memory_order_seq_cst,
                               std::memory_order failure = std::memory_order_seq_cst) noexcept {
        const std::memory_order fail = detail::failure_order(success, failure);
        unsigned char e = expected ? 1u : 0u;
        const bool ok = __atomic_compare_exchange_n(&v_, &e, static_cast<unsigned char>(desired),
                                                    true, detail::to_builtin(success),
                                                    detail::to_builtin(fail));
        expected = (e != 0);
        return ok;
    }
    bool compare_exchange_strong(bool& expected, bool desired,
                                 std::memory_order success = std::memory_order_seq_cst,
                                 std::memory_order failure = std::memory_order_seq_cst) noexcept {
        const std::memory_order fail = detail::failure_order(success, failure);
        unsigned char e = expected ? 1u : 0u;
        const bool ok = __atomic_compare_exchange_n(&v_, &e, static_cast<unsigned char>(desired),
                                                    false, detail::to_builtin(success),
                                                    detail::to_builtin(fail));
        expected = (e != 0);
        return ok;
    }

    bool operator=(bool desired) noexcept { store(desired); return desired; }
    operator bool() const noexcept { return load(); }

    bool is_lock_free() const noexcept { return true; }
    static constexpr bool is_always_lock_free = true;

private:
    unsigned char v_ = 0;
};
```

---

## 10. Сборка: `my::atomic<T>`

В настоящей библиотеке это один шаблон с частичными специализациями (в libstdc++ — наследование от `__atomic_base<_Tp, __integral>`). У нас для наглядности — три независимых класса и диспетчеризация через трейт:

```cpp
namespace detail {

template <class T, class = void>
struct impl_of { using type = atomic_locked<T>; };                 // всё остальное — под замок

template <class T>
struct impl_of<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
{ using type = atomic_integral<T>; };                             // целые — lock-free

template <>
struct impl_of<bool, void> { using type = atomic_bool; };

template <class T>
struct impl_of<T*, void> { using type = atomic_pointer<T>; };      // указатели — lock-free

} // namespace detail

// Финальный "std::atomic": алиас, выбирающий нужную реализацию.
template <class T>
using atomic = typename detail::impl_of<T>::type;

// макросы в духе стандарта
#define MY_ATOMIC_BOOL_LOCK_FREE  __GCC_ATOMIC_BOOL_LOCK_FREE
#define MY_ATOMIC_INT_LOCK_FREE   __GCC_ATOMIC_INT_LOCK_FREE
#define MY_ATOMIC_LONG_LOCK_FREE  __GCC_ATOMIC_LONG_LOCK_FREE
#define MY_ATOMIC_POINTER_LOCK_FREE __GCC_ATOMIC_POINTER_LOCK_FREE
```

Проверка на этапе компиляции:

```cpp
static_assert(my::atomic<int>::is_always_lock_free);
static_assert(my::atomic<int*>::is_always_lock_free);
static_assert(sizeof(my::atomic<int>) == sizeof(int));
static_assert(alignof(my::atomic<int>) >= alignof(int));
static_assert(!my::atomic<struct Big>::is_always_lock_free);
```

---

## 11. `compare_exchange` — главный «неочевидный» метод

Почему он такой:

```cpp
// weak: разрешено ложно сбоить (spurious failure) — только в цикле!
// strong: ложно сбоить не должен (кроме реального несовпадения значения).
bool ok = a.compare_exchange_weak(expected, desired, success_order, failure_order);
```

1. **`expected` — по ссылке**, потому что при неудаче туда пишется *фактическое* значение. Это и делает возможным идиому «прочитать → посчитать → попробовать ещё раз».
2. **`weak` в цикле, `strong` — одиночным вызовом.** `weak` на LL/SC-архитектурах (ARM, POWER) дешевле, но может сбоить сам по себе.
3. **Порядок на неудаче** не может быть `release`/`acq_rel` (нет записи, нечего публиковать) и не может быть строже порядка на успехе. Нарушение в `std::atomic` — UB; у нас — `assert` в отладочной сборке.
4. **`seq_cst` для CAS** — это `lock cmpxchg`; для `relaxed` — всё тот же `lock cmpxchg` на x86 (дешевле не бывает), а на ARM — `ldaxr/stlxr` vs `ldxr/stxr`.

Классический CAS-цикл «ручной» `fetch_add` (так его пишут для типов, где нет аппаратного `fetch_add`):

```cpp
template <class T>
T fetch_add_via_cas(atomic_integral<T>& a, T arg, std::memory_order order) {
    T old = a.load(std::memory_order_relaxed);
    while (!a.compare_exchange_weak(old, static_cast<T>(old + arg), order,
                                    std::memory_order_relaxed)) {
        // old обновлён, повторяем
    }
    return old;
}
```

---

## 12. Демонстрация: что должно работать

**Счётчик (relaxed — атомарность нужна, порядок нет):**

```cpp
my::atomic<int> counter{0};

void worker() {
    for (int i = 0; i < 100'000; ++i)
        counter.fetch_add(1, std::memory_order_relaxed);
}

int main() {
    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i) ts.emplace_back(worker);
    for (auto& t : ts) t.join();
    assert(counter.load() == 800'000);
}
```

**Публикация данных (release/acquire):**

```cpp
int payload = 0;                             // обычная, неатомарная переменная
my::atomic<bool> ready{false};

// производитель
payload = 42;
ready.store(true, std::memory_order_release);   // все записи до этого видимы

// потребитель
while (!ready.load(std::memory_order_acquire)) { std::this_thread::yield(); }
assert(payload == 42);                          // гарантировано
```

**Lock-free стек (push/pop) — CAS + порядки:**

```cpp
struct Node { int value; Node* next; };
my::atomic<Node*> head{nullptr};

void push(Node* n) {
    n->next = head.load(std::memory_order_relaxed);
    while (!head.compare_exchange_weak(n->next, n,
                                       std::memory_order_release,
                                       std::memory_order_relaxed)) {
        // n->next перезаписан актуальной головой
    }
}

Node* pop() {
    Node* old = head.load(std::memory_order_acquire);
    while (old && !head.compare_exchange_weak(old, old->next,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed)) {}
    return old;   // old == nullptr, если стек пуст
}
```

**Литмус SB («store buffering») — зачем нужен `seq_cst`:**

```cpp
my::atomic<int> x{0}, y{0};
int r1 = 0, r2 = 0;

// T1: x.store(1); r1 = y.load();
// T2: y.store(1); r2 = x.load();
// При acquire/release возможно r1 == 0 && r2 == 0.
// При seq_cst — НЕВОЗМОЖНО: все seq_cst-операции в одном тотальном порядке.
```

Именно этот тест «валит» наивную реализацию на спинлоке без внешних барьеров: у `x` и `y` разные замки, и пара store→load может переставиться. Наши `seq_cst_fence_if` до/после критической секции это закрывают.

---

## 13. Подводные камни и отличия от `std::atomic`

1. **Размер и выравнивание.** `sizeof(my::atomic<int>) == sizeof(int)` и `alignof` — только для lock-free специализаций. `atomic_locked<T>` содержит спинлок → больше и нестандартно для ABI. В стандарте `alignof(atomic<T>) >= alignof(T)`, и для некоторых платформ (ARM, 32-bit) атомарность требует выравнивания по размеру объекта — реальные библиотеки добавляют `alignas`.
2. **`volatile` ≠ `atomic`.** `volatile` не даёт ни атомарности, ни межпоточного порядка (это для MMIO/`setjmp`). И наоборот: атомики не нужно объявлять `volatile`.
3. **`relaxed` не «бесплатно» семантически** — он снимает только порядок, но не атомарность. Флаг готовности на `relaxed` — классическая ошибка.
4. **`memory_order_consume`** — фактически мертв, реализуется как `acquire`.
5. **Конструктор по умолчанию.** В C++11/17 `std::atomic<T> a;` оставляет значение **неинициализированным**; в C++20 — value-инициализация. У нас сразу `value_{}` (безопаснее, но это отличие от старого стандарта).
6. **`T` должен быть trivially copyable**, и CAS сравнивает *представление* (`memcmp`), а не `operator==` → типы с padding'ом могут вести себя неожиданно (то же и в std).
7. **Нет копирования/перемещения** → `std::vector<std::atomic<int>>` не собрать; используйте `std::deque`, `std::unique_ptr<T[]>` или фиксированный массив.
8. **`fetch_add` возвращает СТАРОЕ значение** — отсюда `+=` реализуется как `fetch_add(x) + x`.
9. **Переполнение для знаковых — не UB**, а арифметика по модулю 2^N (в отличие от обычного `int`).
10. **ABA-проблема** никуда не делась: CAS не различает «значение то же» и «значение вернулось». Лечится версионированием/тегированным указателем (double-word CAS).
11. **`atomic_locked` не lock-free**: поток, вытесненный под замком, блокирует остальных. Отсюда `is_lock_free() == false`, никаких гарантий прогресса.
12. **Чего у нас нет** (есть в C++20): `wait`/`notify_one`/`notify_all` (реализуются через futex/WaitOnAddress), `atomic_ref<T>`, `atomic<std::shared_ptr<T>>`, `constinit`, `fetch_max/min`.
13. **False sharing**: два «независимых» счётчика в одной кэш-линии дерутся за неё. Лечение — `alignas(64)` на объект.
14. **Порядки памяти не спасают от гонок на *неатомарных* данных**: `payload` в § 12 безопасен только потому, что доступ к нему упорядочен release/acquire-парой.

---

## 14. Как это сделано в настоящих библиотеках

| | Реализация |
|---|---|
| **libstdc++** | `<bits/atomic_base.h>`: `__atomic_base<_ITp, __integral>` + `atomic<_Tp>` наследует; внутри — `_M_i` и `__atomic_*` интринзики, `is_always_lock_free = __atomic_always_lock_free(sizeof(_Tp), 0)` |
| **libc++** | `__c11_atomic_*` (Clang, C11-атомики) либо встроенный `_Atomic`; для generic — `__atomic_*` |
| **MSVC** | `_InterlockedExchange`, `_InterlockedCompareExchange`, `_InterlockedIncrement`, `__iso_volatile_load/store`, `_ReadWriteBarrier`; `atomic_thread_fence` → `_ReadWriteBarrier` + `__faststorefence` |
| **`libatomic`** | если тип не влезает в машинное слово — вызов `__atomic_load_16` и т.п., внутри таблица блокировок по адресу (futex) |

**Что генерируется на x86-64:**

| Операция | Инструкция |
|---|---|
| `load` (любой порядок) | `mov` (на x86 загрузки не переупорядочиваются с загрузками) |
| `store` relaxed/release | `mov` |
| `store` seq_cst | `xchg` (неявный `lock`) либо `mov` + `mfence` |
| `exchange` | `xchg` |
| `compare_exchange` | `lock cmpxchg` |
| `fetch_add` | `lock xadd` |
| `fence(seq_cst)` | `mfence` / `lock add [rsp], 0` |

**ARM64:** `ldar`/`stlr` для acquire/release, `ldaxr`/`stlxr` для RMW, `dmb ish` для полного барьера.

Для MSVC наш код пришлось бы переписать: `__atomic_load_n` → `__iso_volatile_load` (или обычное чтение под `/volatile:iso`), `__atomic_store_n` → `__iso_volatile_store`, `__atomic_exchange_n` → `_InterlockedExchange`, `__atomic_compare_exchange_n` → `_InterlockedCompareExchange`, `__atomic_fetch_add` → `_InterlockedExchangeAdd`, `__atomic_thread_fence(seq_cst)` → `_ReadWriteBarrier()` + `__faststorefence()`; для 8-байтных — суффикс `64`, для 1/2/4-байтных — `8/16/`без суффикса.

---

## 15. Что сказать на защите (шпаргалка)

* `std::atomic` = **атомарность** (одна инструкция CPU или критическая секция) + **порядок памяти** (атрибуты инструкции/барьеры); это ортогональные вещи.
* Внутри — три уровня: `atomic_flag` (гарантированно lock-free примитив) → `atomic<T>` для целых/указателей на интринзиках (`lock cmpxchg`, `lock xadd`, `xchg`) → generic `atomic<T>` на спинлоке (или `libatomic` с таблицей блокировок) для произвольных trivially copyable `T`.
* `acquire/release` даёт мьютекс «бесплатно», а `seq_cst` требует дополнительного полного барьера снаружи критической секции — иначе два разных атомика нарушат единый глобальный порядок (литмус SB).
* `compare_exchange` — единственная «условная» операция: `weak` может сбоить ложно, `expected` при неудаче перезаписывается фактическим значением, порядок на неудаче не может быть `release`/`acq_rel`.
* `is_lock_free()` — рантайм-свойство платформы, `is_always_lock_free` — компайл-тайм константа (`__atomic_always_lock_free`).
* `volatile` не заменяет `atomic`; `relaxed` не отменяет атомарность; CAS не решает ABA.
* В продакшене — только `std::atomic`: своя реализация нужна, чтобы понимать, за что вы платите.