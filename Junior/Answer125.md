## 125. Как получить наименьшее значение типа в C++

Для получения минимального значения встроенного типа в C++ есть несколько способов, которые зависят от версии стандарта.

---

### 1. `std::numeric_limits` (рекомендуемый способ, C++11 и новее)

Заголовочный файл `<limits>` предоставляет шаблон `std::numeric_limits<T>`, у которого есть статический метод `min()`:

```cpp
#include <limits>
#include <iostream>

int main() {
    std::cout << std::numeric_limits<int>::min() << '\n';          // -2147483648
    std::cout << std::numeric_limits<long long>::min() << '\n';
    std::cout << std::numeric_limits<double>::min() << '\n';       // наименьшее ПОЛОЖИТЕЛЬНОЕ нормализованное
    std::cout << std::numeric_limits<double>::lowest() << '\n';    // наименьшее (самое отрицательное) — C++11
    std::cout << std::numeric_limits<unsigned>::min() << '\n';     // 0
}
```

**Важные нюансы:**

- Для **целочисленных** типов `min()` возвращает наименьшее (самое отрицательное) значение.
- Для **вещественных** типов (float, double) `min()` возвращает **наименьшее положительное нормализованное** значение, а не самое отрицательное. Чтобы получить самое отрицательное, используйте **`lowest()`** (появился в C++11).
- Для `unsigned` типов `min()` всегда равно `0`.

---

### 2. `std::numeric_limits<T>::lowest()` (C++11)

Универсальный способ получить именно «наименьшее по значению» (самое отрицательное) для любого типа, включая вещественные:

```cpp
#include <limits>

auto m = std::numeric_limits<float>::lowest();   // -3.40282e+38
```

---

### 3. Макросы из `<climits>` и `<cfloat>` (устаревший, но всё ещё встречается)

```cpp
#include <climits>
#include <cfloat>

INT_MIN        // минимум int
LONG_MIN       // минимум long
LLONG_MIN      // минимум long long (C++11)
SHRT_MIN       // минимум short
CHAR_MIN       // минимум char (зависит от знаковости)
FLT_MIN        // наименьшее положительное нормализованное float
DBL_MIN        // аналогично для double
-FLT_MAX       // самое отрицательное значение float
```

Обратите внимание: `FLT_MIN` — это **наименьшее положительное**, а не самое отрицательное число.

---

### 4. Для пользовательских типов

Если вы пишете свой тип, можно специализировать `std::numeric_limits`:

```cpp
namespace std {
    template<>
    struct numeric_limits<MyType> {
        static constexpr MyType min() noexcept { return MyType{...}; }
        static constexpr MyType lowest() noexcept { return MyType{...}; }
        // ...
    };
}
```

---

### Итоговая рекомендация

| Задача | Способ |
|---|---|
| Минимум целого типа | `std::numeric_limits<T>::min()` |
| Самое отрицательное значение (в т.ч. для float/double) | `std::numeric_limits<T>::lowest()` |
| Старый код / C-стиль | `INT_MIN`, `FLT_MAX` и т.п. |

**Короткий ответ:** используйте `std::numeric_limits<T>::min()` для целых типов и `std::numeric_limits<T>::lowest()` для вещественных — это типобезопасный и переносимый способ из `<limits>`.