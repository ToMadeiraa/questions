# 5. Какие нововведения появились в стандарте С++20 и C++23

Ниже — подробный обзор двух стандартов. Важно держать их раздельно: **C++20** (ISO/IEC 14882:2020, рабочее имя C++2a) — крупнейшее обновление после C++11, а **C++23** (ISO/IEC 14882:2024, работа технически завершена в 2023 году, рабочее имя C++2b) — «доработка» идей C++20 плюс несколько важных самостоятельных добавлений.

---

# Часть I. C++20

## 1. Четыре «кита» C++20

Именно эти четыре вещи определяют лицо стандарта.

### 1.1. Concepts (концепты)

Именованные наборы требований к шаблонным параметрам. Заменяют многоэтажные SFINAE-конструкции и дают внятные ошибки компиляции.

```cpp
#include <concepts>

template<class T>
concept Integral = std::is_integral_v<T>;

template<Integral T>
T gcd(T a, T b);

// Сокращённая запись (abbreviated function template):
void print(Integral auto x);

// requires-выражение:
template<class T>
concept Printable = requires(T t, std::ostream& os) {
    { os << t } -> std::convertible_to<std::ostream&>;
    t.name();
};
```

Появились стандартные концепты: `std::same_as`, `std::derived_from`, `std::convertible_to`, `std::integral`, `std::floating_point`, `std::movable`, `std::copyable`, `std::semiregular`, `std::regular`, `std::equality_comparable`, `std::totally_ordered`, `std::invocable`, `std::predicate`, `std::relation`, `std::strict_weak_order` и др.

### 1.2. Modules (модули)

Замена `#include` для борьбы с временем компиляции и макрос-конфликтами:

```cpp
// math.ixx
export module math;

export int add(int a, int b) { return a + b; }

// main.cpp
import math;

int main() { return add(1, 2); }
```

Поддерживаются интерфейсные модули, разделы (`module :part;`), глобальный фрагмент (`module;`) и header units (`import <vector>;`).

### 1.3. Coroutines (корутины)

Беcстековые сопрограммы: `co_await`, `co_yield`, `co_return`, `std::coroutine_handle`, `std::suspend_always`, `std::suspend_never`. Пользователь описывает `promise_type` (`initial_suspend`, `final_suspend`, `return_value`, `yield_value`, `await_transform`, `unhandled_exception`, `get_return_object`).

```cpp
task<int> compute() {
    int x = co_await read_async();
    co_return x * 2;
}
```

На базе этого механизма в C++23 появился готовый `std::generator`.

### 1.4. Ranges

Диапазоны и «виды» (`views`) с ленивыми вычислениями и конвейером `|`:

```cpp
#include <ranges>

auto result = nums
    | std::views::filter([](int x) { return x % 2 == 0; })
    | std::views::transform([](int x) { return x * x; })
    | std::views::take(10);

std::ranges::sort(v);
auto it = std::ranges::find(v, 42);
```

Появились `std::ranges::range`, `sized_range`, `view`, `input_range`…`contiguous_range`, адаптеры `filter`, `transform`, `take`, `drop`, `reverse`, `iota`, `join`, `split`, `common`, `counted`, `elements`, `keys`, `values`, `all`, `empty`, `single`, `lazy_split`, а также `std::subrange`, `ref_view`, `owning_view`.

---

## 2. Языковые нововведения C++20

### 2.1. Трёхстороннее сравнение `<=>`

Оператор `<=>` (spaceship) и типы упорядочения `std::strong_ordering`, `std::weak_ordering`, `std::partial_ordering`. Позволяет одним оператором получить все шесть сравнений.

```cpp
struct Point {
    int x, y;
    auto operator<=>(const Point&) const = default;
};

bool b = (Point{1, 2} < Point{2, 1});
```

Есть также `std::is_eq`, `std::is_neq`, `std::is_lt`, `std::is_lteq`, `std::is_gt`, `std::is_gteq` и fallback-функции `std::compare_strong_order_fallback` и др.

### 2.2. Designated initializers

Инициализация агрегатов по именам полей:

```cpp
struct Point { int x; int y; };
Point p{ .y = 2, .x = 1 };   // порядок должен совпадать с объявлением
```

### 2.3. `consteval`, `constinit`, расширение `constexpr`

- `consteval` — «немедленная» функция, обязанная вычисляться на этапе компиляции.
- `constinit` — гарантия статической инициализации без динамической.
- `constexpr`-функции теперь могут содержать `dynamic_cast`, `typeid`, `try`/`catch`, аллокации; `constexpr`-аллокация и `constexpr std::string`/`std::vector` стали реальностью.
- `constexpr`-виртуальные функции.
- `std::is_constant_evaluated()` — проверка, вычисляется ли код на этапе компиляции.

```cpp
consteval int square(int x) { return x * x; }
constinit int global = square(5);

if (std::is_constant_evaluated()) { /* compile-time ветка */ }
```

### 2.4. `char8_t`

Отдельный тип для UTF-8, литералы `u8"..."` стали `const char8_t[]`. Появились `std::u8string`, `std::u8string_view`.

### 2.5. Новые атрибуты

| Атрибут | Назначение |
|---|---|
| `[[likely]]` / `[[unlikely]]` | подсказка ветвления |
| `[[no_unique_address]]` | пустые подобъекты не занимают место |
| `[[nodiscard("причина")]]` | сообщение к предупреждению |
| `[[deprecated("причина")]]` | сообщение к предупреждению |

### 2.6. `using enum`

```cpp
enum class Color { Red, Green, Blue };

void f() {
    using enum Color;
    Color c = Red;   // без Color::
}
```

### 2.7. Улучшения лямбд

- Шаблонные лямбды с явными параметрами: `[]<class T>(T x) { ... }`.
- Лямбды в невычисляемых контекстах (`decltype`, `sizeof`).
- Захват structured bindings.
- Раскрытие пакета в init-capture: `[args...] { ... }`.
- Лямбды без состояния по умолчанию конструируемы и присваиваемы.

### 2.8. Прочие языковые изменения

| Изменение | Суть |
|---|---|
| `explicit(bool)` | условная явность конструктора/оператора |
| Инициализатор в range-based for | `for (auto x : get_range())` c init-statement |
| Круглые скобки для агрегатов | `Point p(1, 2);` |
| `new T[]` с выводом размера | `new int[]{1, 2, 3}` |
| `typename` в большем числе контекстов | упрощение шаблонного синтаксиса |
| Уточнение `volatile` | часть операций с `volatile` объявлена устаревшей |
| `std::atomic` по умолчанию | значение инициализируется нулём |

---

## 3. Библиотечные нововведения C++20

### 3.1. `std::span`

Невладеющее представление непрерывной последовательности:

```cpp
void process(std::span<int> data);
process(v);          // из std::vector
process(arr);        // из массива
```

### 3.2. `std::format`

Типобезопасный формат в стиле Python (`{}`, `{:d}`, `{:.2f}`):

```cpp
#include <format>

auto s = std::format("Hello, {}! x = {:.2f}", name, x);
std::format_to(std::back_inserter(out), "{}", 42);
```

Есть `std::format_to`, `std::format_to_n`, `std::formatted_size`, `std::vformat`, `std::make_format_args`, `std::formatter`, `std::format_error`.

### 3.3. Многопоточность

- `std::jthread` — поток с автоматическим `join` и поддержкой `std::stop_token` / `std::stop_source` / `std::stop_callback`.
- `std::latch`, `std::barrier`, `std::counting_semaphore`, `std::binary_semaphore`.
- `std::atomic_ref` — атомарный доступ к обычному объекту.
- `std::atomic<T>::wait` / `notify_one` / `notify_all`, `std::atomic_flag::test`.
- `std::atomic<std::shared_ptr<T>>`, `std::atomic<std::weak_ptr<T>>`.

```cpp
std::jthread worker([](std::stop_token st) {
    while (!st.stop_requested()) do_work();
});
```

### 3.4. `std::source_location`

Доступ к информации о месте вызова без макросов:

```cpp
void log(std::string_view msg,
         const std::source_location& loc = std::source_location::current());
```

### 3.5. `std::chrono`: календарь и часовые пояса

`year_month_day`, `sys_days`, `weekday`, `zoned_time`, `current_zone`, `locate_zone`, `tai_clock`, `gps_clock`, `utc_clock`, `leap_second`.

### 3.6. Битовые операции, математика, константы

- `<bit>`: `std::bit_cast`, `std::endian`, `std::popcount`, `std::countl_zero`, `std::countr_zero`, `std::has_single_bit`, `std::bit_ceil`, `std::bit_floor`, `std::bit_width`, `std::rotl`, `std::rotr`.
- `std::midpoint`, `std::lerp` (`<cmath>`).
- `std::numbers` — `pi`, `e`, `sqrt2`, `phi`, `ln2`, `log2e` и др.
- `std::assume_aligned`, `std::to_address`.

### 3.7. `type_traits` и обобщённое программирование

`std::remove_cvref`, `std::type_identity`, `std::is_bounded_array`, `std::is_unbounded_array`, `std::is_nothrow_convertible`, `std::common_reference`, `std::is_layout_compatible`, `std::is_pointer_interconvertible_base_of`, `std::construct_at`, `std::is_constant_evaluated`.

### 3.8. Контейнеры и алгоритмы

- `std::erase`, `std::erase_if` для контейнеров.
- `std::map::contains`, `std::set::contains`, `std::unordered_map::contains`.
- `std::to_array`, `std::ssize`.
- Constexpr-версии `std::vector`, `std::string`, `std::optional`, `std::variant`, `std::array`.
- `std::make_shared` для массивов.

### 3.9. Строки

`std::string::starts_with`, `ends_with`; `std::string_view::starts_with`, `ends_with`; constexpr-`std::string`; `std::u8string`.

### 3.10. Прочее

`std::cmp_equal`, `std::cmp_less`, `std::cmp_greater`, `std::in_range`; `std::osyncstream`; `std::source_location`; `std::span`; `std::format`.

---

# Часть II. C++23

## 4. Языковые нововведения C++23

### 4.1. `if consteval`

Удобная замена `std::is_constant_evaluated()` в виде ветвления:

```cpp
constexpr int f(int x) {
    if consteval {
        return compile_time_impl(x);
    } else {
        return runtime_impl(x);
    }
}
```

### 4.2. Deducing this (explicit object parameter)

Явный параметр `this` позволяет писать «дедуцирующие» методы и просто реализовывать перегрузки по lvalue/rvalue:

```cpp
struct S {
    template<class Self>
    void f(this Self&& self);
};
```

Это сильно упрощает CRTP и перегрузки по квалификаторам.

### 4.3. `static operator()` и `static operator[]`

Лямбды и функторы могут иметь статический оператор вызова — без неявного `this`:

```cpp
struct F { static int operator()(int x) { return x * 2; } };
```

### 4.4. Многомерный `operator[]`

```cpp
struct Matrix {
    int operator[](int row, int col) const;
};
m[1, 2];
```

### 4.5. `[[assume]]`

Подсказка оптимизатору о выполнимости условия:

```cpp
[[assume(x > 0)]];
```

### 4.6. Препроцессор

- `#elifdef` и `#elifndef`.
- `#warning`.

### 4.7. Литералы и запись

- Суффиксы `uz` и `z` для `size_t`: `0uz`, `0z`.
- `auto(x)` и `auto{x}` — decay-copy.
- Именованные универсальные символы `\N{...}` и ограниченные escape-последовательности `\x{...}`, `\u{...}`, `\o{...}`.

### 4.8. Расширения `constexpr`

- `goto` в `constexpr`-функциях.
- `static constexpr` локальные переменные в `constexpr`-функциях.
- `constexpr std::unique_ptr`, `std::bitset`, `std::type_info::operator==`.
- `constexpr std::to_chars`/`std::from_chars` для целых.

### 4.9. Прочее

- `static`-лямбды (`[]() static { ... }`).
- Атрибуты на лямбдах.
- Удаление API сборки мусора (`std::declare_reachable` и др.).
- Уточнения по `char8_t` и `std::u8string`.

---

## 5. Библиотечные нововведения C++23

### 5.1. `std::expected`

Тип-результат «значение или ошибка» — замена исключениям и парам `(bool, T)`:

```cpp
std::expected<int, std::string> parse(std::string_view s);

if (auto r = parse("42"); r)
    std::cout << *r;
else
    std::cout << r.error();
```

Есть `std::unexpected<E>` и монадические операции: `and_then`, `transform`, `or_else`, `transform_error`.

### 5.2. Монадические операции `std::optional`

`and_then`, `transform`, `or_else` — цепочки без ручных проверок:

```cpp
auto name = get_user().and_then(get_name).transform(to_upper).value_or("anonymous");
```

### 5.3. `std::print` / `std::println`

Печать через `std::format` вместо `std::cout`:

```cpp
#include <print>

std::println("Hello, {}!", name);
std::print(stderr, "error: {}\n", code);
```

### 5.4. `std::mdspan`

Многомерное представление памяти:

```cpp
#include <mdspan>

std::mdspan<double, std::extents<int, 3, 4>> m(data);
m[1, 2] = 3.14;
```

### 5.5. `std::flat_map` и `std::flat_set`

Плоские (на основе отсортированного `vector`) аналоги `map`/`set` — быстрее по памяти и кэшу:

```cpp
#include <flat_map>

std::flat_map<int, std::string> fm;
```

### 5.6. `std::generator`

Готовый корутинный генератор:

```cpp
std::generator<int> range(int n) {
    for (int i = 0; i < n; ++i) co_yield i;
}

for (int x : range(10)) std::cout << x;
```

### 5.7. `std::stacktrace`

Стандартный стек вызовов:

```cpp
#include <stacktrace>

auto st = std::stacktrace::current();
std::cout << st;
```

### 5.8. Дополнения Ranges

Новые адаптеры-«виды»:

| Адаптер | Назначение |
|---|---|
| `views::zip` / `zip_transform` | параллельный обход нескольких диапазонов |
| `views::enumerate` | пары (индекс, значение) |
| `views::adjacent` / `adjacent_transform` | скользящее окно |
| `views::chunk` / `chunk_by` | разбиение на блоки |
| `views::slide` | скользящее окно фиксированного размера |
| `views::stride` | шаг |
| `views::join_with` | объединение с разделителем |
| `views::cartesian_product` | декартово произведение |
| `views::repeat` | бесконечное повторение |
| `views::as_const` / `as_rvalue` | преобразование категории |

Новые алгоритмы: `std::ranges::fold_left`, `fold_right`, `fold_left_first`, `fold_right_last`, `fold_left_with_iter`, `fold_left_first_with_iter`; `ranges::contains`, `starts_with`, `ends_with`, `find_last`, `find_last_if`, `find_last_if_not`; `ranges::iota`, `ranges::shift_left`, `shift_right`.

Плюс `std::ranges::to` — конверсия диапазона в контейнер:

```cpp
auto v = std::views::iota(1, 10)
       | std::views::filter([](int x) { return x % 2 == 0; })
       | std::ranges::to<std::vector>();
```

### 5.9. Утилиты

| Средство | Назначение |
|---|---|
| `std::to_underlying` | базовый тип enum |
| `std::unreachable` | «недостижимый» код |
| `std::byteswap` | перестановка байтов |
| `std::forward_like` | проброс категории значения |
| `std::invoke_r` | `invoke` с приведением результата |
| `std::bind_back` | частичное применение с конца |
| `std::move_only_function` | move-only `std::function` |
| `std::out_ptr` / `std::inout_ptr` | адаптеры для C-API |
| `std::allocate_at_least` | аллокация «не меньше» |
| `std::start_lifetime_as` | явное начало времени жизни объекта |
| `std::is_scoped_enum` | trait для `enum class` |
| `std::is_implicit_lifetime` | trait для неявного времени жизни |
| `std::reference_constructs_from_temporary` | trait про ссылки и временные |

### 5.10. Строки и контейнеры

- `std::string::contains`, `std::string_view::contains`.
- `std::string::resize_and_overwrite`.
- Конструкторы контейнеров от диапазона: `std::from_range_t`, `std::from_range`.
- `std::string_view` теперь конструируется из диапазона.

---

# Часть III. Сравнение и итог

## 6. Что важно запомнить

**C++20 — революция:**

1. **Concepts** — шаблоны с проверяемыми требованиями и понятными ошибками.
2. **Modules** — замена `#include`.
3. **Coroutines** — `co_await`/`co_yield`/`co_return`.
4. **Ranges** — ленивые конвейеры `|` и алгоритмы над диапазонами.
5. **`<=>`** — единый оператор сравнения.
6. **`consteval` / `constinit`** — усиление compile-time.
7. **`std::format`** — типобезопасная замена `printf`/`iostream`.
8. **`std::span`**, `std::jthread`, `std::latch`/`barrier`/`semaphore`, `std::source_location`.
9. **`char8_t`**, `std::bit_cast`, `std::numbers`, календарь `<chrono>`.

**C++23 — практичная доработка:**

1. **`std::expected`** и монадические операции `std::optional` — нормальная обработка ошибок без исключений.
2. **`std::print` / `std::println`** — простой вывод.
3. **`std::mdspan`** — многомерные массивы.
4. **`std::flat_map` / `std::flat_set`** — кэш-дружественные контейнеры.
5. **`std::generator`** — готовые корутины-генераторы.
6. **`std::stacktrace`** — стек вызовов в стандарте.
7. **`if consteval`, deducing this, `static operator()`, многомерный `operator[]`, `[[assume]]`** — язык стал удобнее.
8. **`std::ranges::to` и целая пачка новых views** (`zip`, `enumerate`, `chunk`, `slide`, `stride`, `cartesian_product`…).
9. **`std::move_only_function`, `std::to_underlying`, `std::unreachable`, `std::byteswap`, `std::forward_like`** — прикладные утилиты.

## 7. Краткая сводная таблица

| Направление | C++20 | C++23 |
|---|---|---|
| Парадигма шаблонов | Concepts, requires | Deducing this,static operator |
| Модульность | Modules | — (доработки) |
| Асинхронность | Coroutines (механизм) | `std::generator` (готовый тип) |
| Диапазоны | Ranges, views, `\|` | `ranges::to`, `zip`, `enumerate`, `chunk`, `fold_*` |
| Ошибки | — | `std::expected`, монады `optional` |
| Вывод | `std::format` | `std::print`/`println` |
| Многомерные данные | `std::span` | `std::mdspan` |
| Контейнеры | `contains`, `erase_if`, constexpr | `flat_map`/`flat_set`, `from_range` |
| Compile-time | `consteval`, `constinit`, `is_constant_evaluated` | `if consteval`, constexpr `unique_ptr`/`bitset` |
| Языковые мелочи | `<=>`, designated init, `char8_t`, `using enum` | `[[assume]]`, `uz`/`z`, `#elifdef`, `auto(x)` |
| Диагностика | — | `std::stacktrace` |

Оба стандарта вместе закрывают большой пласт «бытовых» неудобств C++: C++20 дал языку концепты, модули, корутины и диапазоны, а C++23 довёл библиотеку до состояния, когда стандартными средствами можно писать форматированный вывод, ожидаемые значения, многомерные массивы, генераторы и трассировки стека — без сторонних библиотек.