# 40. Какие улучшения получил std::search в С++17?

## Коротко

В C++17 `std::search` не поменял своего «классического» поведения, но получил три библиотечных расширения:

1. **Новую перегрузку, принимающую объект-искатель (`Searcher`)** — алгоритм поиска стал параметром шаблона (pat-  tern «стратегия»).
2. **Набор стандартных искателей** в `<functional>`: `std::default_searcher`, `std::boyer_moore_searcher`, `std::boyer_moore_horspool_searcher` — то есть в стандартную библиотеку наконец вошли алгоритм Бойера–Мура и его упрощённый вариант.
3. **Перегрузки с `ExecutionPolicy`** — вместе с остальными параллельными алгоритмами (P0024R2).

Идейно это предложение N3905 «Extending `std::search` to use additional searching algorithms» (Marshall Clow).

---

## 1. Перегрузка с искателем

```cpp
template<class ForwardIt, class Searcher>
ForwardIt search(ForwardIt first, ForwardIt last, const Searcher& searcher);
```

Семантика ровно такая:

```cpp
return searcher(first, last).first;
```

Искатель — это функциональный объект, который вызывается с парой итераторов и возвращает `std::pair<It, It>` — **полуинтервал `[match_begin, match_end)`** первого вхождения. Если совпадения нет, возвращается `{last, last}`, и `std::search` соответственно вернёт `last`.

Ключевые свойства:

- искатель принимается **по `const`-ссылке**, копия не делается; стандартные искатели при этом `CopyConstructible`;
- можно передавать собственные искатели (КМП, Рабин–Карп и т.п.) — `std::search` превратился в точку расширения;
- `std::search` использует только `.first`, но при прямом вызове искателя доступен и конец совпадения;
- перегрузка не конфликтует с классической 4-аргументной: различается арность.

Формально от `Searcher` требуется, чтобы выражение `s(first, last)` было корректно и возвращало `std::pair<It, It>`; требования к итераторам определяются конкретным искателем.

---

## 2. Стандартные искатели

Все они объявлены в `<functional>` (не в `<algorithm>` — это частая ошибка).

### `std::default_searcher`

```cpp
template<class ForwardIt1, class BinaryPredicate = std::equal_to<>>
class default_searcher {
public:
    default_searcher(ForwardIt1 pat_first, ForwardIt1 pat_last,
                     BinaryPredicate pred = BinaryPredicate());

    template<class ForwardIt2>
    std::pair<ForwardIt2, ForwardIt2>
    operator()(ForwardIt2 first, ForwardIt2 last) const;
};
```

- Реализует классический наивный поиск (по сути тот же `std::search`), сложность `O(n·m)`.
- Нужен как «эталон», для однородного интерфейса и когда предикат удобнее передать через искатель.
- Работает с forward-итераторами.

### `std::boyer_moore_searcher`

```cpp
template<class RandomIt1,
         class Hash = std::hash<typename std::iterator_traits<RandomIt1>::value_type>,
         class BinaryPredicate = std::equal_to<>>
class boyer_moore_searcher {
public:
    boyer_moore_searcher(RandomIt1 pat_first, RandomIt1 pat_last,
                         Hash hf = Hash(), BinaryPredicate pred = BinaryPredicate());

    template<class RandomIt2>
    std::pair<RandomIt2, RandomIt2>
    operator()(RandomIt2 first, RandomIt2 last) const;
};
```

- Полный алгоритм Бойера–Мура: эвристика **плохого символа** (таблица на основе хеша) + эвристика **хорошего суффикса**.
- Предвычисление таблиц — в конструкторе, `O(m + σ)` времени и памяти (`m` — длина паттерна, `σ` — число различных символов).
- Средняя сложность сублинейная (порядка `O(n/m)` на случайном тексте), худшая — `O(n·m)`.
- Требует, чтобы `Hash` был согласован с `BinaryPredicate`.
- По спецификации и паттерн, и диапазон поиска задаются **random-access итераторами** (`RandomIt1` / `RandomIt2`), так что `std::list` здесь формально не подойдёт.

### `std::boyer_moore_horspool_searcher`

```cpp
template<class RandomIt1,
         class Hash = std::hash<typename std::iterator_traits<RandomIt1>::value_type>,
         class BinaryPredicate = std::equal_to<>>
class boyer_moore_horspool_searcher;
```

- Вариант Бойера–Мура–Хорспула: только эвристика плохого символа.
- Меньше предвычисления и памяти, проще реализация; для коротких и средних паттернов часто быстрее полного БМ.
- Средняя сложность тоже сублинейная, худшая — `O(n·m)`.

---

## 3. Параллельные перегрузки

Вместе с остальными алгоритмами `std::search` получил версии с политикой выполнения:

```cpp
template<class ExecutionPolicy, class ForwardIt1, class ForwardIt2>
ForwardIt1 search(ExecutionPolicy&& policy,
                  ForwardIt1 first, ForwardIt1 last,
                  ForwardIt2 s_first, ForwardIt2 s_last);

template<class ExecutionPolicy, class ForwardIt1, class ForwardIt2, class BinaryPredicate>
ForwardIt1 search(ExecutionPolicy&& policy,
                  ForwardIt1 first, ForwardIt1 last,
                  ForwardIt2 s_first, ForwardIt2 s_last, BinaryPredicate p);
```

Степень реального распараллеливания зависит от реализации. У перегрузки с искателем policy-версии нет.

---

## 4. Примеры

### Базовое использование

```cpp
#include <algorithm>
#include <functional>
#include <iostream>
#include <string>

int main() {
    const std::string text = "The quick brown fox jumps over the lazy dog";
    const std::string pat  = "brown";

    auto searcher = std::boyer_moore_searcher(pat.begin(), pat.end());
    auto it = std::search(text.begin(), text.end(), searcher);

    if (it != text.end())
        std::cout << "found at " << (it - text.begin()) << '\n'; // 10
    else
        std::cout << "not found\n";
}
```

### Повторное использование и границы совпадения

Главный практический выигрыш — предвычисление делается один раз:

```cpp
auto s = std::boyer_moore_horspool_searcher(pat.begin(), pat.end());

for (const auto& line : lines) {
    auto [b, e] = s(line.begin(), line.end());   // C++17 structured bindings
    if (b != line.end()) {
        // совпадение в [b, e)
    }
}
```

### Свой предикат и хеш (например, поиск без учёта регистра)

```cpp
struct ci_equal {
    bool operator()(char a, char b) const noexcept {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    }
};

struct ci_hash {
    std::size_t operator()(char c) const noexcept {
        return std::hash<char>{}(
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
};

auto searcher = std::boyer_moore_searcher(
    pat.begin(), pat.end(), ci_hash{}, ci_equal{});
```

Предикат и хеш обязаны быть согласованы: если `ci_equal(a, b)`, то и `ci_hash(a) == ci_hash(b)`.

### Минимальный собственный искатель

```cpp
struct my_searcher {
    template<class It>
    std::pair<It, It> operator()(It first, It last) const {
        // ... поиск ...
        return {last, last};   // не найдено
    }
};
```

---

## 5. Сравнение

| Искатель | Алгоритм | Итераторы | Предвычисление / память | Средняя сложность | Когда применять |
|---|---|---|---|---|---|
| `default_searcher` | наивный | forward | нет, `O(1)` | `O(n·m)` | эталон, короткие паттерны, единый интерфейс |
| `boyer_moore_searcher` | Бойер–Мур (bad char + good suffix) | random access | `O(m + σ)` | сублинейная, ~`O(n/m)` | длинные паттерны, большой алфавит |
| `boyer_moore_horspool_searcher` | Бойер–Мур–Хорспул (bad char) | random access | `O(m + σ)`, меньше | сублинейная | короткие/средние паттерны, экономия памяти |

---

## 6. Подводные камни

- **Заголовок.** Искатели — в `<functional>`; подключать только `<algorithm>` недостаточно.
- **Время жизни паттерна.** Стандартные искатели сохраняют итераторы на паттерн (БМ — ещё и таблицы), поэтому паттерн должен жить не меньше искателя. Менять паттерн после создания искателя нельзя — таблицы устареют.
- **Предвычисление не бесплатно.** Создавайте искатель один раз и переиспользуйте; создание на каждый вызов может сделать БМ медленнее наивного поиска.
- **БМ не всегда быстрее.** На коротких паттернах и маленьком алфавите (например, ДНК) выигрыш невелик, а накладные расходы на таблицы заметны; `std::string::find`/`memchr` часто быстрее — нужно измерять.
- **Возвращается только начало.** Если нужен конец совпадения, вызывайте искатель напрямую (`auto [b, e] = s(...)`).
- **`constexpr` в C++17 нет.** `std::search` стал `constexpr` только в C++20.

---

## 7. Что было дальше

- **C++20:** `std::search` (включая перегрузку с искателем) стал `constexpr`; появился `std::ranges::search`, возвращающий `subrange`, но параметра-искателя у ranges-версии нет — искатели применяются вручную.
- Сами три искателя с C++17 не менялись и по-прежнему доступны.

**Итог:** C++17 превратил `std::search` из одного жёстко зашитого алгоритма в расширяемый механизм: появилась перегрузка с `Searcher`, стандартные реализации Бойера–Мура и Бойера–Мура–Хорспула с предвычислением, возможность подставлять свои алгоритмы и параллельные версии для классического поиска.