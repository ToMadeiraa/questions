# 96. Вызов объекта из nested namespace в C++

**Nested namespace** (вложенное пространство имён) — это пространство имён, объявленное внутри другого пространства имён. Например:

```cpp
namespace outer {
    namespace inner {
        int value = 42;
    }
}
```

Чтобы обратиться к объекту `value`, нужно указать полный путь через оператор разрешения области видимости `::`.

## 1. Полное квалифицированное имя (самый надёжный способ)

Указываете всю цепочку пространств имён от глобального до нужного:

```cpp
#include <iostream>

namespace outer {
    namespace inner {
        int value = 42;
    }
}

int main() {
    std::cout << outer::inner::value << std::endl; // 42
}
```

## 2. Синтаксис C++17: `namespace A::B`

Начиная с C++17 вложенные пространства имён можно объявлять и использовать в компактной форме:

```cpp
namespace outer::inner {
    int value = 42;
}

int main() {
    std::cout << outer::inner::value << std::endl; // 42
}
```

## 3. С помощью `using`-объявления (для одного имени)

```cpp
int main() {
    using outer::inner::value;
    std::cout << value << std::endl; // 42
}
```

## 4. С помощью `using namespace` (для всего пространства имён)

```cpp
int main() {
    using namespace outer::inner;
    std::cout << value << std::endl; // 42
}
```

⚠️ Осторожно: `using namespace` в глобальной области видимости или в заголовочных файлах может приводить к конфликтам имён. Лучше применять его локально — внутри функции.

## 5. Псевдоним пространства имён (`namespace alias`)

Удобно, если путь длинный и используется многократно:

```cpp
namespace oi = outer::inner;

int main() {
    std::cout << oi::value << std::endl; // 42
}
```

## 6. Вызов функции из вложенного namespace

```cpp
namespace outer {
    namespace inner {
        void print() { std::cout << "Hello from inner\n"; }
    }
}

int main() {
    outer::inner::print(); // полное имя

    namespace oi = outer::inner;
    oi::print();           // через псевдоним
}
```

## 7. Через объект-экземпляр класса из вложенного namespace

```cpp
namespace outer {
    namespace inner {
        class Widget {
        public:
            void show() { std::cout << "Widget\n"; }
        };
    }
}

int main() {
    outer::inner::Widget w;
    w.show();
}
```

## Ключевые моменты

- Оператор `::` — «оператор разрешения области видимости» (scope resolution operator), именно он разделяет уровни вложенности.
- Полное имя всегда однозначно и безопасно: `A::B::C::object`.
- В C++17 появился сокращённый синтаксис объявления `namespace A::B { ... }`.
- `using namespace` удобен, но повышает риск конфликтов имён — применяйте дозированно.
- Псевдонимы (`namespace alias`) — хороший компромисс между краткостью и читаемостью.