# 5. Какие нововведения появились в стандарте С++17

C++17 (ISO/IEC 14882:2017, до публикации — C++1z) — обновление менее радикальное, чем C++11, но тоже очень крупное. Если C++11 менял сам способ написания кода, то C++17 во многом «закрывал дыры»: добавлял то, чего давно не хватало (structured bindings, `optional`/`variant`/`any`, `filesystem`, параллельные алгоритмы), упрощал метапрограммирование (`if constexpr`, fold expressions, CTAD) и убирал старый балласт.

---

## 1. Общая характеристика

Ключевые направления C++17:

- **Удобство языка** — structured bindings, инициализатор в `if`/`switch`, inline-переменные.
- **Метапрограммирование** — `if constexpr`, fold expressions, вариадические улучшения, `_v`-суффиксы.
- **Вывод типов** — class template argument deduction (CTAD).
- **Библиотека** — «большая тройка» `std::optional` / `std::variant` / `std::any`, `std::string_view`, `std::filesystem`.
- **Производительность** — гарантированное копирование-элиминирование (copy elision), параллельные алгоритмы.
- **Чистка** — удаление `auto_ptr`, `random_shuffle`, триграфов и прочего устаревшего.

---

## 2. Языковые нововведения

### 2.1. Structured bindings (структурированные привязки)

Возможность «распаковать» кортеж, пару, массив или простую структуру в отдельные имена:

```cpp
std::pair<int, std::string> p{1, "one"};
auto [id, name] = p;

for (const auto& [key, value] : myMap)
    std::cout << key << " = " << value << '\n';

int arr[3] = {1, 2, 3};
auto [a, b, c] = arr;

struct Point { int x, y; };
auto [px, py] = Point{10, 20};
```

Работает и по ссылке: `auto& [k, v] = *it;`.

### 2.2. Инициализатор в `if` и `switch`

Позволяет ограничить область видимости вспомогательной переменной самим оператором:

```cpp
if (auto it = m.find(key); it != m.end())
    use(it->second);

switch (auto code = compute(); code) {
    case 0: /* ... */ break;
    default: /* ... */ break;
}
```

### 2.3. `if constexpr`

Ветвление на этапе компиляции: невыбранная ветка не инстанцируется, что делает шаблонный код гораздо проще SFINAE-хаков.

```cpp
template<class T>
void print(const T& value) {
    if constexpr (std::is_integral_v<T>)
        std::cout << "int: " << value;
    else
        std::cout << "other: " << value;
}
```

### 2.4. Fold expressions (свёртки)

Короткий синтаксис применения бинарной операции к пакету параметров:

```cpp
template<class... Ts>
auto sum(Ts... args) { return (args + ...); }         // унарная правая

template<class... Ts>
bool all_true(Ts... args) { return (... && args); }   // унарная левая

template<class... Ts>
auto sum_with_init(Ts... args) { return (0 + ... + args); } // с начальным значением
```

### 2.5. Class template argument deduction (CTAD) и deduction guides

Компилятор сам выводит параметры шаблона класса из аргументов конструктора:

```cpp
std::pair p{1, 2.5};              // std::pair<int, double>
std::vector v{1, 2, 3};           // std::vector<int>
std::tuple t{1, "hello", 3.0};    // std::tuple<int, const char*, double>
std::lock_guard lk(m);            // std::lock_guard<std::mutex>
```

Можно задавать собственные правила вывода:

```cpp
template<class T> struct Box { T value; };
Box(const char*) -> Box<std::string>;
```

### 2.6. `auto` в качестве non-type template parameter

```cpp
template<auto N>
struct Constant { static constexpr auto value = N; };

Constant<42> a;
Constant<'x'> b;
```

### 2.7. Inline-переменные

`inline` теперь применим к переменным, что позволяет определять их в заголовочных файлах без ODR-нарушений:

```cpp
// header.h
inline int global_counter = 0;

struct Config {
    inline static std::string name = "default";   // без отдельного определения
};
```

### 2.8. Расширение `constexpr`

- `constexpr`-лямбды.
- `constexpr`-функции могут содержать `if`, циклы, локальные переменные (в C++11 тело было почти пустым).
- Неявный `const` у `constexpr`-методов снят: `constexpr`-метод может быть non-const.
- `constexpr`-статические члены класса неявно `inline`.

```cpp
constexpr int factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; ++i) result *= i;
    return result;
}
```

### 2.9. `static_assert` без сообщения

```cpp
static_assert(sizeof(int) == 4);   // сообщение теперь необязательно
```

### 2.10. Новые стандартные атрибуты

| Атрибут | Назначение |
|---|---|
| `[[fallthrough]]` | подавляет предупреждение о «проваливании» в `switch` |
| `[[nodiscard]]` | предупреждает, если возвращаемое значение игнорируется |
| `[[maybe_unused]]` | подавляет предупреждение о неиспользуемой сущности |

```cpp
[[nodiscard]] int compute();
[[maybe_unused]] auto [a, b] = get_pair();

switch (x) {
    case 1:
        do_something();
        [[fallthrough]];
    case 2:
        do_more();
        break;
}
```

(Атрибуты `[[deprecated]]`, `[[noreturn]]` появились раньше — в C++11/14.)

### 2.11. Вложенные пространства имён

```cpp
namespace A::B::C {   // вместо namespace A { namespace B { namespace C {
    void f();
}
```

### 2.12. Улучшения лямбд

- Захват `*this` по значению: `[=, *this] { ... }` — копия объекта, а не указателя.
- `constexpr`-лямбды.
- Лямбды в `constexpr`-контексте.

### 2.13. Гарантированное копирование-элиминирование (guaranteed copy elision)

В ряде случаев компилятор **обязан** не создавать промежуточный объект — даже если конструктор копирования/перемещения удалён:

```cpp
struct NonCopyable {
    NonCopyable() = default;
    NonCopyable(const NonCopyable&) = delete;
};

NonCopyable make() { return NonCopyable{}; }   // OK в C++17, ошибка в C++14
NonCopyable x = NonCopyable{};                 // OK
```

### 2.14. `noexcept` стал частью типа функции

```cpp
void f() noexcept;
void g() throw();       // по-прежнему эквивалент noexcept
void (*p)() noexcept = &f;
static_assert(!std::is_same_v<decltype(&f), decltype(&g)>);
```

### 2.15. Уточнённый порядок вычислений

C++17 формально зафиксировал порядок для ряда выражений: постфиксные выражения, операторы `<<`/`>>`, индексация, вызов функции. Аргументы функции теперь **indeterminately sequenced** (не пересекаются), поэтому `f(i++, i++)` перестало быть UB (но осталось unspecified).

### 2.16. Прочие языковые изменения

| Изменение | Суть |
|---|---|
| `__has_include` | проверка наличия заголовка в препроцессоре |
| `auto x{1}` | теперь `int` (а не `initializer_list<int>`) |
| Списочная инициализация enum | разрешена для enum с фиксированным базовым типом |
| `typename` в template template parameter | `template<template<typename> typename>` |
| Агрегаты | расширены: базы, `default`-члены |
| `using` для наследования конструкторов | уточнены правила |

---

## 3. Нововведения стандартной библиотеки

### 3.1. `std::optional`

Тип, который может либо хранить значение, либо быть пустым — замена «магическим» значениям и `bool`-флагам:

```cpp
std::optional<int> parse(std::string_view s);

if (auto v = parse("42"); v)
    std::cout << *v;
else
    std::cout << "нет значения";

int x = opt.value_or(0);
```

`std::optional<T&>` запрещён; для ссылок используется `std::optional<std::reference_wrapper<T>>`.

### 3.2. `std::variant`

Типобезопасное объединение (sum type):

```cpp
std::variant<int, double, std::string> v = 42;
v = "text";

std::visit([](auto&& x) { std::cout << x; }, v);
std::holds_alternative<int>(v);
std::get<std::string>(v);
std::get_if<double>(&v);
```

Пустое состояние возможно только через `valueless_by_exception()`; для «пустого» альтернативного варианта есть `std::monostate`.

### 3.3. `std::any`

Контейнер для значения произвольного типа (с сохранением типа):

```cpp
std::any a = 42;
a = std::string("hello");

if (a.type() == typeid(std::string))
    std::cout << std::any_cast<std::string>(a);
```

Требует CopyConstructible; `std::any_cast` бросает `std::bad_any_cast` при неверном типе.

### 3.4. `std::string_view`

Невладеющее представление строки (указатель + длина) без копирования:

```cpp
void process(std::string_view sv);

process("literal");
process(std::string("temp"));

using namespace std::string_view_literals;
auto sv = "hello"sv;
```

Важно: не гарантирует нуль-терминатор и не владеет данными — следите за временем жизни.

### 3.5. `std::filesystem`

Полноценная библиотека работы с файловой системой (`<filesystem>`):

```cpp
namespace fs = std::filesystem;

for (const auto& entry : fs::recursive_directory_iterator("dir"))
    std::cout << entry.path() << '\n';

if (fs::exists(p) && fs::is_regular_file(p))
    std::cout << fs::file_size(p);

fs::create_directories("a/b/c");
fs::copy("src", "dst", fs::copy_options::recursive);
fs::remove_all("tmp");

auto p = fs::path("dir") / "file.txt";
```

Есть `path`, `directory_entry`, `file_status`, `perms`, `space_info`, `copy_options`, `current_path`, `temp_directory_path`, `absolute`, `canonical`, `relative` и т.д. На старых GCC требуется линковать `-lstdc++fs`.

### 3.6. Параллельные алгоритмы

Политики исполнения `<execution>`: `std::execution::seq`, `par`, `par_unseq`. Большинство алгоритмов `<algorithm>` и `<numeric>` получили перегрузки с политикой:

```cpp
#include <execution>

std::sort(std::execution::par, v.begin(), v.end());
std::for_each(std::execution::par_unseq, v.begin(), v.end(), f);
```

### 3.7. `std::byte`

Отдельный тип для «сырых» байтов (`enum class byte : unsigned char`), не являющийся числом:

```cpp
std::byte b{0xFF};
b = b | std::byte{0x0F};
auto n = std::to_integer<int>(b);
```

### 3.8. `<charconv>`

Быстрые, локаленезависимые преобразования чисел и строк:

```cpp
char buf[32];
auto [ptr, ec] = std::to_chars(buf, buf + sizeof buf, 42);

int value;
std::from_chars(buf, ptr, value);
```

В C++17 поддержаны и целые, и числа с плавающей точкой.

### 3.9. Полиморфные аллокаторы (`std::pmr`)

`<memory_resource>`: `std::pmr::memory_resource`, `polymorphic_allocator`, `monotonic_buffer_resource`, `unsynchronized_pool_resource`, `synchronized_pool_resource`, а также псевдонимы `std::pmr::vector`, `std::pmr::string` и т.п.

```cpp
std::pmr::monotonic_buffer_resource pool;
std::pmr::vector<int> v{&pool};
```

### 3.10. Прочие библиотечные добавления

**Утилиты и обобщённый код**

- `std::invoke`, `std::invoke_result`, `std::is_invocable`, `std::is_nothrow_invocable`.
- `std::apply`, `std::make_from_tuple`.
- `std::not_fn` — замена `std::not1`/`not2`.
- `std::as_const`.
- `std::launder`.
- `std::in_place`, `std::in_place_t`, `std::in_place_type`, `std::in_place_index`.
- `std::uncaught_exceptions` (множественное число).
- `std::size`, `std::empty`, `std::data` — свободные функции.
- `std::clamp`.
- `std::gcd`, `std::lcm` (`<numeric>`).
- `std::destroy_at`, `std::destroy`, `std::destroy_n`.
- `std::uninitialized_default_construct(_n)`, `std::uninitialized_value_construct(_n)`, `std::uninitialized_move(_n)`.
- `std::is_aggregate`, `std::has_unique_object_representations`.
- `std::is_swappable`, `std::is_nothrow_swappable`.
- `std::bool_constant`, `std::void_t`.
- `std::conjunction`, `std::disjunction`, `std::negation`.
- `_v`-суффиксы для всех type traits: `std::is_same_v<T, U>`, `std::is_integral_v<T>` и т.д.

**Алгоритмы и численные операции**

- `std::sample`, `std::for_each_n`.
- `std::reduce`, `std::transform_reduce`.
- `std::inclusive_scan`, `std::exclusive_scan`, `std::transform_inclusive_scan`, `std::transform_exclusive_scan`.
- Searchers: `std::default_searcher`, `std::boyer_moore_searcher`, `std::boyer_moore_horspool_searcher` для `std::search`.

**Контейнеры**

- `std::map`/`std::set`: `try_emplace`, `insert_or_assign`, `merge`, `extract`, `node_type`.
- `std::vector::emplace_back` теперь возвращает ссылку.
- `std::string::data()` получил non-const перегрузку.
- `std::shared_ptr<T[]>` — поддержка массивов.
- `std::enable_shared_from_this::weak_from_this`.

**Память и потоки**

- Выровненное выделение памяти: `std::align_val_t`, перегрузки `operator new`/`delete` с выравниванием, `std::aligned_alloc`.
- `std::scoped_lock` — блокировка нескольких мьютексов без deadlock.
- `std::shared_mutex` (разделяемый мьютекс; `shared_timed_mutex` и `shared_lock` появились в C++14).
- `std::atomic<T>::is_always_lock_free`.
- `std::hardware_destructive_interference_size`, `std::hardware_constructive_interference_size`.

---

## 4. Что было удалено или объявлено устаревшим

| Элемент | Статус в C++17 |
|---|---|
| `std::auto_ptr` | удалён |
| `std::random_shuffle` | удалён |
| `std::bind1st`, `bind2nd`, `ptr_fun`, `mem_fun`, `mem_fun_ref` | удалены |
| `std::unary_function`, `std::binary_function` | удалены |
| Триграфы | удалены |
| `register` | удалён как класс хранения |
| Динамическая спецификация `throw(T)` | удалена (кроме `throw()`) |
| `std::result_of` | устарел |
| `std::uncaught_exception` | устарел (заменён на `uncaught_exceptions`) |
| `std::iterator` | устарел |
| `std::codecvt`, `std::wstring_convert` | устарели |
| `std::not1`, `std::not2` | устарели (заменены `std::not_fn`) |
| `std::get_temporary_buffer`, `std::raw_storage_iterator` | устарели |
| `std::is_literal_type` | устарел |
| `std::allocator::construct/destroy` | устарели |

---

## 5. Итог

Самое важное, что дал C++17:

1. **Structured bindings** — `auto [a, b] = ...` радикально упростил работу с кортежами и словарями.
2. **`if constexpr` + fold expressions** — метапрограммирование стало читаемым, SFINAE-монстры ушли в прошлое.
3. **CTAD** — `std::pair p{1, 2.0}` вместо `std::pair<int, double> p{1, 2.0}`.
4. **`std::optional` / `std::variant` / `std::any`** — «большая тройка» типов-сумм и опциональности.
5. **`std::string_view`** — дешёвая передача строк без копирования.
6. **`std::filesystem`** — переносимая работа с файлами наконец вошла в стандарт.
7. **Параллельные алгоритмы** — декларативное распараллеливание стандартных алгоритмов.
8. **Гарантированное copy elision** — стало проще возвращать non-copyable объекты по значению.
9. **Inline-переменные и `[[nodiscard]]`/`[[fallthrough]]`/`[[maybe_unused]]`** — чище заголовочный и «предупреждающий» код.

По масштабу C++17 уступает C++11, но именно он сделал повседневный C++ заметно приятнее: меньше шаблонного шума, меньше ручных проверок на «пустоту», меньше велосипедов для файлов и строк.