# 176. Реализуйте макрос для сравнения двух строк

## Постановка задачи

Требуется реализовать **макрос** на C++ для сравнения двух строк. Под «строками» могут пониматься:
- C-строки (`const char*`, завершающиеся `'\0'`);
- объекты `std::string`.

Макрос должен быть универсальным (работать и с C-строками, и с `std::string`) и корректным с точки зрения побочных эффектов аргументов.

---

## Вариант 1. Простейший макрос для C-строк

Самый прямолинейный вариант — обёртка над `strcmp`:

```cpp
#include <cstring>

#define STR_EQ(a, b) (std::strcmp((a), (b)) == 0)
```

Использование:

```cpp
const char* s1 = "hello";
const char* s2 = "hello";
if (STR_EQ(s1, s2)) {
    // строки равны
}
```

**Недостатки:**
- Работает только с C-строками (`const char*`), но **не** со `std::string` (для `std::string` нужен `.c_str()`).
- Аргументы вычисляются дважды в некоторых реализациях (здесь — по одному разу, но всё равно макрос небезопасен при сложных выражениях с побочными эффектами).
- Не учитывает `nullptr`.

---

## Вариант 2. Универсальный макрос (C-строки + `std::string`)

Чтобы макрос работал и с `std::string`, и с `const char*`, используем перегрузки вспомогательной inline-функции (это безопаснее, чем пытаться «втиснуть» логику в макрос):

```cpp
#include <string>
#include <cstring>

namespace detail {
    inline bool strEqual(const char* a, const char* b) {
        if (!a || !b) return a == b;           // обе nullptr — равны
        return std::strcmp(a, b) == 0;
    }
    inline bool strEqual(const std::string& a, const std::string& b) {
        return a == b;
    }
    inline bool strEqual(const std::string& a, const char* b) {
        return b ? a == b : false;
    }
    inline bool strEqual(const char* a, const std::string& b) {
        return a ? b == a : false;
    }
}

#define STR_EQUAL(a, b) (::detail::strEqual((a), (b)))
```

Использование:

```cpp
std::string s1 = "test";
const char* s2 = "test";
std::string s3 = "other";

STR_EQUAL(s1, s2);   // true
STR_EQUAL(s1, s3);   // false
STR_EQUAL(s1, s1);   // true
STR_EQUAL(nullptr, nullptr); // true
```

**Плюсы:**
- Работает со всеми комбинациями `std::string` / `const char*`.
- Аргументы вычисляются ровно один раз.
- Корректно обрабатывает `nullptr`.

---

## Вариант 3. Макрос, возвращающий результат сравнения (а не только равенство)

Если нужен не только факт равенства, но и «знак» сравнения (как у `strcmp`), можно сделать так:

```cpp
#include <cstring>

#define STR_CMP(a, b) (std::strcmp((a), (b)))
```

Но для `std::string` это не сработает — нужен `.c_str()`. Поэтому лучше оставить `STR_CMP` только для C-строк, а для `std::string` использовать встроенный `compare`.

---

## Вариант 4. «Максимально безопасный» макрос через `do { ... } while(0)`

Если по заданию требуется именно **макрос-выражение** (а не обёртка над функцией), можно оформить его так:

```cpp
#include <cstring>

#define STR_EQ(a, b) \
    ([](const char* x, const char* y) { \
        return (x && y) ? std::strcmp(x, y) == 0 : x == y; \
    }((a), (b)))
```

Здесь используется лямбда — аргументы вычисляются ровно один раз, `nullptr` обрабатывается корректно. Однако такой макрос работает **только с C-строками**.

---

## Рекомендация

В реальном коде **макросы для сравнения строк — плохая практика**: их легко misuse (двойное вычисление аргументов, отсутствие проверки типов, проблемы с областями видимости). Гораздо лучше использовать:

```cpp
inline bool strEqual(const std::string& a, const std::string& b) { return a == b; }
inline bool strEqual(const char* a, const char* b) {
    return (a && b) ? std::strcmp(a, b) == 0 : a == b;
}
```

или просто `operator==` для `std::string`.

Однако **в рамках учебного задания** «реализуйте макрос» корректный ответ — это макрос-обёртка над `strcmp` (вариант 1) либо универсальный макрос с перегрузками (вариант 2), если требуется поддержка `std::string`.

---

## Итоговый ответ (кратко)

```cpp
#include <cstring>
#include <string>

namespace detail {
    inline bool strEqual(const char* a, const char* b) {
        return (a && b) ? std::strcmp(a, b) == 0 : a == b;
    }
    inline bool strEqual(const std::string& a, const std::string& b) { return a == b; }
    inline bool strEqual(const std::string& a, const char* b) { return b && a == b; }
    inline bool strEqual(const char* a, const std::string& b) { return a && b == a; }
}

#define STR_EQUAL(a, b) (::detail::strEqual((a), (b)))
```

Макрос `STR_EQUAL(a, b)` возвращает `true`, если строки равны, и корректно работает как с C-строками, так и с `std::string`, а также с `nullptr`.