# 181. Найдите такие элементы двух массивов, которые попадаются только в каждом из них. Желательно использовать STL.

## Постановка задачи

Нужно найти элементы, которые присутствуют **только в первом массиве** и **только во втором массиве** — то есть симметрическую разность множеств (элементы, встречающиеся ровно в одном из массивов). Формулировка «только в каждом из них» означает: элементы, уникальные для каждого массива (нет во втором / нет в первом).

## Идея решения через STL

Удобнее всего работать с `std::set` (или `std::unordered_set`):

1. Загрузить оба массива в множества (дубликаты автоматически отбрасываются).
2. Пройти по первому множеству и вывести те элементы, которых нет во втором — это «только в первом».
3. Пройти по второму множеству и вывести те, которых нет в первом — «только во втором».

Альтернативно можно использовать готовый алгоритм `std::set_symmetric_difference`, который сразу даёт объединение обоих результатов.

## Вариант 1. Ручной обход множеств (наглядный)

```cpp
#include <iostream>
#include <vector>
#include <set>
#include <algorithm>

int main() {
    std::vector<int> a = {1, 2, 2, 3, 4, 5, 7};
    std::vector<int> b = {2, 3, 4, 6, 7, 8, 8};

    // Дубликаты отбрасываются автоматически
    std::set<int> sa(a.begin(), a.end());
    std::set<int> sb(b.begin(), b.end());

    std::cout << "Только в первом массиве: ";
    for (int x : sa)
        if (sb.find(x) == sb.end())
            std::cout << x << ' ';
    std::cout << '\n';

    std::cout << "Только во втором массиве: ";
    for (int x : sb)
        if (sa.find(x) == sa.end())
            std::cout << x << ' ';
    std::cout << '\n';

    return 0;
}
```

Вывод:
```
Только в первом массиве: 1 5
Только во втором массиве: 6 8
```

Сложность: `O((n + m) log(n + m))` из-за построения `std::set` и `find` за `O(log n)`.

## Вариант 2. Через `std::unordered_set` (быстрее в среднем)

```cpp
#include <iostream>
#include <vector>
#include <unordered_set>

int main() {
    std::vector<int> a = {1, 2, 2, 3, 4, 5, 7};
    std::vector<int> b = {2, 3, 4, 6, 7, 8, 8};

    std::unordered_set<int> sa(a.begin(), a.end());
    std::unordered_set<int> sb(b.begin(), b.end());

    std::cout << "Только в первом: ";
    for (int x : sa) if (!sb.count(x)) std::cout << x << ' ';
    std::cout << "\nТолько во втором: ";
    for (int x : sb) if (!sa.count(x)) std::cout << x << ' ';
    std::cout << '\n';
}
```

Средняя сложность — `O(n + m)`, но порядок вывода не определён.

## Вариант 3. Готовый алгоритм `std::set_symmetric_difference`

Если массивы предварительно отсортированы (или сложены в `std::set`), можно применить стандартный алгоритм, который сразу выдаёт симметрическую разность:

```cpp
#include <iostream>
#include <vector>
#include <set>
#include <algorithm>
#include <iterator>

int main() {
    std::vector<int> a = {1, 2, 2, 3, 4, 5, 7};
    std::vector<int> b = {2, 3, 4, 6, 7, 8, 8};

    std::set<int> sa(a.begin(), a.end());
    std::set<int> sb(b.begin(), b.end());

    std::vector<int> diff;
    std::set_symmetric_difference(
        sa.begin(), sa.end(),
        sb.begin(), sb.end(),
        std::back_inserter(diff));

    std::cout << "Симметрическая разность: ";
    for (int x : diff) std::cout << x << ' ';
    std::cout << '\n';  // 1 5 6 8
}
```

Здесь результат — `{1, 5, 6, 8}`. Если же нужно разделить на две группы (только в первом / только во втором), используйте `std::set_difference` дважды:

```cpp
std::vector<int> onlyA, onlyB;
std::set_difference(sa.begin(), sa.end(), sb.begin(), sb.end(),
                    std::back_inserter(onlyA));
std::set_difference(sb.begin(), sb.end(), sa.begin(), sa.end(),
                    std::back_inserter(onlyB));
```

## Важные нюансы

- **Дубликаты.** Формулировка «элементы, которые попадаются» неоднозначна: если элемент встречается в первом массиве дважды, а во втором — ни разу, он всё равно «уникален для первого». Множества (`set`/`unordered_set`) естественно решают эту проблему, оставляя по одному экземпляру.
- **Сортировка.** `std::set_symmetric_difference` и `std::set_difference` требуют **отсортированных** входных диапазонов — `std::set` это гарантирует.
- **Выбор контейнера.** `std::set` даёт упорядоченный вывод и `O(log n)` на операцию; `std::unordered_set` — быстрее в среднем, но без порядка.
- **Тип элементов.** Подход работает для любых типов, поддерживающих сравнение (`operator<` для `set`/алгоритмов) или хеширование (`std::hash` для `unordered_set`).