# 5. Какие нововведения появились в стандарте С++11

C++11 (ISO/IEC 14882:2011, до публикации известный как C++0x) — одно из самых крупных обновлений языка за всю его историю. Фактически это «новый C++»: язык стал заметно выразительнее, безопаснее и, главное, пригодным для написания переносимого многопоточного кода. Ниже — систематизированный обзор нововведений: сначала языковые, затем библиотечные, отдельно — модель памяти и многопоточность, и в конце — что было удалено или объявлено устаревшим.

---

## 1. Общая характеристика

Основные цели стандарта:

- **Выразительность** — писать меньше кода для тех же задач (`auto`, range-based for, лямбды).
- **Эффективность** — избегать лишних копирований (семантика перемещения).
- **Безопасность** — типизированный `nullptr`, `enum class`, `override`/`final`, `noexcept`.
- **Многопоточность** — формальная модель памяти и стандартная библиотека потоков.
- **Обобщённое программирование** — вариадические шаблоны, псевдонимы шаблонов, `constexpr`.
- **Единообразие инициализации** — синтаксис `{}` для всего.

---

## 2. Языковые нововведения

### 2.1. Вывод типов: `auto` и `decltype`

`auto` выводит тип переменной из инициализатора, `decltype` — тип выражения без его вычисления.

```cpp
auto x = 42;            // int
auto it = v.begin();    // итератор, тип не пишем вручную
decltype(x + 1.0) y;    // double

// Возвращаемый тип, вычисляемый компилятором:
template<class A, class B>
auto add(A a, B b) -> decltype(a + b) { return a + b; }
```

Появились также `std::declval` для использования в `decltype` внутри SFINAE-конструкций.

### 2.2. `nullptr`

Введён отдельный тип `std::nullptr_t` и ключевое слово `nullptr`, которое не является целым числом и не превращается в `0` неявно. Это устранило классическую проблему перегрузок `f(int)` / `f(char*)`.

```cpp
void f(int);
void f(char*);
f(nullptr);   // однозначно вызовет f(char*)
```

### 2.3. Единообразная инициализация и `std::initializer_list`

Списковая инициализация `{}` стала применима почти везде: для переменных, членов, массивов, контейнеров, возврата из функции. Появился `std::initializer_list<T>` и конструкторы, принимающие его.

```cpp
struct Point { int x, y; };
Point p{1, 2};
std::vector<int> v{1, 2, 3, 4};
int arr[]{1, 2, 3};

// Запрет сужающих преобразований:
int i{3.14};   // ошибка компиляции
```

### 2.4. Range-based for

Цикл по диапазону без явных итераторов:

```cpp
for (const auto& item : container)
    process(item);

for (auto&& x : container)   // универсальная ссылка
    modify(x);
```

### 2.5. Семантика перемещения (rvalue references)

Ключевое нововведение: ссылки на rvalue `T&&`, конструкторы/операторы перемещения, `std::move`, `std::forward`, свёртка ссылок (reference collapsing) и «универсальные» (forwarding) ссылки. Позволило передавать ресурсы без копирования и хранить в контейнерах move-only типы.

```cpp
std::string a = "long string";
std::string b = std::move(a);        // a перестаёт быть валидным по значению

template<class T>
void wrapper(T&& arg) {              // forwarding reference
    inner(std::forward<T>(arg));     // идеальная передача
}
```

Правило пяти (деструктор, копирование, перемещение) пришло на смену правилу трёх.

### 2.6. Ссылочные квалификаторы методов

Метод можно ограничить вызовом только на lvalue или только на rvalue:

```cpp
struct S {
    void f() &;    // только для lvalue-объекта
    void f() &&;   // только для rvalue-объекта
};
```

### 2.7. Лямбда-выражения

Анонимные функции с захватом контекста — вероятно, самое популярное нововведение:

```cpp
int threshold = 10;
auto pred = [threshold](int x) { return x > threshold; };
std::sort(v.begin(), v.end(), [](int a, int b) { return a > b; });

[&]  { /* захват всех по ссылке */ };
[=]  { /* захват всех по значению */ };
[this] { /* захват this */ };
```

> Важно: обобщённые лямбды (`[](auto x){}`) и init-capture (`[y = x + 1]`) — это уже **C++14**, а не C++11.

### 2.8. `constexpr`

Вычисления на этапе компиляции: `constexpr`-функции и переменные. В C++11 тело `constexpr`-функции было сильно ограничено (по сути, одно `return`-выражение).

```cpp
constexpr int square(int x) { return x * x; }
int arr[square(4)];   // размер массива известен компилятору
```

### 2.9. `static_assert`

Проверки утверждений на этапе компиляции:

```cpp
static_assert(sizeof(void*) == 8, "Требуется 64-битная платформа");
```

В C++11 сообщение обязательно (форма без сообщения — C++17).

### 2.10. `noexcept`

Спецификатор и оператор `noexcept` заменили устаревшую динамическую спецификацию `throw(...)`:

```cpp
void f() noexcept;                 // не бросает исключений
static_assert(noexcept(f()));
```

### 2.11. `override`, `final`, `= default`, `= delete`

Явное управление переопределением и генерацией специальных функций:

```cpp
struct Base {
    virtual void f();
    virtual ~Base() = default;
};
struct Derived : Base {
    void f() override;             // ошибка компиляции, если не переопределяет
    void g() final;                // запрет дальнейшего переопределения
    Derived(const Derived&) = delete;   // запрет копирования
};
```

### 2.12. Делегирующие и наследуемые конструкторы

```cpp
struct S {
    S() : S(42) {}       // делегирование другому конструктору
    S(int x) : v(x) {}
    int v;
};

struct D : Base {
    using Base::Base;    // наследование конструкторов
};
```

### 2.13. `enum class` (scoped enumerations)

Строго типизированные перечисления с собственной областью видимости и явным базовым типом:

```cpp
enum class Color : unsigned char { Red, Green, Blue };
Color c = Color::Red;
// int i = c;   // ошибка: неявного преобразования нет
```

### 2.14. Вариадические шаблоны

Шаблоны с произвольным числом параметров, `sizeof...`, раскрытие пакетов:

```cpp
template<class... Ts>
void print(const Ts&... args) {
    (std::cout << ... << args);   // (это уже C++17, но идея — из C++11)
}

template<class... Ts>
struct Tuple;

template<class T, class... Rest>
struct Tuple<T, Rest...> { T head; Tuple<Rest...> tail; };
```

На них построены `std::tuple`, `std::function`, `std::thread` и др.

### 2.15. Псевдонимы типов и шаблонов

`using` заменил `typedef` и добавил шаблонные псевдонимы:

```cpp
using Int = int;
template<class T> using Vec = std::vector<T, MyAllocator<T>>;
```

### 2.16. Прочие языковые изменения

| Нововведение | Суть |
|---|---|
| `>>` в шаблонах | `vector<vector<int>>` больше не требует пробела |
| `extern template` | явное подавление неявного инстанцирования |
| Сырые строки | `R"(C:\path\file)"`, с произвольным разделителем |
| Unicode-литералы | `char16_t`, `char32_t`, `u"..."`, `U"..."`, `u8"..."` |
| Пользовательские литералы | `operator"" _km(long double)` |
| Атрибуты | единый синтаксис `[[noreturn]]`, `[[carries_dependency]]` |
| `alignas` / `alignof` | управление и запрос выравнивания |
| `long long` | официально стандартизирован (как в C99) |
| `explicit operator bool` | явные операторы преобразования |
| `inline namespace` | версионирование API |
| `final`/`override` | контекстные ключевые слова, не ломающие старый код |

---

## 3. Нововведения стандартной библиотеки

### 3.1. Умные указатели

`std::unique_ptr` (с поддержкой массивов и пользовательского deleter), `std::shared_ptr`, `std::weak_ptr`, `std::make_shared`, `std::enable_shared_from_this`. `std::auto_ptr` объявлен устаревшим (удалён в C++17).

```cpp
auto p = std::make_unique<int>(42);        // make_unique — это C++14
std::unique_ptr<Widget> q(new Widget);     // так — в C++11
auto s = std::make_shared<Widget>(args...);
```

### 3.2. `std::function`, `std::bind`, `std::ref`

Тип-обёртка для любых вызываемых сущностей, частичное применение и ссылочные обёртки:

```cpp
std::function<int(int)> f = [](int x) { return x * 2; };
using namespace std::placeholders;
auto g = std::bind(add, 10, _1);
std::thread t(std::ref(obj));   // без копирования
```

Также появились `std::mem_fn`, `std::reference_wrapper`, `std::cref`.

### 3.3. Новые контейнеры и адаптеры

- `std::array` — статический массив с интерфейсом контейнера.
- `std::forward_list` — односвязный список.
- `std::unordered_map`, `std::unordered_multimap`, `std::unordered_set`, `std::unordered_multiset` — хеш-таблицы.
- `std::tuple` — кортеж произвольной длины (`std::get`, `std::tie`, `std::tuple_cat`, `std::make_tuple`).

У существующих контейнеров появились `emplace`/`emplace_back`/`emplace_hint`, `cbegin`/`cend`, `crbegin`/`crend`, `data()`, `shrink_to_fit()`, `at()` у ассоциативных контейнеров, конструкторы из `initializer_list`.

### 3.4. Регулярные выражения

Заголовок `<regex>`: `std::regex`, `std::smatch`, `std::regex_match`, `std::regex_search`, `std::regex_replace`.

### 3.5. Случайные числа, время, рациональные дроби

- `<random>` — нормальный ГПСЧ (`std::mt19937` и др.) и распределения; `std::shuffle`.
- `<chrono>` — `duration`, `time_point`, `system_clock`, `steady_clock`, `high_resolution_clock`, `duration_cast`.
- `<ratio>` — рациональная арифметика на этапе компиляции.

> Литералы `1s`, `100ms` для chrono — это **C++14**; в C++11 пишут `std::chrono::seconds(1)`.

### 3.6. Метапрограммирование: `<type_traits>`

`std::is_same`, `std::enable_if`, `std::conditional`, `std::decay`, `std::common_type`, `std::integral_constant`, `std::underlying_type`, `std::is_trivially_copyable` и десятки других — основа для SFINAE и обобщённого кода.

### 3.7. Утилиты и алгоритмы

- `std::to_string`, `std::stoi`/`stol`/`stod` и т.д.
- Свободные `std::begin`/`std::end`, `std::next`/`std::prev`.
- Алгоритмы: `all_of`, `any_of`, `none_of`, `copy_if`, `copy_n`, `find_if_not`, `is_sorted`, `is_permutation`, `minmax`, `minmax_element`, `partition_copy`, `move`, `move_backward`, `shuffle`, `iota`.
- `std::addressof`, `std::align`, `std::aligned_storage`, `std::max_align_t`.
- `std::type_index` (`<typeindex>`), `std::hash`.

### 3.8. Обработка ошибок и системные коды

- `std::exception_ptr`, `std::current_exception`, `std::rethrow_exception`, `std::nested_exception`, `std::throw_with_nested`, `std::rethrow_if_nested`.
- `std::error_code`, `std::error_condition`, `std::system_error`, `std::errc`, `std::system_category`, `std::generic_category`.
- `std::quick_exit`, `std::at_quick_exit`, `std::get_terminate`, `std::set_terminate`.

### 3.9. Прочее

- `<cstdint>` — целые фиксированной ширины (`int32_t`, `uint64_t`).
- `<codecvt>`, `std::wstring_convert`, `std::u16string`/`std::u32string`.
- `std::chrono`, `std::ratio`, `std::atomic` (см. ниже).
- `std::allocator_traits`, `std::pointer_traits`, `std::scoped_allocator_adaptor`, `std::uses_allocator`.
- `std::unique_ptr`/`std::shared_ptr`-специфичные `std::static_pointer_cast`, `dynamic_pointer_cast`, `const_pointer_cast`, `get_deleter`.
- Атомарные операции над `shared_ptr`: `std::atomic_load`, `atomic_store`, `atomic_exchange`, `atomic_compare_exchange`.

---

## 4. Многопоточность и модель памяти

Это, пожалуй, второе по значимости направление после семантики перемещения.

### 4.1. Потоки

Заголовок `<thread>`: `std::thread`, `std::this_thread` (`get_id`, `yield`, `sleep_for`, `sleep_until`), `std::thread::hardware_concurrency()`.

```cpp
std::thread t([]{ do_work(); });
t.join();
```

### 4.2. Синхронизация

`<mutex>`: `std::mutex`, `recursive_mutex`, `timed_mutex`, `recursive_timed_mutex`, `std::lock_guard`, `std::unique_lock`, `std::lock`, `std::try_lock`, теги `defer_lock`, `try_to_lock`, `adopt_lock`, `std::once_flag`/`std::call_once`.

`<condition_variable>`: `std::condition_variable`, `condition_variable_any`, `std::cv_status`, `std::notify_all_at_thread_exit`.

### 4.3. Атомика и модель памяти

`<atomic>`: `std::atomic<T>`, `std::atomic_flag`, `std::memory_order_*` (`relaxed`, `acquire`, `release`, `acq_rel`, `seq_cst`), барьеры `atomic_thread_fence`/`atomic_signal_fence`, `std::kill_dependency`, `std::atomic_init`, типы `atomic_int` и т.п.

Стандарт формально определил отношения **sequenced-before**, **synchronizes-with** и **happens-before**, а гонки данных (data race) объявил неопределённым поведением. Это позволило писать корректный переносимый многопоточный код без опоры на конкретный компилятор.

### 4.4. Асинхронность

`<future>`: `std::future`, `std::shared_future`, `std::promise`, `std::packaged_task`, `std::async`, политики `std::launch::async`/`deferred`, `std::future_status`, `std::future_error`.

### 4.5. `thread_local`

Поток-локальные переменные с временем жизни потока:

```cpp
thread_local int counter = 0;
```

---

## 5. Что было удалено или объявлено устаревшим

| Элемент | Статус в C++11 |
|---|---|
| `std::auto_ptr` | устарел (удалён в C++17) |
| Динамическая спецификация исключений `throw(...)` | устарела (удалена в C++20) |
| `register` | устарел (удалён в C++17) |
| `std::bind1st`, `bind2nd`, `ptr_fun`, `mem_fun`, `mem_fun_ref` | устарели (удалены в C++17) |
| `std::unary_function`, `std::binary_function` | устарели (удалены в C++17) |
| Ключевое слово `export` (экспорт шаблонов) | удалено |
| `std::gets` | удалён |

---

## 6. Итог

Если сжать до самого главного, C++11 дал программисту пять «несущих» идей:

1. **Вывод типов** — `auto`, `decltype`, `decltype(...)` в возвращаемом типе.
2. **Семантика перемещения** — `T&&`, `std::move`, `std::forward`, правило пяти.
3. **Лямбды и `std::function`** — функциональный стиль без рукописных функторов.
4. **Универсальная инициализация и range-based for** — короче и безопаснее.
5. **Многопоточность и модель памяти** — `std::thread`, `std::atomic`, `thread_local`, формальные гарантии.

Плюс «инфраструктурные» улучшения: умные указатели, `enum class`, `constexpr`, `noexcept`, `override`/`final`, вариадические шаблоны, `nullptr`, хеш-таблицы, `<regex>`, `<chrono>`, `<random>`, `<type_traits>`.

Именно после C++11 язык стали воспринимать как фактически новый язык — отсюда и распространённая формулировка «C++11 feels like a new language» (Бьёрн Страуструп).