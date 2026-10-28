# 177. Реализуйте реверс строк

## Что означает «реверс строки»

Реверс (переворот) строки — это операция, при которой порядок символов меняется на противоположный. Например, строка `"Hello"` после реверса превращается в `"olleH"`. В C++ существует несколько способов реализации этой операции — от использования стандартной библиотеки до ручного написания алгоритма. Рассмотрим их подробно.

---

## 1. Использование `std::reverse` из `<algorithm>`

Самый простой и идиоматичный способ для C++ — применить стандартный алгоритм `std::reverse`, который работает с любыми контейнерами, включая `std::string`.

```cpp
#include <iostream>
#include <string>
#include <algorithm>

int main() {
    std::string s = "Hello, World!";
    std::reverse(s.begin(), s.end());
    std::cout << s << std::endl; // !dlroW ,olleH
    return 0;
}
```

**Плюсы:** коротко, безопасно, работает за O(n), не требует ручного управления памятью.

---

## 2. Ручная реализация через два указателя (индексы)

Классический алгоритм: два индекса — `left` в начале, `right` в конце; меняем символы местами и сдвигаем индексы навстречу друг другу, пока `left < right`.

```cpp
#include <iostream>
#include <string>
#include <utility> // std::swap

void reverseString(std::string& s) {
    int left = 0;
    int right = static_cast<int>(s.size()) - 1;
    while (left < right) {
        std::swap(s[left], s[right]);
        ++left;
        --right;
    }
}

int main() {
    std::string s = "Practical task";
    reverseString(s);
    std::cout << s << std::endl; // ksat lacitcarP
    return 0;
}
```

**Сложность:** время O(n), дополнительная память O(1). Это оптимальный вариант «in-place».

---

## 3. Реализация через `std::swap` и итераторы

Тот же алгоритм, но записанный в терминах итераторов — так код будет работать не только со `std::string`, но и с `std::vector<char>` и другими контейнерами с двунаправленными итераторами.

```cpp
#include <string>
#include <utility>

template <typename BidirIt>
void reverseRange(BidirIt first, BidirIt last) {
    while (first != last && first != --last) {
        std::swap(*first, *last);
        ++first;
    }
}
```

Обратите внимание на `--last` в условии: он вызывается до проверки `first != last`, что корректно для непустого диапазона.

---

## 4. Рекурсивный вариант (для демонстрации)

```cpp
#include <string>
#include <utility>

void reverseRecursive(std::string& s, int left, int right) {
    if (left >= right) return;
    std::swap(s[left], s[right]);
    reverseRecursive(s, left + 1, right - 1);
}
```

**Минус:** глубина рекурсии равна n/2, что при больших строках может привести к переполнению стека. На практике предпочтительнее итеративный вариант.

---

## 5. Создание новой перевёрнутой строки

Если исходную строку нужно сохранить, можно построить новую:

```cpp
#include <string>
#include <algorithm>

std::string reversedCopy(const std::string& s) {
    return std::string(s.rbegin(), s.rend());
}
```

Или через ручной цикл:

```cpp
std::string reversedCopy(const std::string& s) {
    std::string result;
    result.reserve(s.size());          // избегаем лишних реаллокаций
    for (auto it = s.rbegin(); it != s.rend(); ++it)
        result.push_back(*it);
    return result;
}
```

**Сложность:** время O(n), память O(n).

---

## 6. Реверс «слов» в строке (частный случай)

Иногда под «реверсом строки» в практических заданиях понимают переворот порядка слов, например: `"one two three"` → `"three two one"`. Такой вариант решается в два прохода:

1. Перевернуть всю строку целиком.
2. Перевернуть каждое слово по отдельности.

```cpp
#include <string>
#include <algorithm>
#include <sstream>

std::string reverseWords(const std::string& s) {
    std::string tmp = s;
    std::reverse(tmp.begin(), tmp.end());

    size_t start = 0;
    for (size_t i = 0; i <= tmp.size(); ++i) {
        if (i == tmp.size() || tmp[i] == ' ') {
            std::reverse(tmp.begin() + start, tmp.begin() + i);
            start = i + 1;
        }
    }
    return tmp;
}
```

Пример: `"one two three"` → `"eerht owt eno"` → `"three two one"`.

---

## 7. Реверс C-строки (`char*`)

Если требуется работать с нуль-терминированными массивами символов (часто в учебных заданиях по C++/C):

```cpp
#include <cstring>
#include <utility>

void reverseCString(char* str) {
    if (!str) return;
    size_t len = std::strlen(str);
    if (len < 2) return;
    for (size_t i = 0, j = len - 1; i < j; ++i, --j) {
        std::swap(str[i], str[j]);
    }
}
```

Здесь важно проверить `str` на `nullptr` и корректно обработать пустую строку.

---

## 8. Работа с Unicode (важное замечание)

`std::string` хранит **байты**, а не символы. Для ASCII-строк реверс байтов совпадает с реверсом символов, но для UTF-8 (кириллица, эмодзи и т. п.) простой `std::reverse` «поломает» многобайтовые символы. Корректный подход — либо работать с `std::wstring` / `std::u32string`, либо использовать библиотеку вроде ICU, либо реверсировать по code points вручную. На собеседованиях об этом полезно упомянуть.

---

## Итоговая рекомендация

| Способ | Когда применять |
|---|---|
| `std::reverse` | Основной рабочий вариант в реальном коде |
| Два указателя (`left`/`right`) | На собеседовании / в учебном задании — показывает понимание алгоритма |
| Создание копии через `rbegin()/rend()` | Когда исходную строку нужно сохранить |
| Рекурсия | Только для демонстрации, не для продакшена |
| Реверс слов | Если формулировка задания подразумевает перестановку слов |

Для практического задания чаще всего ожидается **ручная реализация через два указателя** (in-place, O(1) памяти) — её и стоит приводить как основной ответ, дополнив ссылкой на `std::reverse` как на идиоматичный вариант.