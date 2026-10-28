# 119. Какие алгоритмы STL применяли? В чём преимущество использования алгоритмов перед собственноручно написанными функциями?

## Какие алгоритмы STL применяются на практике

Алгоритмы STL — это шаблонные функции из заголовка `<algorithm>` (а также `<numeric>`), работающие с диапазонами итераторов. На практике чаще всего используются:

### Немодифицирующие (только читают диапазон)

- `std::find`, `std::find_if`, `std::find_if_not` — поиск элемента/первого элемента по предикату;
- `std::count`, `std::count_if` — подсчёт элементов;
- `std::any_of`, `std::all_of`, `std::none_of` — проверки условий по диапазону;
- `std::for_each` — применение функции к каждому элементу;
- `std::equal`, `std::mismatch` — сравнение диапазонов;
- `std::search` — поиск подпоследовательности.

### Модифицирующие

- `std::copy`, `std::copy_if`, `std::copy_n`, `std::copy_backward`;
- `std::transform` — поэлементное преобразование (в т.ч. в другой контейнер);
- `std::fill`, `std::fill_n`, `std::generate`;
- `std::remove`, `std::remove_if` (в связке с `erase` — идиома erase-remove);
- `std::unique` — удаление подряд идущих дубликатов;
- `std::replace`, `std::replace_if`;
- `std::reverse`, `std::rotate`, `std::shuffle`;
- `std::swap_ranges`, `std::iter_swap`.

### Сортировка и бинарный поиск

- `std::sort`, `std::stable_sort`, `std::partial_sort`, `std::nth_element`;
- `std::lower_bound`, `std::upper_bound`, `std::equal_range`, `std::binary_search`;
- `std::min_element`, `std::max_element`, `std::minmax_element`;
- `std::is_sorted`, `std::is_sorted_until`.

### Работа с множествами (для отсортированных диапазонов)

- `std::set_union`, `std::set_intersection`, `std::set_difference`, `std::set_symmetric_difference`;
- `std::merge`, `std::includes`.

### Числовые (`<numeric>`)

- `std::accumulate`, `std::reduce` (C++17) — свёртка;
- `std::inner_product`;
- `std::partial_sum`, `std::adjacent_difference`;
- `std::iota` — заполнение последовательностью;
- `std::gcd`, `std::lcm` (C++17).

### Параллельные версии (C++17)

Многие алгоритмы имеют перегрузки с политикой выполнения: `std::execution::seq`, `par`, `par_unseq`, например `std::sort(std::execution::par, v.begin(), v.end())`.

### Пример использования

```cpp
#include <algorithm>
#include <numeric>
#include <vector>
#include <string>
#include <iostream>

struct Person {
    std::string name;
    int age;
};

int main() {
    std::vector<Person> people = {
        {"Alice", 30}, {"Bob", 25}, {"Carol", 35}, {"Dave", 25}
    };

    // Сортировка по возрасту, затем по имени
    std::sort(people.begin(), people.end(),
              [](const Person& a, const Person& b) {
                  return a.age != b.age ? a.age < b.age : a.name < b.name;
              });

    // Поиск первого человека старше 28
    auto it = std::find_if(people.begin(), people.end(),
                           [](const Person& p) { return p.age > 28; });

    // Подсчёт людей младше 30
    auto young = std::count_if(people.begin(), people.end(),
                               [](const Person& p) { return p.age < 30; });

    // Суммарный возраст
    int total = std::accumulate(
        people.begin(), people.end(), 0,
        [](int sum, const Person& p) { return sum + p.age; });

    // Удаление людей младше 30 (идиома erase-remove)
    people.erase(std::remove_if(people.begin(), people.end(),
                                [](const Person& p) { return p.age < 30; }),
                 people.end());

    // Проверка, все ли старше 20
    bool all_adults = std::all_of(people.begin(), people.end(),
                                  [](const Person& p) { return p.age > 20; });

    std::cout << "young=" << young << ", total=" << total
              << ", all_adults=" << all_adults << '\n';
}
```

## Преимущества алгоритмов STL перед собственноручно написанными функциями

1. **Корректность и надёжность.** Алгоритмы STL многократно протестированы и стандартизированы. В них уже учтены граничные случаи (пустой диапазон, один элемент, перекрывающиеся диапазоны в `copy` и т.п.), которые легко упустить в ручной реализации.

2. **Отсутствие «велосипедов» и ошибок.** Ручные циклы — частый источник off-by-one ошибок, неправильной работы с итераторами (`end()` вместо `--end()`), забытого `++it` и т.д. Алгоритм устраняет целый класс таких ошибок.

3. **Выразительность и читаемость.** `std::find_if(v.begin(), v.end(), pred)` сразу говорит, *что* делается, а не *как*. Намерение видно из имени алгоритма; ручной цикл приходится читать целиком, чтобы понять смысл.

4. **Меньше кода, проще сопровождение.** Одна строка вместо 5–10 строк цикла; меньше мест для ошибок при изменении логики.

5. **Производительность.** Реализации стандартных библиотек оптимизированы: векторизация, разворачивание циклов, специальные алгоритмы (например, интроспективная сортировка в `std::sort`, оптимизации для тривиально копируемых типов в `std::copy`/`std::fill`). Ручной цикл обычно не достигает такой оптимизации, а компилятор не всегда может её вывести.

6. **Универсальность.** Один и тот же алгоритм работает с любым контейнером (vector, list, deque, map, массивы, строки) через итераторы — не нужно писать отдельную функцию под каждый тип.

7. **Совместимость с параллелизмом.** В C++17 появились перегрузки с политиками выполнения (`std::execution::par`), позволяющие распараллелить алгоритм без переписывания кода.

8. **Композируемость.** Алгоритмы легко комбинируются: `transform` + `copy_if` + `accumulate`, что позволяет строить конвейеры обработки данных.

9. **Согласованность со стандартом и экосистемой.** Код в стиле STL знаком любому C++-разработчику, легче проходит ревью, лучше сочетается с range-based for, лямбдами и ranges (C++20).

10. **Гарантии сложности.** Стандарт задаёт требования к сложности (например, `std::sort` — O(N log N), `std::find` — O(N)), что делает поведение кода предсказуемым.

**Когда всё же пишут свои функции:** нестандартная логика, специфические требования к производительности/памяти, отсутствие подходящего алгоритма, работа с сырыми данными без итераторов. Но и тогда предпочтительно сначала поискать комбинацию существующих алгоритмов.