# 10. Как использовать variadic templates?

**Variadic templates** (вариативные шаблоны) — механизм C++11, позволяющий шаблону принимать произвольное число аргументов любого типа. Они заменили небезопасные `va_list`/`...` из C: всё проверяется на этапе компиляции, разворачивается в обычный код и не стоит ничего в рантайме. На них построены `std::tuple`, `std::function`, `std::thread`, `emplace_back`, `std::make_shared`, `std::visit` и почти весь современный API.

## 1. Базовый синтаксис: пакеты параметров

```cpp
template <typename... Ts>        // Ts — пакет параметров шаблона (template parameter pack)
void f(Ts... args);              // args — пакет параметров функции (function parameter pack)

template <typename... Ts>
struct TypeList {
    static constexpr std::size_t size = sizeof...(Ts);  // количество элементов в пакете
};

TypeList<>            empty;     // пакет пуст — это допустимо
TypeList<int, double> two;       // Ts = {int, double}
```

Ключевые моменты:

- `typename... Ts` (или `class... Ts`) объявляет пакет **типов**; `Ts... args` — пакет **значений**.
- В C++17 появились пакеты для шаблонов с нетиповыми параметрами: `template <int... Ns>`.
- `sizeof...(Ts)` и `sizeof...(args)` дают число элементов пакета **на этапе компиляции** (это не `sizeof` в байтах).
- Пакет нельзя «прочитать» в рантайме как контейнер: у него нет `size()`, по нему нельзя пройти циклом. Его можно только **раскрыть** (pack expansion) или свернуть (fold expression).

## 2. Классический приём: рекурсивное раскрытие

До C++17 основной способ «обойти» пакет — рекурсия с уменьшением пакета на один элемент:

```cpp
#include <iostream>

void print() { std::cout << '\n'; }   // база рекурсии: вызывается для пустого пакета

template <typename T, typename... Rest>
void print(const T& first, const Rest&... rest) {
    std::cout << first;
    if constexpr (sizeof...(rest) > 0) {   // C++17: ветка не инстанцируется, если пакет пуст
        std::cout << ", ";
        print(rest...);                     // раскрытие: rest... -> rest1, rest2, ...
    }
}

print(1, 2.5, "три", std::string("четыре"));
```

Здесь `print(rest...)` — это и есть раскрытие пакета: компилятор подставляет элементы через запятую. Каждый рекурсивный вызов создаёт новую инстанциацию с пакетом на один элемент короче.

До C++17 вместо `if constexpr` приходилось делать две перегрузки (базовую и рекурсивную) или использовать трюк с «фиктивным» массивом:

```cpp
template <typename... Ts>
void print_all(const Ts&... xs) {
    int dummy[] = { 0, (std::cout << xs << ' ', 0)... };  // раскрытие в braced-init-list
    (void)dummy;
}
```

## 3. Свёртки (fold expressions, C++17)

Свёртки позволяют обойтись без рекурсии — компилятор сам строит цепочку операторов:

```cpp
template <typename... Ts>
auto sum(const Ts&... xs) { return (xs + ...); }        // унарная правая: x1 + (x2 + (x3 + ...))
// эквивалент: x1 + (x2 + (x3 + x4))

template <typename... Ts>
auto sum_left(const Ts&... xs) { return (... + xs); }   // унарная левая: ((x1 + x2) + x3) + ...

template <typename... Ts>
auto sum_safe(const Ts&... xs) { return (0 + ... + xs); } // бинарная: начальное значение 0

template <typename... Ts>
bool all_positive(const Ts&... xs) { return ((xs > 0) && ...); }

template <typename... Fs>
void call_all(Fs&&... fs) { (fs(), ...); }               // свёртка по запятой: вызов по очереди
```

Правила для **пустого пакета**:

| оператор | результат при пустом пакете |
|---|---|
| `&&` | `true` |
| `\|\|` | `false` |
| `,` | `void()` |
| `+`, `*`, `-`, … | ошибка компиляции |

Поэтому для «суммы» пустого пакета нужна бинарная свёртка с начальным значением: `(0 + ... + xs)`. Заметьте, что `(0 + ... + xs)` для `double` даст `double`, а для пустого пакета — `int 0`; для строк или пользовательских типов такое начало не подойдёт — тогда используют `std::common_type_t<Ts...>` или перегрузку.

Разница между левой и правой свёрткой важна для неассоциативных операций: `(a - b - c)` и `a - (b - c)` — разные вещи.

## 4. Идеальная передача: `std::forward<Args>(args)...`

Самый частый практический шаблон — проброс пакета аргументов в конструктор:

```cpp
template <typename T, typename... Args>
std::unique_ptr<T> my_make_unique(Args&&... args) {      // && у пакета = forwarding references
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

auto p = my_make_unique<std::string>(5, 'x');            // std::string(5, 'x')
```

`std::forward<Args>(args)...` — раскрытие, в котором **каждый** элемент получает свою идеальную передачу: lvalue остаётся lvalue, rvalue — rvalue. Без `std::forward` вы потеряете move-семантику (внутри функции у `args` есть имя, значит это lvalue).

Тот же приём в `emplace_back`:

```cpp
template <typename... Args>
reference emplace_back(Args&&... args) {
    ::new (static_cast<void*>(m_end)) T(std::forward<Args>(args)...);
    return *(m_end++);
}
```

Это и есть причина, по которой `v.emplace_back(a, b, c)` конструирует объект на месте, без промежуточного временного объекта.

## 5. Вариативные классы шаблонов

Пакет можно использовать в списке базовых классов, в полях и в списке инициализации:

```cpp
// Компиляторный «список типов»
template <typename... Ts>
struct TypeList { static constexpr std::size_t size = sizeof...(Ts); };

// Множественное наследование от всех переданных классов
template <typename... Mixins>
struct Combined : Mixins... {
    Combined(Mixins... m) : Mixins(std::move(m))... {}   // раскрытие в mem-initializer-list
};

// Проверка «входит ли тип в пакет» — целиком на этапе компиляции
template <typename T, typename... Ts>
inline constexpr bool contains_v = (std::is_same_v<T, Ts> || ...);

static_assert(contains_v<int, double, int, char>);
static_assert(!contains_v<float, double, int, char>);
```

Классический идиом «overloaded» для `std::variant` (C++17):

```cpp
template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;                 // using-declaration с раскрытием пакета (C++17)
};
template <typename... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;      // deduction guide (в C++20 часто не нужен)

std::variant<int, std::string> v = 42;
std::visit(Overloaded{
    [](int i)         { std::cout << "int " << i; },
    [](const std::string& s) { std::cout << "str " << s; }
}, v);
```

## 6. Где ещё разрешено раскрытие пакета

Раскрытие `pattern...` допустимо в ограниченном наборе контекстов; полезно помнить основные:

- список аргументов функции — `f(args...);`
- список аргументов шаблона — `std::tuple<Ts...>`, `std::common_type_t<Ts...>`;
- braced-init-list — `{make(args)...}`;
- список базовых классов — `struct X : Bases...`;
- список инициализации членов — `X() : Bases(args)... {}`;
- лямбда-захват — `[args...]`, `[&args...]`, в C++20 — `[args = std::forward<Args>(args)...]`;
- `sizeof...(pack)` и fold expressions;
- `alignas`, атрибуты, `using`-объявления.

Там, где раскрытие syntactically невозможно (например, «взять i-й элемент»), применяют `std::tuple` + `std::apply`/`std::index_sequence`:

```cpp
template <typename... Ts>
void call_with_tuple(F&& f, std::tuple<Ts...>& t) {
    std::apply(f, t);          // f(std::get<0>(t), std::get<1>(t), ...)
}
```

## 7. C++20 и дальше

```cpp
// Ограничение на каждый элемент пакета
template <std::integral... Ts>
auto sum(const Ts&... xs) { return (xs + ...); }

// requires-выражение на пакет
template <typename... Ts>
    requires (sizeof...(Ts) > 0)
void non_empty(Ts&&...);

// Дженерик-лямбда с пакетом
auto f = []<typename... Ts>(Ts&&... xs) {
    return (std::forward<Ts>(xs) + ...);
};
```

В C++26 появилась **индексация пакетов**: `Ts...[0]`, `args...[I]` — наконец можно обратиться к элементу пакета по индексу без `tuple`-хаков (поддержка зависит от компилятора).

## 8. Типичные ошибки и подводные камни

1. **Нет базы рекурсии.** Рекурсивный `print(rest...)` для пустого пакета вызовет `print()`, и без перегрузки `void print()` будет ошибка «no matching function».
2. **Забыли `std::forward`.** `new T(args...)` скопирует аргументы вместо перемещения; для move-only типов вообще не скомпилируется.
3. **Пустой пакет в унарной свёртке.** `(xs + ...)` для нуля аргументов — ошибка; используйте `(0 + ... + xs)`.
4. **Глубина рекурсии.** У рекурсивных вариативных шаблонов есть лимит инстанцирования (обычно ~900–1024). Свёртки и `if constexpr` позволяют его обойти, но по-настоящему большие пакеты всё равно упираются в лимит.
5. **Порядок вычисления аргументов не определён.** Раскрытие `f(g(args)...)` не гарантирует порядок вызовов `g`; если он важен — свёртка по запятой `(g(args), ...)`.
6. **Слишком «жадная» перегрузка.** `template <typename... Ts> void f(Ts...)` может перехватывать вызовы неожиданным образом; лечится концептами/`requires` или `enable_if`.
7. **Пакет не последний в списке параметров функции.** Для функции параметры после пакета должны выводиться из списка аргументов или иметь значение по умолчанию; на практике пакет всегда ставят последним (у классов шаблонов такого ограничения нет).
8. **Шаблонная функция не может быть `virtual`.** Вариативный метод сделать виртуальным нельзя.
9. **Хранение пакета.** Пакет нельзя сохранить «как есть» в поле — только превратить в `std::tuple<Ts...>`, `std::variant` или разложить по полям через наследование.
10. **`sizeof...(args)` — это не рантайм-размер.** Для количества фактически переданных значений он подходит, но не путайте с `sizeof` типа.

## 9. Шпаргалка: задача → инструмент

| Задача | Инструмент |
|---|---|
| Принять любое число аргументов | `template <typename... Ts> f(Ts&&... args)` |
| Узнать их количество | `sizeof...(args)` |
| Пробросить в конструктор | `std::forward<Ts>(args)...` |
| Применить операцию ко всем | свёртка `(op ...)` |
| Выполнить действие для каждого | свёртка по запятой `(f(args), ...)` |
| Разная логика для разных типов | рекурсия + `if constexpr` |
| Обойти пакет по индексу | `std::tuple` + `std::apply` / `index_sequence` (или `Ts...[i]` в C++26) |
| Хранить набор типов | `std::tuple<Ts...>` / `TypeList<Ts...>` |
| Проверить наличие типа | `(std::is_same_v<T, Ts> \|\| ...)` |

**Итог:** вариативные шаблоны — это способ описать функцию или класс с произвольным числом типизированных аргументов. Работа с пакетом сводится к трём приёмам: рекурсивному уменьшению пакета (C++11), свёрткам (C++17) и раскрытию в одном из разрешённых контекстов (`std::forward<Args>(args)...`, списки баз, захваты лямбд). В прикладном коде чаще всего встречается связка «`Args&&... args` + `std::forward<Args>(args)...`» — это универсальный способ пробросить аргументы куда угодно без потери типов и move-семантики.