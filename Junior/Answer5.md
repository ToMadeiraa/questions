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

# `std::function`, `std::bind`, `std::ref`, `std::mem_fn`, `std::reference_wrapper`, `std::cref`

## 0. Общая картина

Все шесть сущностей объявлены в `<functional>` и пришли в C++11. Они решают три разные задачи, которые полезно держать раздельно:

| Задача | Инструменты |
|---|---|
| Хранить **ссылку** как объект (копируемый, присваиваемый) | `std::reference_wrapper`, `std::ref`, `std::cref` |
| **Стереть тип** вызываемого объекта (type erasure) | `std::function` |
| **Адаптировать** вызываемый объект (частичное применение, перестановка аргументов, указатели на члены) | `std::bind`, `std::mem_fn` |

Их общий фундамент — правило **INVOKE**: единый набор правил «как вызвать `f` с аргументами `a...`», который умеет работать с обычными функциями, функциональными объектами, указателями на функции-члены и указателями на данные-члены, а также разыменовывать умные указатели и `reference_wrapper`. В C++17 это правило оформили в виде `std::invoke`:

```cpp
std::invoke(pm, obj);   // (obj.*pm)()
std::invoke(pm, ptr);   // ((*ptr).*pm)()
```

Полезно помнить: `bind` и `mem_fn` — это, по сути, «сахар» над INVOKE, а `function` — стирание типа над INVOKE. В современном C++ почти всё это заменяется лямбдами, но понимать механику нужно: она лежит в основе `std::thread`, `std::async`, `std::make_tuple`, алгоритмов и старого кода.

---

## 1. `std::reference_wrapper<T>`

```cpp
template< class T >
class reference_wrapper;
```

Это **копируемая и присваиваемая обёртка вокруг ссылки** `T&`. Внутри хранится указатель `T*` (получаемый через `std::addressof`, чтобы корректно работать с перегруженным `operator&`).

Ключевые свойства:

- `get()` возвращает `T&`; есть неявное преобразование `operator T&() const noexcept`, поэтому `reference_wrapper<T>` подставляется туда, где ожидается `T&`.
- **Нет состояния «пусто»**, нельзя переприсвоить на другой объект «по значению» — только скопировать указатель (см. ловушку ниже).
- Не является владельцем: объект должен жить дольше обёртки.
- Тривиально копируется; копирование дешёвое (одна маска указателя).
- Есть частичная специализация для **функциональных типов** `reference_wrapper<R(Args...)>`: у неё есть `operator()`, поэтому обёртка вокруг функции сама вызываема.
- С C++20 конструктор и `get()` стали `constexpr`.

```cpp
#include <functional>
#include <vector>
#include <iostream>

int main() {
    int a = 1, b = 2;
    std::reference_wrapper<int> ra = a;

    ra.get() = 10;      // a == 10
    int& ref = ra;      // неявное преобразование
    ref = 20;           // a == 20

    // Контейнер «ссылок» — частая идиома
    std::vector<int> v{1, 2, 3};
    std::vector<std::reference_wrapper<int>> r(v.begin(), v.end());
    for (int& x : r) x *= 2;   // меняет сам v: {2, 4, 6}
}
```

### Главная ловушка: `operator=` перепривязывает, а не присваивает

```cpp
int x = 1, y = 2;
std::reference_wrapper<int> rx = x, ry = y;

rx = ry;    // КОПИРУЕТСЯ указатель: rx теперь ссылается на y, x не изменился
rx = 100;   // а вот здесь срабатывает operator int&() -> присваивание ЧЕРЕЗ ссылку: y == 100
```

Первое присваивание выбирает неявный копирующий `operator=` (точное совпадение), второе — встроенный `operator=` для `int` после пользовательского преобразования. Это классический источник неожиданностей.

### Зачем это нужно

`reference_wrapper` существует потому, что многие механизмы стандартной библиотеки **копируют (decay-copy) свои аргументы**: `std::bind`, `std::thread`, `std::async`, `std::make_tuple`, `std::function`. Если передать туда `T`, получится копия; если передать `reference_wrapper<T>`, копируется лишь указатель, а semantics остаётся ссылочной. Именно для этого нужны фабрики `std::ref` / `std::cref`.

---

## 2. `std::ref` и `std::cref`

Это **фабричные функции вывода типа**, создающие `reference_wrapper`:

```cpp
template<class T> reference_wrapper<T>       ref(T& t) noexcept;              // C++11
template<class T> reference_wrapper<const T> cref(const T& t) noexcept;       // C++11
template<class T> void ref(const T&&) = delete;                              // запрет rvalue
template<class T> void cref(const T&&) = delete;
// плюс перегрузки для уже готового reference_wrapper<T> (C++20 — constexpr)
```

Смысл:

- `ref(x)` → `reference_wrapper<T>`; `cref(x)` → `reference_wrapper<const T>`.
- Удалённые перегрузки для `const T&&` дают **ошибку компиляции** на `std::ref(42)` — нельзя привязать ссылку к временному объекту (защита от висячей ссылки).
- Начиная с C++20 `ref`/`cref` — `constexpr`.

```cpp
void inc(int& x) { ++x; }

int a = 0;
auto f = std::bind(inc, std::ref(a));   // без ref — ошибка компиляции
f();                                     // a == 1

const std::string s = "hi";
auto cs = std::cref(s);                  // reference_wrapper<const std::string>
```

**Канонический пример — `std::thread`:**

```cpp
void process(BigObject& obj);

BigObject obj;
std::thread t(process, std::ref(obj));   // без std::ref не скомпилируется:
                                         // поток копирует аргументы, а BigObject&
                                         // не привязывается к rvalue-копии
t.join();
```

То же самое с `std::async`, `std::bind`, `std::make_tuple` (для кортежа ссылок, впрочем, обычно берут `std::tie`).

---

## 3. `std::function`

```cpp
template< class >
class function;                       // первичный шаблон не определён

template< class R, class... Args >
class function<R(Args...)>;           // единственная специализация
```

**Полиморфная обёртка с стиранием типа**: может хранить любой вызываемый объект, который можно вызвать с `Args...` и результат которого приводим к `R`. Это «указатель на функцию на стероидах»: копируемый, присваиваемый, с состоянием.

### Основные свойства

```cpp
#include <functional>
#include <iostream>

int add(int a, int b) { return a + b; }

struct Mul {
    int k;
    int operator()(int x) const { return x * k; }
};

int main() {
    std::function<int(int,int)> f;         // пустая
    std::cout << std::boolalpha << bool(f) << '\n';  // false

    f = add;                               // указатель на функцию
    f = [](int a, int b){ return a + b; }; // лямбда
    f = Mul{3};                            // функциональный объект с состоянием
    f = std::bind(add, std::placeholders::_1, 10);

    f = nullptr;                           // снова пустая
    try {
        f(1, 2);                           // бросает std::bad_function_call
    } catch (const std::bad_function_call& e) {
        std::cout << e.what() << '\n';
    }
}
```

- **Пустое состояние**: `operator bool`, сравнение с `nullptr` (в C++20 специализированные `operator==`/`!=` с `nullptr_t` удалены — используйте `operator bool`). Вызов пустой `function` — не UB, а исключение `std::bad_function_call`.
- **Требования к цели**: `CopyConstructible` и вызываемость. Move-only объекты (например, лямбда, захватившая `std::unique_ptr`) хранить **нельзя** — для этого в C++23 появился `std::move_only_function`.
- **Копирование**: копируется сама цель (глубокая копия состояния), а не ссылка на неё.
- **`operator()` объявлен `const`**, но цель вызывается как не-const lvalue: `mutable`-лямбда внутри `std::function` спокойно вызывается через константную обёртку.
- **`target_type()` / `target<T>()`** — доступ к хранимому объекту по RTTI. Для лямбд (безымянный тип) и результата `bind` (неспецифицированный тип) это практически бесполезно; работает для указателей на функции и известных типов.
- **Стоимость**: type erasure даёт косвенный вызов (через виртуальную таблицу/указатель на функцию), возможную динамическую аллокацию при больших целях и невозможность инлайна через границы TU. Мелкие цели обычно кладутся в SBO-буфер, но стандарт этого не гарантирует. Поэтому `std::function` не стоит тащить в горячие циклы.
- **Сигнатуры несовместимы**: `std::function<int(int)>` и `std::function<double(double)>` — разные, никак не связанные типы. Неявное преобразование между `std::function` разных сигнатур ограничено (совместимые `R`/`Args`).
- С C++17 в сигнатуре допускается `noexcept`: `std::function<int(int) noexcept>`.
- `std::function` **не** `constexpr`.

### Практические паттерны

```cpp
// Рекурсивная лямбда
std::function<int(int)> fact = [&](int n) {
    return n <= 1 ? 1 : n * fact(n - 1);
};

// Хранилище колбэков
class Button {
    std::function<void()> onClick;
public:
    void setOnClick(std::function<void()> cb) { onClick = std::move(cb); }
    void click() { if (onClick) onClick(); }
};
```

Важное замечание про API: параметр `std::function<...>` в функции — это **стирание типа на входе** и потенциальные аллокации. Если функция вызывается в шаблоне и тип ей не нужен, лучше принять `template<class F> void f(F&&)` (или C++20 `std::predicate`-концепт и т.п.), а `std::function` использовать там, где действительно нужно хранить/передавать гетерогенные вызываемые объекты.

### Альтернативы

- `std::move_only_function` (C++23) — для move-only целей.
- `std::copyable_function` и `std::function_ref` (C++26) — невладеющая ссылка на вызываемый объект без аллокаций.

---

## 4. `std::bind`

```cpp
template< class F, class... Args >
/* unspecified */ bind( F&& f, Args&&... args );
```

Создаёт вызываемый объект — **частичное применение** `f`: часть аргументов фиксируется, часть остаётся «дырками», заполняемыми при вызове. Плейсхолдеры живут в `std::placeholders`:

```cpp
using namespace std::placeholders;

void draw(int x, int y, const std::string& color);

auto red    = std::bind(draw, _1, _2, "red");      // draw(x, y, "red")
auto at1020 = std::bind(draw, 10, 20, _1);         // draw(10, 20, color)
auto flip   = std::bind(draw, _2, _1, "blue");     // перестановка аргументов

red(1, 2);            // draw(1, 2, "red")
at1020("green");      // draw(10, 20, "green")
```

### Семантика аргументов

- Все связанные аргументы **decay-копируются** в объект результата. Отсюда — необходимость `std::ref` для ссылочной семантики.
- `reference_wrapper` в списке аргументов **разворачивается в `T&`** в момент вызова.
- Если связанный аргумент сам является bind-выражением (`std::is_bind_expression<T>::value == true`), то при вызове он **вызывается** с аргументами внешнего вызова — так делается вложенный bind.
- Плейсхолдер `_N` подставляет `N`-й аргумент вызова; один и тот же плейсхолдер можно использовать несколько раз (аргумент будет вычислен/передан несколько раз).
- Число поддерживаемых плейсхолдеров определяется реализацией (обычно `_1`…`_9`, в некоторых реализациях больше).

```cpp
struct Widget {
    int value = 42;
    void show(int k) const { std::cout << value * k << '\n'; }
};

Widget w;

// 1. Указатель на функцию-член: объект надо передать явно
auto f1 = std::bind(&Widget::show, &w, _1);
f1(2);                                   // w.show(2)

// 2. Объект по значению (копия!)
auto f2 = std::bind(&Widget::show, w, 5);
f2();                                    // копия w

// 3. Объект по ссылке
auto f3 = std::bind(&Widget::show, std::ref(w), 5);
f3();

// 4. Указатель на данные-член
auto getv = std::bind(&Widget::value, _1);
int& v = getv(w);                        // ссылка на w.value

// 5. Вложенный bind: (x * 2) + y
auto h = std::bind(std::plus<int>(),
                   std::bind(std::multiplies<int>(), _1, 2),
                   _2);
h(3, 4);                                 // 10
```

### Проблемы и ограничения `bind`

1. **Неочевидная ссылочная семантика.** `std::bind(f, local)` копирует `local`; если нужна ссылка — только `std::ref`. Если `local` умрёт раньше результата `bind` — висячая ссылка.
2. **Висячие ссылки с `std::ref`.** `auto f = std::bind(g, std::ref(tmp));` — если `tmp` локальный и `f` переживёт его, UB.
3. **Вложенные bind-выражения вычисляются «жадно».** Если вы хотели передать bind-объект *как значение*, приходится оборачивать его в лямбду.
4. **Перегрузки и шаблоны.** `std::bind(f, ...)` не умеет выбирать перегрузку — нужен явный `static_cast` к типу функции.
5. **Ужасные сообщения об ошибках** и неочевидные типы результата (неспецифицированный тип).
6. **Нет `constexpr`**, нет `noexcept`-проброса, результат неудобно инспектировать (`target_type` бесполезен).
7. **Не работает с move-only аргументами** так, как ожидается (копирование).
8. Плейсхолдеры легко «потерять» при вложенности и `using namespace` в заголовках.

### Чем заменить

- **Лямбды** — почти всегда лучше: явные типы, явные захваты, читаемые ошибки, `constexpr` в C++17.
  ```cpp
  auto red = [](int x, int y){ draw(x, y, "red"); };   // вместо bind
  ```
- `std::bind_front` (C++20) — фиксирует **первые** аргументы, без плейсхолдеров:
  ```cpp
  auto at1020 = std::bind_front(draw, 10, 20);   // draw(10, 20, color)
  ```
- `std::bind_back` (C++23) — фиксирует последние аргументы.
- `std::mem_fn` — если нужен только указатель на член.

---

## 5. `std::mem_fn`

```cpp
template< class M, class T >
/* unspecified */ mem_fn( M T::* pm ) noexcept;        // C++11
// constexpr с C++20
```

Создаёт **вызываемую обёртку над указателем на член** (функцию или данные). В отличие от `bind`, объект передаётся не при создании, а **при вызове**, и обёртка сама разбирается, что ей дали: объект, ссылку, сырой указатель, `std::reference_wrapper`, `std::unique_ptr`, `std::shared_ptr` — по правилам INVOKE.

```cpp
struct Widget {
    int value = 42;
    void show(int k) const { std::cout << value * k << '\n'; }
};

Widget w;
auto show = std::mem_fn(&Widget::show);

show(w, 3);                                   // (w.*pm)(3)
show(&w, 3);                                  // ((*p).*pm)(3)
show(std::ref(w), 3);                         // reference_wrapper
show(std::make_shared<Widget>(), 3);          // shared_ptr
show(std::make_unique<Widget>(), 3);          // unique_ptr

auto getv = std::mem_fn(&Widget::value);
int& r = getv(w);                             // указатель на данные-член -> ссылка
```

Свойства:

- Работает с **любым** указателем на член, включая данные-члены.
- Не требует плейсхолдеров и не переупорядочивает аргументы: первый аргумент вызова — всегда объект.
- Не владеет объектом и не копирует его.
- С C++20 — `constexpr`. В C++11/14/17 не был `constexpr` и не был SFINAE-friendly (ошибки «в глубине» шаблона).
- Вызов через нулевой указатель на член — UB; `mem_fn` сам по себе нулевой указатель не проверяет.
- **Не** умеет работать с «голым» указателем на функцию — только с указателями на члены.

Смысл появления: заменить старые `std::mem_fun` / `std::mem_fun_ref` (удалены в C++17) и позволить, например, `std::transform` по контейнеру указателей:

```cpp
std::vector<Widget*> v{ new Widget{1}, new Widget{2} };
std::for_each(v.begin(), v.end(), std::mem_fn(&Widget::show));
// ... и не забыть delete
```

В современном коде `mem_fn` почти полностью вытеснен лямбдами и `std::invoke`:

```cpp
auto show = [](const auto& obj, int k){ obj.show(k); };
// или
std::invoke(&Widget::show, w, 3);
```

---

## 6. Как они сочетаются

Классическая связка «`function` + `bind` + `ref`»:

```cpp
class Logger {
public:
    void log(const std::string& msg) { /* ... */ }
};

Logger logger;
std::function<void(const std::string&)> sink =
    std::bind(&Logger::log, std::ref(logger), std::placeholders::_1);

sink("hello");   // logger.log("hello"), без копии logger
```

`function` + `mem_fn`:

```cpp
std::function<void(Widget&, int)> f = std::mem_fn(&Widget::show);
f(w, 3);
```

`vector<function>` как таблица обработчиков:

```cpp
std::vector<std::function<int(int)>> pipeline;
pipeline.push_back([](int x){ return x + 1; });
pipeline.push_back(std::bind(std::multiplies<int>(), std::placeholders::_1, 2));

int v = 5;
for (auto& step : pipeline) v = step(v);   // (5+1)*2 = 12
```

`ref` + контейнер ссылок (без владения):

```cpp
std::vector<std::reference_wrapper<const std::string>> view;
for (const auto& s : storage) view.push_back(std::cref(s));
```

---

## 7. Сводная таблица

| | `reference_wrapper<T>` | `ref` / `cref` | `function<R(Args...)>` | `bind` | `mem_fn` |
|---|---|---|---|---|---|
| Что это | класс-обёртка ссылки | фабрики `reference_wrapper` | type-erased обёртка | адаптер частичного применения | адаптер указателя на член |
| Владеет объектом | нет | нет | **да** (хранит копию) | **да** (копии связанных аргументов) | нет |
| Копируемость | да, тривиальная | — | да (цель должна быть CopyConstructible) | да, если копируемы аргументы | да |
| Ссылочная семантика | по определению | по определению | через `ref` / захват по ссылке | через `std::ref` | по определению (объект приходит при вызове) |
| `constexpr` | с C++20 | с C++20 | нет | нет | с C++20 |
| Типичная цена | 1 указатель | 1 указатель | косвенный вызов, возможная аллокация | копии + косвенный вызов | косвенный вызов |
| Главный риск | «перепривязка» через `operator=`; висячая ссылка | висячая ссылка на умерший объект | копия тяжёлого состояния; пустой вызов | висячие `ref`, неочевидные копии, плохие ошибки | нулевой указатель на член, «сырой» указатель на функцию |
| Современная замена | без изменений (нужен для thread/async) | без изменений | `move_only_function`, `function_ref` | лямбды, `bind_front`/`bind_back` | лямбды, `std::invoke` |

Исторический контекст: `std::bind1st`, `std::bind2nd`, `std::mem_fun`, `std::mem_fun_ref`, `std::ptr_fun`, `std::unary_function`, `std::binary_function` объявлены устаревшими в C++11 и **удалены в C++17**. `std::result_of` удалён в C++20 в пользу `std::invoke_result`.

---

## 8. Типичные ошибки

**1. Забыли `std::ref` — получили копию.**

```cpp
void modify(std::vector<int>& v);
std::vector<int> data;
auto f = std::bind(modify, data);        // копия! изменения не видны снаружи
auto g = std::bind(modify, std::ref(data));  // правильно
```

**2. `std::ref` на временный объект — не компилируется (и это хорошо).**

```cpp
std::bind(f, std::ref(std::string("tmp")));   // ошибка: deleted overload
```

**3. Висячая ссылка.**

```cpp
std::function<void()> make() {
    int local = 42;
    return std::bind([](int x){ /*...*/ }, std::ref(local));
    // local умирает -> UB при вызове
}
```
Лечится захватом по значению.

**4. Рекурсивная `std::function` и висячий захват.**

```cpp
std::function<int(int)> f;
f = [&](int n){ return n ? n * f(n - 1) : 1; };  // f должна пережить лямбду
```
Если `f` — локальная и лямбда «утекает» наружу, получаем висячую ссылку на саму `f`.

**5. `std::function` в горячем цикле.** Лишний косвенный вызов и возможная аллокация; для шаблонного кода используйте `template<class F>`.

**6. Move-only цель в `std::function`.**

```cpp
auto p = std::make_unique<int>(1);
std::function<void()> f = [p = std::move(p)]{};  // ошибка: цель не CopyConstructible
// нужно: std::move_only_function<void()> (C++23) или shared_ptr
```

**7. `bind` с перегруженной функцией.**

```cpp
void g(int); void g(double);
auto f = std::bind(g, 1);                               // неоднозначно
auto ok = std::bind(static_cast<void(*)(int)>(g), 1);   // нужен каст
```

**8. `mem_fn` и нулевой указатель на член.** `std::mem_fn(nullptr)` компилируется не всегда, а вызов через нулевой `pm` — UB; `mem_fn` не проверяет ничего.

**9. `std::ref` и перегрузка `operator&`.** Именно поэтому `reference_wrapper` внутри использует `std::addressof`; свой код с `&obj` может вести себя иначе.

**10. Присваивание `reference_wrapper`.** `rw1 = rw2` перепривязывает `rw1`, а `rw1 = value` пишет через ссылку — см. раздел 1.

---

## 9. Что использовать сегодня

- **Хотите передать по ссылке в `std::thread` / `std::async` / `std::bind`** → `std::ref` / `std::cref`. Это их главная оставшаяся ниша.
- **Нужно хранить разнородные колбэки** → `std::function` (или `std::move_only_function` в C++23, если цель move-only). На границах API, в горячем коде — шаблонный параметр `F&&`.
- **Нужно частичное применение** → лямбда или `std::bind_front` / `std::bind_back`. `std::bind` — только для legacy-кода и случаев с плейсхолдерами и перестановкой аргументов.
- **Нужно вызвать указатель на член** → лямбда или `std::invoke`; `std::mem_fn` — когда нужен именно готовый вызываемый объект, принимающий объект как первый аргумент.
- **Нужен контейнер ссылок без владения** → `std::vector<std::reference_wrapper<T>>` + `std::ref`/`std::cref`.

Правило большого пальца: **`std::ref`/`std::cref` — всё ещё нужны; `std::function` — нужен на границах; `std::bind` и `std::mem_fn` — почти всегда заменяются лямбдами и `std::invoke`.**

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