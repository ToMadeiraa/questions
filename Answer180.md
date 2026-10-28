# 180. Реализуйте подсчет чисел Фибоначчи.

## Что такое числа Фибоначчи

Числа Фибоначчи — это последовательность, в которой каждое следующее число равно сумме двух предыдущих:

```
F(0) = 0
F(1) = 1
F(n) = F(n-1) + F(n-2), для n >= 2
```

Первые числа: `0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, ...`

Ниже — несколько способов реализации «подсчёта» чисел Фибоначчи на C++ с разбором их достоинств и недостатков. Под «подсчётом» обычно понимают либо вычисление n-го числа, либо генерацию первых n чисел, либо подсчёт количества вызовов/операций (например, при рекурсии).

---

## 1. Итеративный способ (рекомендуемый)

Самый простой и эффективный вариант: O(n) по времени и O(1) по памяти.

```cpp
#include <iostream>

// Возвращает n-е число Фибоначчи (F(0)=0, F(1)=1)
unsigned long long fibonacciIterative(int n) {
    if (n < 0) throw std::invalid_argument("n must be >= 0");
    if (n == 0) return 0ULL;
    if (n == 1) return 1ULL;

    unsigned long long prev = 0ULL; // F(i-1)
    unsigned long long curr = 1ULL; // F(i)
    for (int i = 2; i <= n; ++i) {
        unsigned long long next = prev + curr;
        prev = curr;
        curr = next;
    }
    return curr;
}

int main() {
    for (int i = 0; i <= 10; ++i)
        std::cout << "F(" << i << ") = " << fibonacciIterative(i) << '\n';
    return 0;
}
```

Вывод:
```
F(0) = 0
F(1) = 1
F(2) = 1
F(3) = 2
F(4) = 3
F(5) = 5
F(6) = 8
F(7) = 13
F(8) = 21
F(9) = 34
F(10) = 55
```

**Плюсы:** быстро, без переполнения стека, легко понять.  
**Минусы:** для очень больших n нужно использовать «длинную» арифметику (например, `boost::multiprecision::cpp_int`) или `unsigned long long` ограничен n ≈ 93.

---

## 2. Рекурсивный способ (с мемоизацией)

Классическая рекурсия `F(n) = F(n-1) + F(n-2)` без мемоизации имеет экспоненциальную сложность O(2^n). Мемоизация (кэш) сводит её к O(n).

```cpp
#include <iostream>
#include <vector>

unsigned long long fibonacciMemo(int n, std::vector<unsigned long long>& memo) {
    if (n < 0) throw std::invalid_argument("n must be >= 0");
    if (n == 0) return 0ULL;
    if (n == 1) return 1ULL;
    if (memo[n] != 0ULL) return memo[n];          // уже посчитано
    memo[n] = fibonacciMemo(n - 1, memo) + fibonacciMemo(n - 2, memo);
    return memo[n];
}

unsigned long long fibonacciMemo(int n) {
    if (n < 0) throw std::invalid_argument("n must be >= 0");
    std::vector<unsigned long long> memo(n + 1, 0ULL);
    return fibonacciMemo(n, memo);
}
```

Здесь `memo[0] = 0` — особый случай: значение 0 совпадает с «не посчитано», поэтому `F(0)` обрабатывается отдельно до проверки кэша.

**Плюсы:** читается близко к математическому определению.  
**Минусы:** O(n) памяти, риск переполнения стека при больших n.

---

## 3. Рекурсивный способ «в лоб» (для демонстрации сложности)

```cpp
unsigned long long fibonacciNaive(int n) {
    if (n < 0) throw std::invalid_argument("n must be >= 0");
    if (n == 0) return 0ULL;
    if (n == 1) return 1ULL;
    return fibonacciNaive(n - 1) + fibonacciNaive(n - 2);
}
```

**Плюсы:** предельно коротко.  
**Минусы:** экспоненциальная сложность O(2^n) — уже при n = 40 вычисления заметно тормозят. Годится только как учебный пример.

---

## 4. Подсчёт количества операций/вызовов (если под «подсчётом» понимается именно счётчик)

Иногда в практических заданиях нужно не просто вычислить число, а **посчитать**, сколько раз выполняется рекурсивный вызов или сложение. Тогда в функцию добавляют счётчик по ссылке.

```cpp
#include <iostream>

unsigned long long fibonacciCounted(int n, unsigned long long& calls) {
    ++calls; // считаем каждый вызов
    if (n == 0) return 0ULL;
    if (n == 1) return 1ULL;
    return fibonacciCounted(n - 1, calls) + fibonacciCounted(n - 2, calls);
}

int main() {
    for (int n = 0; n <= 10; ++n) {
        unsigned long long calls = 0;
        unsigned long long value = fibonacciCounted(n, calls);
        std::cout << "F(" << n << ") = " << value
                  << ", вызовов: " << calls << '\n';
    }
}
```

Такой счётчик наглядно показывает экспоненциальный рост числа вызовов у наивной рекурсии. Если нужен подсчёт именно арифметических операций — увеличьте счётчик в месте сложения.

---

## 5. Генерация первых n чисел (часто просят именно «подсчёт последовательности»)

```cpp
#include <iostream>
#include <vector>

std::vector<unsigned long long> fibonacciSequence(int n) {
    if (n < 0) throw std::invalid_argument("n must be >= 0");
    std::vector<unsigned long long> result;
    result.reserve(n);
    unsigned long long a = 0ULL, b = 1ULL;
    for (int i = 0; i < n; ++i) {
        result.push_back(a);
        unsigned long long next = a + b;
        a = b;
        b = next;
    }
    return result;
}

int main() {
    for (auto x : fibonacciSequence(10))
        std::cout << x << ' ';
    std::cout << '\n'; // 0 1 1 2 3 5 8 13 21 34
}
```

---

## 6. Быстрое возведение матрицы в степень (для очень больших n)

Если нужно вычислять `F(n)` для больших `n` (сотни тысяч и больше) и по модулю — используют матричную формулу:

```
[ F(n+1) F(n)   ]   [1 1]^n
[ F(n)   F(n-1) ] = [1 0]
```

```cpp
#include <iostream>
#include <array>
#include <cstdint>

using Matrix = std::array<std::array<long long, 2>, 2>;

Matrix mul(const Matrix& a, const Matrix& b, long long mod) {
    Matrix c{};
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k)
                c[i][j] = (c[i][j] + a[i][k] * b[k][j]) % mod;
    return c;
}

Matrix power(Matrix base, long long exp, long long mod) {
    Matrix result = {{{1, 0}, {0, 1}}};
    while (exp > 0) {
        if (exp & 1) result = mul(result, base, mod);
        base = mul(base, base, mod);
        exp >>= 1;
    }
    return result;
}

long long fibonacciFast(long long n, long long mod) {
    if (n < 0) throw std::invalid_argument("n must be >= 0");
    if (n == 0) return 0;
    Matrix m = {{{1, 1}, {1, 0}}};
    Matrix p = power(m, n, mod);
    return p[0][1]; // F(n)
}

int main() {
    const long long MOD = 1'000'000'007LL;
    std::cout << fibonacciFast(10, MOD) << '\n';       // 55
    std::cout << fibonacciFast(1000, MOD) << '\n';     // F(1000) mod 1e9+7
}
```

**Сложность:** O(log n).  
**Плюсы:** подходит для огромных n (по модулю).  
**Минусы:** сложнее в реализации, без модуля быстро переполняется.

---

## Что выбрать на практике

| Способ | Время | Память | Когда использовать |
|---|---|---|---|
| Итеративный | O(n) | O(1) | По умолчанию, обычные n |
| Рекурсия + мемоизация | O(n) | O(n) | Учебные задачи, когда хочется «рекурсивно» |
| Наивная рекурсия | O(2^n) | O(n) | Только для демонстрации сложности / счётчиков |
| Матричное возведение | O(log n) | O(1) | Огромные n, часто по модулю |

**Итоговая рекомендация:** для ответа на практическое задание достаточно итеративной реализации `fibonacciIterative` (п.1). Если в задании явно требуют «подсчитать», уточните, что имеется в виду — вычисление n-го числа, генерация последовательности или подсчёт вызовов рекурсии; для каждого случая выше дан готовый вариант.