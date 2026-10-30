# 37. Что такое curiously recurring template pattern?

**CRTP (curiously recurring template pattern, «любопытно повторяющийся шаблонный образец»; также «шаблон рекурсивного наследования», «static polymorphism idiom»)** — это идиома C++, в которой класс-наследник передаёт **сам себя** в качестве шаблонного параметра своему базовому классу:

```cpp
template <typename Derived>
class Base {
    // ...
};

class Derived : public Base<Derived> {   // Derived встречается в собственной базе
    // ...
};
```

Название подчёркивает «курьёзность»: тип рекурсивно входит в собственное определение — `Derived` является аргументом шаблона того класса, от которого он наследуется. Термин ввёл Джеймс Коплиен (James Coplien) в 1995 году в журнале *C++ Report*; сама конструкция использовалась и раньше. Теоретический аналог в функциональном программировании называется **F-bounded polymorphism** (Canning et al., 1989).

Суть приёма: поскольку базовый класс — шаблон, он **знает точный тип наследника на этапе компиляции**. Это позволяет базе вызывать методы наследника без `virtual`, то есть реализовать **статический (компиляционный) полиморфизм**.

---

## Почему это вообще компилируется

В точке `class Derived : public Base<Derived>` тип `Derived` ещё **неполный** (incomplete type): компилятор только начал его определять. Тем не менее `Base<Derived>` инстанцируется — и это законно, потому что `Derived` внутри шаблона является **зависимым именем**, а всё зависимое откладывается:

* объявления членов и сигнатуры инстанцируются сразу;
* **тела функций-членов шаблона класса инстанцируются только при использовании** (odr-use), а к этому моменту `Derived` уже полный тип.

Отсюда практические правила:

```cpp
template <typename Derived>
class Base {
    // ОШИБКА: Derived ещё неполный на уровне класса
    // static_assert(sizeof(Derived) > 0, "");

    void ok() {
        // OK: тело инстанцируется позже, Derived уже полный
        static_assert(sizeof(Derived) > 0, "");
        static_cast<Derived*>(this)->impl();   // законный downcast
    }
};
```

Именно поэтому `static_cast<Derived*>(this)` внутри функций базы корректен: стандарт требует полноты `Derived` в точке инстанцирования тела, и она обеспечена.

---

## Что это даёт

1. **Статический полиморфизм** — выбор реализации на этапе компиляции, без vtable и косвенных вызовов.
2. **Возможность инлайна** — вызовы «виртуальных» функций полностью инлайнятся, оптимизатор видит конкретный тип.
3. **Нулевые накладные расходы** — пустая база обычно «схлопывается» (empty base optimization), размер объекта не растёт.
4. **Доступ к статическим членам наследника** — база может вызывать `Derived::static_func()` без объекта.
5. **Возможность возвращать `Derived&`/`Derived`** — удобно для цепочек вызовов, клонирования, операторов сравнения.
6. **Работа в `constexpr`-контексте** — статический вызов естественно вычислим на этапе компиляции.

---

## Типичные применения

### 1. Статический полиморфизм (основной сценарий)

```cpp
#include <iostream>

template <typename Derived>
class Shape {
public:
    double area() const {
        // «Виртуальный» вызов, разрешаемый на этапе компиляции
        return static_cast<const Derived*>(this)->area_impl();
    }

    void describe() const {
        std::cout << "area = " << area() << '\n';
    }
};

class Circle : public Shape<Circle> {
public:
    explicit Circle(double r) : r_(r) {}
    double area_impl() const { return 3.141592653589793 * r_ * r_; }
private:
    double r_;
};

class Square : public Shape<Square> {
public:
    explicit Square(double a) : a_(a) {}
    double area_impl() const { return a_ * a_; }
private:
    double a_;
};

int main() {
    Circle c{2.0};
    Square s{3.0};
    c.describe();   // area = 12.5664
    s.describe();   // area = 9
}
```

Ни одного `virtual`, ни одного `vptr` — при этом `describe()` работает для любого типа, наследующего `Shape<...>`.

### 2. `std::enable_shared_from_this` — самый известный пример из стандартной библиотеки

```cpp
class Session : public std::enable_shared_from_this<Session> {
public:
    void start() {
        auto self = shared_from_this();   // std::shared_ptr<Session>
        async_read(self);                 // продлеваем жизнь объекта
    }
};
```

`enable_shared_from_this<T>` хранит `std::weak_ptr<T>` и при создании `shared_ptr<T>` «подхватывает» его. Это классический CRTP: `T` передаёт себя в базу. Требуется **публичное** наследование.

### 3. Счётчик экземпляров (у каждого типа — свой)

```cpp
template <typename Derived>
class InstanceCounter {
public:
    InstanceCounter()                       { ++count_; }
    InstanceCounter(const InstanceCounter&) { ++count_; }
    ~InstanceCounter()                      { --count_; }

    static int alive() { return count_; }

private:
    inline static int count_ = 0;   // C++17
};

class Widget : public InstanceCounter<Widget> { };
class Gadget : public InstanceCounter<Gadget> { };
```

У `Widget` и `Gadget` — независимые счётчики, потому что это разные инстанциации `InstanceCounter<...>`.

### 4. Генерация операторов сравнения (в связке с Barton–Nackman trick)

```cpp
template <typename Derived>
struct Comparable {
    friend bool operator!=(const Derived& a, const Derived& b) { return !(a == b); }
    friend bool operator>(const Derived& a, const Derived& b)  { return b < a; }
    friend bool operator<=(const Derived& a, const Derived& b) { return !(b < a); }
    friend bool operator>=(const Derived& a, const Derived& b) { return !(a < b); }
};

struct Money : Comparable<Money> {
    long long cents = 0;

    friend bool operator==(const Money& a, const Money& b) { return a.cents == b.cents; }
    friend bool operator<(const Money& a, const Money& b)  { return a.cents < b.cents; }
};
```

Дружественные функции, определённые внутри `Comparable<Derived>`, находятся через ADL для `Derived` (базовый класс входит в множество ассоциированных классов). В C++20 это часто заменяется на `operator<=>`.

### 5. Fluent interface / цепочки вызовов

```cpp
template <typename Derived>
class Builder {
public:
    Derived& set_name(std::string n) {
        static_cast<Derived*>(this)->name_ = std::move(n);
        return static_cast<Derived&>(*this);
    }
    Derived& set_size(int s) {
        static_cast<Derived*>(this)->size_ = s;
        return static_cast<Derived&>(*this);
    }
};

class HttpRequest : public Builder<HttpRequest> {
    friend class Builder<HttpRequest>;
    std::string name_;
    int size_ = 0;
};

// HttpRequest req;
// req.set_name("GET").set_size(10);
```

База возвращает **точный** тип `Derived&`, а не `Builder&`, поэтому цепочка не «срезается» и продолжает видеть методы наследника.

### 6. «Виртуальный клон» без виртуальных функций

```cpp
template <typename Derived>
struct Cloneable {
    Derived clone() const { return Derived(*static_cast<const Derived*>(this)); }
};
```

### 7. Одновременно статический и динамический интерфейс

Часто CRTP-база наследует обычный абстрактный интерфейс — получается «два уровня»: быстрый статический вызов для шаблонного кода и обычный полиморфизм для гетерогенных контейнеров.

```cpp
struct IShape {
    virtual ~IShape() = default;
    virtual double area() const = 0;
};

template <typename D>
struct Shape : IShape {
    double area() const override { return static_cast<const D*>(this)->area_impl(); }
};

class Circle : public Shape<Circle> {
public:
    double area_impl() const { return 3.141592653589793 * r_ * r_; }
private:
    double r_ = 1.0;
};
```

### 8. Прочие известные применения

* `std::view_interface<D>` / `std::ranges::view_interface` (C++20) — CRTP-база для диапазонов.
* `boost::operators`, `boost::iterator_facade`, `boost::iterator_adaptor`, `boost::equality_comparable`.
* Expression templates (Eigen, Blaze) — статический полиморфизм выражений.
* Policy-based design и миксины (Александреску, «Modern C++ Design»).
* Статические «интерфейсы» с проверкой требований к наследнику на этапе компиляции.

---

## CRTP против виртуальных функций

| Критерий | CRTP (статический) | `virtual` (динамический) |
|---|---|---|
| Момент выбора реализации | компиляция | выполнение |
| Стоимость вызова | нулевая, возможен инлайн | косвенный вызов через vtable |
| Размер объекта | обычно 0 (EBO) | + указатель на vtable |
| Гетерогенный контейнер | нельзя без общего нетемплейного базового типа | естественно |
| Тип известен в рантайме | нет | да |
| Плагины, динамическая загрузка | нет | да |
| Объём кода | своя копия на каждую инстанциацию | одна на иерархию |
| Время компиляции | больше | меньше |
| Диагностика ошибок | тяжёлая, «простыни» шаблонов | обычная |
| `constexpr` | естественно | ограниченно |
| ABI / раздельная компиляция | всё в заголовке | стабильнее |
| Добавление типа в рантайме | невозможно | возможно |

Важно: современные компиляторы умеют девиртуализировать вызовы (особенно при `final` и LTO), поэтому «виртуальные функции всегда медленнее» — упрощение. Преимущество CRTP — **гарантированный** нулевой overhead и инлайн без анализа потока программы, а не абсолютная скорость.

---

## Ограничения и подводные камни

1. **Нет общего базового типа.** `Base<Circle>` и `Base<Square>` — совершенно разные, никак не связанные типы. Нельзя завести `std::vector<Base<...>*>`. Решение — нетемплейный интерфейс (см. пример 7).

2. **Нет динамического полиморфизма.** Все типы должны быть известны при компиляции; набор типов нельзя расширить в рантайме.

3. **Раздувание кода.** Каждая инстанциация порождает собственный набор функций. При сотнях типов бинарник и время компиляции заметно растут.

4. **Всё в заголовке.** Определения обязаны быть видимы во всех TU; вынести в `.cpp` можно только с явным инстанцированием. Отсюда — зависимость от заголовков и проблемы ABI.

5. **Плохая диагностика.** Ошибки часто «всплывают» глубоко внутри `Base`, а не в месте реальной ошибки в наследнике; сообщения длинные и запутанные.

6. **Нет проверки, что `D` действительно наследует `Base<D>`.** Если `static_cast` ни разу не используется, код может скомпилироваться, хотя иерархия неверна. Рекомендуется явная проверка:

```cpp
template <typename D>
class Base {
public:
    Base() {
        static_assert(std::is_base_of_v<Base<D>, D>,
                      "D must derive from Base<D>");
    }
};
```

7. **Конструктор и деструктор базы.** В них объект `D` ещё не построен (или уже разрушен). Вызов через `static_cast<D*>(this)` методов `D` — **неопределённое поведение**. Не вызывайте «переопределённые» функции из конструктора/деструктора базы.

8. **Дальнейшее наследование «не работает».** Если `class E : public D`, то `Base<D>` по-прежнему видит **только `D`**, а не `E`: вызовы статически привязаны к `D`, а не к фактическому типу. В этом принципиальное отличие от `virtual`.

9. **Скрытие имён.** Метод наследника с тем же именем скрывает метод базы; нужно `using Base<D>::foo;`.

10. **Доступ к `private` наследника.** База не имеет доступа к закрытым членам `D`; нужен `friend class Base<D>;` в наследнике (или `protected`).

11. **Множественное наследование.** `class D : Base1<D>, Base2<D>` создаёт неоднозначности при преобразованиях `this`; требуется аккуратность.

12. **Нет виртуального деструктора.** Удаление объекта через `Base<D>*` — UB. Обычно `Base<D>*` просто не используют как владеющий указатель, но при комбинации с нетемплейным интерфейсом это надо учитывать.

13. **Зависимая база и поиск имён.** Если наследник сам является шаблоном, унаследованные имена не находятся неквалифицированно — нужны `this->member` или `Base<D>::member`.

14. **Потокобезопасность.** Статические счётчики и прочие «общие» данные CRTP-базы сами по себе не атомарны — синхронизация на программисте.

---

## Современная альтернатива: deducing this (C++23)

В C++23 появился «explicit object parameter» (P0847, *deducing this*), который позволяет решать часть тех же задач без наследования и без `static_cast`:

```cpp
struct Shape {
    template <typename Self>
    double area(this const Self& self) {
        return self.area_impl();   // Self выводится как фактический тип
    }
};

struct Circle : Shape {
    double area_impl() const { return 3.141592653589793 * r_ * r_; }
    double r_ = 1.0;
};
```

Это сохраняет статическую диспетчеризацию, но не требует, чтобы наследник что-то знал о базе, корректно работает с `const`/`&&`-квалификаторами и даёт более понятные ошибки. Поддержка постепенно появляется в свежих версиях GCC, Clang и MSVC; до её повсеместного распространения CRTP остаётся основным инструментом.

---

## Итог

**CRTP** — это идиома `class D : Base<D>`, при которой базовый шаблон параметризуется типом наследника. Она даёт:

* статический полиморфизм без `virtual` и без накладных расходов;
* доступ базы к типу и членам наследника на этапе компиляции;
* удобные миксины: счётчики, операторы сравнения, клонирование, fluent-интерфейсы, статические интерфейсы.

Плата за это — отсутствие общего базового типа и динамического полиморфизма, раздувание кода, header-only-зависимость, тяжёлая диагностика и ряд ограничений, связанных с неполным типом наследника в точке определения базы. Поэтому CRTP применяют там, где типы известны на этапе компиляции и важна производительность (embedded, high-performance libraries, метапрограммирование), а для рантайм-полиморфизма оставляют обычные виртуальные функции — часто комбинируя оба подхода через нетемплейный интерфейс.