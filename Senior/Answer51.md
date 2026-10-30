# 51. Расскажите о основных алгоритмах поиска.

Алгоритмы поиска в STL — это шаблоны из `<algorithm>` (плюс `<functional>` для «искателей» C++17 и `<ranges>` для диапазонных версий C++20). Общая черта: они принимают полуинтервал `[first, last)`, ничего не знают о конкретном контейнере и возвращают итератор (либо пару итераторов, `bool`, `subrange`). Эффективность зависит от:

- категории итераторов (`InputIterator`, `ForwardIterator`, `RandomAccessIterator`);
- упорядоченности диапазона;
- типа поиска (значение, предикат, подпоследовательность, ключ в контейнере).

## Классификация

| Группа | Алгоритмы |
|---|---|
| Линейный поиск | `find`, `find_if`, `find_if_not`, `find_first_of`, `adjacent_find` |
| Поиск подпоследовательности | `search`, `search_n`, `find_end`, searchers C++17 |
| Бинарный поиск | `binary_search`, `lower_bound`, `upper_bound`, `equal_range`, `partition_point` |
| Поиск экстремума | `min_element`, `max_element`, `minmax_element` |
| Проверка свойств диапазона | `all_of`, `any_of`, `none_of`, `mismatch`, `equal` |
| Подсчёт | `count`, `count_if` |
| Поиск в ассоциативных контейнерах | члены `find`, `lower_bound`, `upper_bound`, `equal_range`, `contains` |

---

## 1. Линейный поиск

Линейный поиск просматривает диапазон слева направо, пока не найдёт совпадение. Работает на **неупорядоченных** данных и на любых итераторах (в том числе `InputIterator` — например, потоковых).

### `std::find`

```cpp
template<class InputIt, class T>
InputIt find(InputIt first, InputIt last, const T& value);
```

- Возвращает итератор на **первый** элемент, равный `value` (сравнение `*it == value`), либо `last`, если элемента нет.
- Сложность: не более `last - first` сравнений, т.е. **O(n)**.
- Требования: `InputIterator`.

```cpp
#include <algorithm>
#include <vector>
#include <iostream>

int main() {
    std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};

    if (auto it = std::find(v.begin(), v.end(), 5); it != v.end())
        std::cout << "5 at index " << (it - v.begin()) << '\n'; // 4
}
```

> Для `std::set`/`std::map` вызывать `std::find` бессмысленно: он даст O(n), тогда как член `find` — O(log n).

### `std::find_if` и `std::find_if_not`

```cpp
template<class InputIt, class UnaryPred>
InputIt find_if(InputIt first, InputIt last, UnaryPred p);

template<class InputIt, class UnaryPred>
InputIt find_if_not(InputIt first, InputIt last, UnaryPred p); // C++11
```

- `find_if` — первый элемент, для которого `p(*it) == true`.
- `find_if_not` — первый элемент, для которого `p(*it) == false`.
- Сложность O(n), не более `last - first` вызовов предиката.

```cpp
auto is_even = [](int x) { return x % 2 == 0; };

auto it  = std::find_if(v.begin(), v.end(), is_even);      // 4
auto it2 = std::find_if_not(v.begin(), v.end(), is_even);  // 3
```

В C++20 аналог: `std::ranges::find(v, 5)`, `std::ranges::find_if(v, is_even)` с возможностью проекции:

```cpp
struct Person { std::string name; int age; };
std::vector<Person> people{{"Alice", 30}, {"Bob", 25}};

auto it = std::ranges::find(people, "Bob", &Person::name);
```

### `std::find_first_of`

```cpp
template<class InputIt, class ForwardIt>
InputIt find_first_of(InputIt first, InputIt last,
                      ForwardIt s_first, ForwardIt s_last);
```

Ищет первый элемент `[first, last)`, равный **любому** элементу из `[s_first, s_last)`. Есть версия с бинарным предикатом.

- Сложность: не более `(last - first) * (s_last - s_first)` сравнений — **O(n·m)**.
- Типичное применение — поиск первого вхождения любого символа из набора.

```cpp
std::string s = "hello, world";
std::string vowels = "aeiou";

auto it = std::find_first_of(s.begin(), s.end(),
                             vowels.begin(), vowels.end()); // 'e', индекс 1
```

### `std::adjacent_find`

```cpp
template<class ForwardIt>
ForwardIt adjacent_find(ForwardIt first, ForwardIt last);
```

Ищет первую пару **соседних** равных элементов (или пару, удовлетворяющую бинарному предикату). Возвращает итератор на первый элемент пары, иначе `last`.

- Требования: `ForwardIterator`.
- Сложность: O(n).

```cpp
std::vector<int> v{1, 2, 2, 3, 4, 4};
auto it = std::adjacent_find(v.begin(), v.end()); // индекс 1
```

Удобен для обнаружения дубликатов в **отсортированном** диапазоне.

### `std::mismatch` и `std::equal`

Формально это не поиск, но близкие алгоритмы сравнения:

- `std::mismatch(first1, last1, first2[, last2][, pred])` — возвращает пару итераторов на первое расхождение (или на концы диапазонов).
- `std::equal(first1, last1, first2[, pred])` — проверяет полное совпадение.

```cpp
std::vector<int> a{1, 2, 3, 4};
std::vector<int> b{1, 2, 9, 4};

auto [ia, ib] = std::mismatch(a.begin(), a.end(), b.begin());
// ia -> 3-й элемент (значение 3), ib -> значение 9
```

---

## 2. Поиск подпоследовательности

### `std::search`

```cpp
template<class ForwardIt1, class ForwardIt2>
ForwardIt1 search(ForwardIt1 first, ForwardIt1 last,
                  ForwardIt2 s_first, ForwardIt2 s_last);
```

Ищет **первое** вхождение подпоследовательности `[s_first, s_last)` в `[first, last)`.

- Возвращает итератор на начало первого вхождения; если вхождения нет — `last`.
- Пустой образец (`s_first == s_last`) «находится» в начале: возвращается `first`.
- Требования: `ForwardIterator` для обоих диапазонов.
- Сложность: не более `(last - first) * (s_last - s_first)` сравнений — **O(n·m)**.

```cpp
std::string text = "to be or not to be, that is the question";
std::string pat  = "to be";

auto it1 = std::search(text.begin(), text.end(), pat.begin(), pat.end()); // 0
auto it2 = std::search(it1 + 1, text.end(), pat.begin(), pat.end());     // 13
```

В C++20: `std::ranges::search` возвращает `subrange` (при отсутствии — `{last, last}`).

### `std::search_n`

```cpp
template<class ForwardIt, class Size, class T>
ForwardIt search_n(ForwardIt first, ForwardIt last, Size count, const T& value);
```

Ищет первый блок из `count` подряд идущих элементов, равных `value` (есть версия с предикатом). Возвращает итератор на начало блока, иначе `last`. При `count <= 0` возвращает `first`.

- Сложность: O(n).

```cpp
std::vector<int> v{1, 0, 0, 0, 2};
auto it = std::search_n(v.begin(), v.end(), 3, 0); // индекс 1
```

### `std::find_end`

```cpp
template<class ForwardIt1, class ForwardIt2>
ForwardIt1 find_end(ForwardIt1 first, ForwardIt1 last,
                    ForwardIt2 s_first, ForwardIt2 s_last);
```

Ищет **последнее** вхождение подпоследовательности (в отличие от `search`, который ищет первое). Возвращает `last`, если вхождения нет; для пустого образца возвращает `last`.

- Сложность: O(n·m).

```cpp
std::string s = "ababab";
std::string p = "ab";

auto it = std::find_end(s.begin(), s.end(), p.begin(), p.end()); // индекс 4
```

### Searchers C++17 (`<functional>`)

Начиная с C++17, `std::search` умеет принимать объект-«искатель» (searcher), инкапсулирующий алгоритм поиска подстроки:

| Searcher | Алгоритм | Требования | Сложность |
|---|---|---|---|
| `std::default_searcher` | наивный, как обычный `search` | `ForwardIterator` | O(n·m) |
| `std::boyer_moore_searcher` | Бойер–Мур (эвристики плохого символа и хорошего суффикса) | `RandomAccessIterator`, элементы должны быть хешируемы | в среднем O(n + m), в худшем O(n·m); память O(m + σ) |
| `std::boyer_moore_horspool_searcher` | Бойер–Мур–Хорспул (только эвристика плохого символа) | `RandomAccessIterator`, хешируемость | в среднем O(n), в худшем O(n·m); память O(σ) |

Здесь `n` — длина текста, `m` — длина образца, `σ` — размер алфавита. По умолчанию хеш берётся из `std::hash<value_type>`; для пользовательских типов можно передать свой хеш и предикат равенства.

```cpp
#include <algorithm>
#include <functional>
#include <string>

std::string text = "The quick brown fox jumps over the lazy dog";
std::string pat  = "the";

auto searcher = std::boyer_moore_searcher(pat.begin(), pat.end());
auto it = std::search(text.begin(), text.end(), searcher);

if (it != text.end())
    ; // найдено, индекс 31
```

Важно:

- Искатель **строится один раз** (предобработка образца) и может переиспользоваться для многих вызовов `search` — это и есть основной выигрыш.
- Для коротких образцов и небольших текстов наивный поиск может оказаться быстрее из-за накладных расходов на предобработку.
- `boyer_moore_searcher` требует именно **RandomAccessIterator** (подходит `std::string`, `std::vector`, массивы; не подходит `std::list`).

---

## 3. Бинарный поиск

Бинарный поиск работает за O(log n) сравнений, но требует, чтобы диапазон был **упорядочен** (точнее — разбит/partitioned) относительно искомого значения. Нарушение этого предусловия — **неопределённое поведение**, а не просто «неверный ответ».

### `std::lower_bound`

```cpp
template<class ForwardIt, class T>
ForwardIt lower_bound(ForwardIt first, ForwardIt last, const T& value);
```

Возвращает итератор на **первый** элемент, для которого `*it >= value` (то есть `!( *it < value )`). Если все элементы меньше `value` — возвращает `last`.

### `std::upper_bound`

Возвращает итератор на **первый** элемент, для которого `*it > value` (то есть `value < *it`).

### `std::equal_range`

Возвращает пару `{lower_bound, upper_bound}` — полуинтервал всех элементов, **эквивалентных** `value`.

### `std::binary_search`

```cpp
template<class ForwardIt, class T>
bool binary_search(ForwardIt first, ForwardIt last, const T& value);
```

Возвращает `true`, если в диапазоне есть элемент, эквивалентный `value`, иначе `false`. Позицию не сообщает — для позиции нужен `lower_bound`.

| Алгоритм | Возвращает | Смысл (для сортировки по возрастанию) |
|---|---|---|
| `lower_bound` | итератор | первый элемент `>= value` |
| `upper_bound` | итератор | первый элемент `> value` |
| `equal_range` | `pair<It, It>` | диапазон `[lower, upper)` всех `== value` |
| `binary_search` | `bool` | есть ли элемент `== value` |

```cpp
#include <algorithm>
#include <vector>

std::vector<int> v{1, 2, 2, 2, 3, 5, 8};

bool has5 = std::binary_search(v.begin(), v.end(), 5);      // true

auto lo = std::lower_bound(v.begin(), v.end(), 2);          // индекс 1
auto hi = std::upper_bound(v.begin(), v.end(), 2);          // индекс 4
auto [f, l] = std::equal_range(v.begin(), v.end(), 2);      // [1, 4)

// Вставка с сохранением порядка:
v.insert(std::upper_bound(v.begin(), v.end(), 2), 2);
```

Ключевые детали:

- **Компаратор.** Версия с компаратором: `lower_bound(first, last, value, comp)` возвращает первый `it`, где `comp(*it, value)` ложно; `upper_bound` — первый `it`, где `comp(value, *it)` истинно. Компаратор должен задавать строгий слабый порядок и быть тем же, которым сортировали диапазон.
- **Категория итераторов.** Формально достаточно `ForwardIterator`, но число сравнений логарифмическое, а число шагов итератора — линейное. Настоящая O(log n) по времени достигается только на `RandomAccessIterator` (`vector`, `deque`, `array`, сырой массив). На `std::list` поиск будет O(n) по проходам.
- **`std::partition_point`** — обобщение: возвращает первый элемент, для которого предикат ложен, если диапазон разбит по этому предикату. Фактически `lower_bound` — это `partition_point` с предикатом `element < value`.

```cpp
auto p = std::partition_point(v.begin(), v.end(),
                              [](int x) { return x < 2; }); // то же, что lower_bound(..., 2)
```

В C++20 есть диапазонные версии с проекциями:

```cpp
auto it = std::ranges::lower_bound(v, 2);
bool has = std::ranges::binary_search(v, 5);
```

---

## 4. Поиск экстремума

```cpp
template<class ForwardIt>
ForwardIt min_element(ForwardIt first, ForwardIt last);

template<class ForwardIt>
ForwardIt max_element(ForwardIt first, ForwardIt last);

template<class ForwardIt>
std::pair<ForwardIt, ForwardIt> minmax_element(ForwardIt first, ForwardIt last); // C++11
```

- `min_element`/`max_element` возвращают итератор на первый минимальный/максимальный элемент (при нескольких равных — на первый).
- `minmax_element` возвращает пару `{min, max}`; при нескольких минимумах — первый, при нескольких максимумах — последний. Сложность около `3n/2` сравнений (выгоднее двух отдельных проходов).
- Требования: `ForwardIterator`.

```cpp
std::vector<int> v{3, 1, 4, 1, 5, 9, 2, 6};

auto mn = std::min_element(v.begin(), v.end());  // значение 1
auto mx = std::max_element(v.begin(), v.end());  // значение 9
auto [a, b] = std::minmax_element(v.begin(), v.end());

// C++20 с проекцией:
// std::ranges::min_element(people, {}, &Person::age);
```

---

## 5. Проверки свойств диапазона и подсчёт

### `all_of`, `any_of`, `none_of`

```cpp
template<class InputIt, class UnaryPred>
bool all_of(InputIt first, InputIt last, UnaryPred p);   // C++11

template<class InputIt, class UnaryPred>
bool any_of(InputIt first, InputIt last, UnaryPred p);   // C++11

template<class InputIt, class UnaryPred>
bool none_of(InputIt first, InputIt last, UnaryPred p);  // C++11
```

Это «поиск с коротким замыканием»: `any_of` останавливается на первом `true`, `all_of` — на первом `false`, `none_of` — на первом `true`. Сложность — не более n вызовов предиката.

```cpp
bool all_positive = std::all_of(v.begin(), v.end(),
                                [](int x) { return x > 0; });
bool has_even     = std::any_of(v.begin(), v.end(),
                                [](int x) { return x % 2 == 0; });
```

### `count` и `count_if`

```cpp
template<class InputIt, class T>
typename iterator_traits<InputIt>::difference_type
count(InputIt first, InputIt last, const T& value);

template<class InputIt, class UnaryPred>
typename iterator_traits<InputIt>::difference_type
count_if(InputIt first, InputIt last, UnaryPred p);
```

Считают количество элементов, равных значению или удовлетворяющих предикату. Сложность O(n).

```cpp
auto n = std::count(v.begin(), v.end(), 1);          // 2
auto m = std::count_if(v.begin(), v.end(),
                       [](int x) { return x > 3; }); // 3
```

---

## 6. Поиск в ассоциативных контейнерах

Для `std::set`, `std::map`, `std::multiset`, `std::multimap` (упорядоченные, обычно красно-чёрное дерево) нужно использовать **члены контейнера**, а не общие алгоритмы:

| Операция | Член контейнера | Сложность |
|---|---|---|
| Поиск ключа | `find(key)` | O(log n) |
| Первый ключ `>= key` | `lower_bound(key)` | O(log n) |
| Первый ключ `> key` | `upper_bound(key)` | O(log n) |
| Диапазон ключей `== key` | `equal_range(key)` | O(log n) |
| Количество ключей | `count(key)` | O(log n) |
| Наличие ключа (C++20) | `contains(key)` | O(log n) |

Для `std::multimap`/`std::multiset` `equal_range` — основной способ перебрать все элементы с одинаковым ключом.

```cpp
std::map<std::string, int> m{{"one", 1}, {"two", 2}};

if (auto it = m.find("two"); it != m.end())
    ; // it->second == 2

if (m.contains("one")) // C++20
    ;
```

**Гетерогенный поиск.** Чтобы `find` не создавал временный `std::string` из `const char*`, используют прозрачный компаратор:

```cpp
std::map<std::string, int, std::less<>> m;
m.find("key"); // без создания временной строки (C++14)
```

Для неупорядоченных контейнеров (`std::unordered_set`, `std::unordered_map`) `find`/`contains` в среднем работают за **O(1)**, в худшем — O(n). Гетерогенный поиск там появился в C++20 и требует прозрачных `hash` и `equal_to`.

**Строки.** У `std::string` есть собственные члены `find`, `rfind`, `find_first_of`, `find_last_of`, `find_first_not_of`, `find_last_not_of`, возвращающие `size_type` или `npos`. Они ищут подстроку и обычно оптимизированы лучше общего `std::find` (который ищет только один символ через `==`). В C++20 добавлены `starts_with`/`ends_with`, в C++23 — `contains`.

```cpp
std::string s = "hello, world";
auto pos = s.find("world"); // 7
if (pos != std::string::npos)
    ;
```

---

## 7. Что нового в C++20/23

- **Ranges-версии** (`std::ranges::find`, `find_if`, `search`, `binary_search`, `lower_bound`, `min_element` и т.д.) принимают диапазон целиком и поддерживают **проекции**, что убирает большинство лямбд:
  ```cpp
  auto it = std::ranges::find_if(people,
                                 [](int a) { return a > 30; },
                                 &Person::age);
  auto youngest = std::ranges::min_element(people, {}, &Person::age);
  ```
- **`std::ranges::contains`** (C++23) — булев поиск:
  ```cpp
  bool has = std::ranges::contains(v, 5);
  ```
- **`std::ranges::find_last`, `find_last_if`, `find_last_if_not`** (C++23) — поиск последнего элемента; возвращают `subrange` (пустой `{last, last}`, если не найдено):
  ```cpp
  auto r = std::ranges::find_last(v, 2);
  if (!r.empty())
      auto idx = r.begin() - v.begin();
  ```
- **`std::ranges::starts_with` / `ends_with`** (C++23) — проверка префикса/суффикса диапазона.
- **`std::string::contains`, `starts_with`, `ends_with`** (C++20/23).

---

## 8. Сводная таблица

| Алгоритм | Что ищет | Требования | Сложность | Возврат |
|---|---|---|---|---|
| `find` | первый `== value` | `InputIterator` | O(n) | итератор / `last` |
| `find_if` | первый с `pred` | `InputIterator` | O(n) | итератор / `last` |
| `find_if_not` | первый с `!pred` | `InputIterator` | O(n) | итератор / `last` |
| `find_first_of` | первый из набора | `InputIterator` + `ForwardIterator` | O(n·m) | итератор / `last` |
| `adjacent_find` | первую пару соседних равных | `ForwardIterator` | O(n) | итератор / `last` |
| `search` | первое вхождение подпоследовательности | `ForwardIterator` | O(n·m) (наивно) | итератор / `last` |
| `search_n` | `count` подряд равных | `ForwardIterator` | O(n) | итератор / `last` |
| `find_end` | последнее вхождение подпоследовательности | `ForwardIterator` | O(n·m) | итератор / `last` |
| `binary_search` | наличие `value` | `ForwardIterator`, упорядочено | O(log n) сравнений | `bool` |
| `lower_bound` | первый `>= value` | `ForwardIterator`, упорядочено | O(log n) сравнений | итератор |
| `upper_bound` | первый `> value` | `ForwardIterator`, упорядочено | O(log n) сравнений | итератор |
| `equal_range` | диапазон `== value` | `ForwardIterator`, упорядочено | O(log n) сравнений | `pair` |
| `partition_point` | границу разбиения | `ForwardIterator`, разбит | O(log n) сравнений | итератор |
| `min_element` / `max_element` | минимум / максимум | `ForwardIterator` | O(n) | итератор |
| `minmax_element` | оба | `ForwardIterator` | ~3n/2 сравнений | `pair` |
| `all_of` / `any_of` / `none_of` | свойство | `InputIterator` | O(n), с коротким замыканием | `bool` |
| `count` / `count_if` | количество | `InputIterator` | O(n) | `difference_type` |

---

## 9. Практические рекомендации и типичные ошибки

1. **Выбор алгоритма.**
   - Неупорядоченный диапазон, нужен факт наличия/позиция — `find`/`find_if`.
   - Упорядоченный диапазон — `binary_search`, а если нужна позиция — `lower_bound`/`equal_range`.
   - Поиск подстроки — `std::string::find` для обычных случаев, `std::search` с `boyer_moore_searcher` для больших текстов/образцов.
   - Ассоциативный контейнер — **членные** `find`/`lower_bound`, а не общие алгоритмы.
2. **Бинарный поиск только на упорядоченном диапазоне.** Передача неупорядоченного диапазона в `binary_search`/`lower_bound` — это нарушение предусловия (UB), а не «неточный результат». Компаратор должен совпадать с тем, которым сортировали.
3. **`std::find` на `std::set`/`std::map` — O(n).** Это классическая ошибка, сводящая на нет преимущество дерева.
4. **`binary_search` не даёт позицию.** Если нужна позиция или границы равных элементов — `lower_bound`/`equal_range`.
5. **Возврат `last` — это «не найдено».** Всегда сравнивайте результат с `end()`; для строк — с `npos`.
6. **Искатель (searcher) строится один раз.** Создание `boyer_moore_searcher` внутри цикла на каждый вызов убивает весь выигрыш.
7. **Пустой образец.** `search` возвращает `first`, `find_end` — `last`, `search_n` при `count <= 0` — `first`. Это стоит помнить в граничных случаях.
8. **Предикаты не должны менять элементы** и должны быть согласованы (для ranges — регулярными); иначе поведение не определено.
9. **Параллельные версии.** C++17 добавляет перегрузки с `std::execution::par`/`par_unseq` для многих алгоритмов, включая поиск. Исключения, вылетевшие из параллельного алгоритма, приводят к `std::terminate`. Ranges-версии до C++26 политик выполнения не имеют.
10. **`std::ranges` vs классические версии.** Ranges удобнее (проекции, целые диапазоны), но возвращают `subrange`/`borrowed_iterator_t`, а не «голый» итератор; проверяйте `empty()`/сравнивайте с `end()` корректно.

## Итог

«Основные» алгоритмы поиска STL делятся на три большие группы: **линейные** (`find`, `find_if`, `find_first_of`, `adjacent_find`, `count`, `all_of`/`any_of`/`none_of`), **поиск подпоследовательности** (`search`, `search_n`, `find_end` и searchers C++17) и **бинарные** (`binary_search`, `lower_bound`, `upper_bound`, `equal_range`, `partition_point`). Отдельно стоят поиск экстремумов (`min_element`, `max_element`, `minmax_element`) и поиск в ассоциативных контейнерах через членные функции. Ключ к правильному выбору — учитывать упорядоченность данных, категорию итераторов и то, что нужно вернуть: факт наличия, позицию, диапазон равных элементов или поддиапазон вхождения.