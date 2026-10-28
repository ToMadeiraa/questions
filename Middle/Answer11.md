# 11. Назовите порождающие, структурные и поведенческие паттерны программирования и приведите примеры их использования.

Паттерны (шаблоны) проектирования — это типовые решения часто встречающихся задач проектирования ПО. Классическая книга «Банды четырёх» (GoF — Gang of Four) делит их на три группы: **порождающие** (creational), **структурные** (structural) и **поведенческие** (behavioral). Ниже — перечень паттернов каждой группы с примерами применения и краткими иллюстрациями на C++.

---

## Порождающие паттерны (Creational)

Отвечают за то, **как создаются объекты**, скрывая детали создания и делая систему независимой от способа порождения объектов.

| Паттерн | Суть | Пример использования в C++ |
|---|---|---|
| **Factory Method** (фабричный метод) | Определяет интерфейс создания объекта, а подклассы решают, какой класс инстанцировать | Платформозависимые классы `Document`/`Button`: `WindowsButton`, `LinuxButton`; `std::make_unique` как идиома создания |
| **Abstract Factory** (абстрактная фабрика) | Интерфейс для создания семейств связанных объектов | Кроссплатформенный GUI-тулкит: `WinFactory` и `MacFactory` создают согласованные `Button` + `CheckBox` |
| **Builder** (строитель) | Пошаговое конструирование сложного объекта, отделяя процесс от представления | Построение SQL-запроса или HTTP-запроса: `QueryBuilder().select(...).from(...).where(...).build()` |
| **Prototype** (прототип) | Создание объекта копированием существующего экземпляра | Клонирование сложных конфигураций: `std::unique_ptr<Shape> clone()` возвращает копию без знания конкретного типа |
| **Singleton** (одиночка) | Гарантирует единственный экземпляр класса и глобальную точку доступа | Логгер, пул соединений, конфигурация. В C++11 — через «магическую статику»: `static Logger& instance() { static Logger l; return l; }` |

**Пример (Singleton, thread-safe в C++11):**
```cpp
class Logger {
public:
    static Logger& instance() {
        static Logger logger;   // инициализация гарантированно потокобезопасна
        return logger;
    }
    void log(const std::string& msg);
private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
};
```

**Пример (Builder):**
```cpp
class Pizza {
public:
    class Builder {
    public:
        Builder& size(int s) { size_ = s; return *this; }
        Builder& cheese(bool c) { cheese_ = c; return *this; }
        Pizza build() const { return Pizza(size_, cheese_); }
    private:
        int size_ = 30;
        bool cheese_ = false;
    };
private:
    Pizza(int s, bool c) : size_(s), cheese_(c) {}
    int size_; bool cheese_;
};
// Использование:
Pizza p = Pizza::Builder{}.size(40).cheese(true).build();
```

---

## Структурные паттерны (Structural)

Описывают **способы компоновки классов и объектов** в более крупные структуры, сохраняя гибкость и эффективность.

| Паттерн | Суть | Пример использования в C++ |
|---|---|---|
| **Adapter** (адаптер) | Преобразует интерфейс класса в ожидаемый клиентом интерфейс | Обёртка над сторонней библиотекой (например, `std::stack`/`std::queue` как адаптеры над контейнерами); адаптер legacy-API к новому интерфейсу |
| **Bridge** (мост) | Разделяет абстракцию и реализацию, чтобы они могли меняться независимо | Графические фигуры (`Shape`) и способы отрисовки (`Renderer`: OpenGL, Vulkan) |
| **Composite** (компоновщик) | Древовидная структура «часть-целое» с единым интерфейсом | Дерево файловой системы, UI-дерево виджетов (контейнер и лист обрабатываются одинаково) |
| **Decorator** (декоратор) | Динамически добавляет объекту обязанности, оборачивая его | `std::iostream`-цепочки; потоки с буферизацией/сжатием/шифрованием поверх базового `Stream` |
| **Facade** (фасад) | Упрощённый интерфейс к сложной подсистеме | Класс `MultimediaFacade`, скрывающий работу с аудио/видео/кодеками |
| **Flyweight** (приспособленец) | Разделение мелкозернистых объектов для экономии памяти | Кэш символов шрифта, разделяемые спрайты/текстуры, интернирование строк |
| **Proxy** (заместитель) | Суррогат, контролирующий доступ к объекту | «Ленивая» загрузка изображений, удалённый прокси (RPC), умные указатели `std::shared_ptr` (счётчик ссылок — прокси) |

**Пример (Decorator):**
```cpp
struct Stream { virtual void write(const std::string&) = 0; virtual ~Stream() = default; };

class FileStream : public Stream {
public: void write(const std::string& d) override { /* запись в файл */ }
};

class CompressedStream : public Stream {              // декоратор
    Stream& inner_;
public:
    explicit CompressedStream(Stream& s) : inner_(s) {}
    void write(const std::string& d) override { inner_.write(compress(d)); }
};
// Использование: FileStream f; CompressedStream cf(f); cf.write("data");
```

**Пример (Proxy — ленивая загрузка):**
```cpp
class Image { public: virtual void draw() = 0; virtual ~Image() = default; };

class RealImage : public Image {
public:
    explicit RealImage(const std::string& f) { /* тяжёлая загрузка */ }
    void draw() override { /* отрисовка */ }
};

class ImageProxy : public Image {
    std::string file_;
    std::unique_ptr<RealImage> real_;
public:
    explicit ImageProxy(std::string f) : file_(std::move(f)) {}
    void draw() override {
        if (!real_) real_ = std::make_unique<RealImage>(file_); // загрузка по требованию
        real_->draw();
    }
};
```

---

## Поведенческие паттерны (Behavioral)

Описывают **взаимодействие объектов** и распределение обязанностей между ними.

| Паттерн | Суть | Пример использования в C++ |
|---|---|---|
| **Chain of Responsibility** (цепочка обязанностей) | Передача запроса по цепочке обработчиков до первого, кто его обработает | Обработка событий в GUI, middleware в HTTP-серверах, логирование по уровням |
| **Command** (команда) | Инкапсулирует запрос как объект | Undo/Redo, очереди задач, макросы; `std::function` как простая форма команды |
| **Interpreter** (интерпретатор) | Грамматика языка + интерпретатор выражений | Вычислитель математических выражений, парсер DSL |
| **Iterator** (итератор) | Последовательный доступ к элементам без раскрытия представления | `std::vector<int>::iterator`, range-based for, собственные итераторы для контейнеров |
| **Mediator** (посредник) | Централизует взаимодействие множества объектов | Диспетчер событий, «шина» сообщений, диалоговые окна с множеством виджетов |
| **Memento** (снимок) | Сохранение и восстановление состояния без нарушения инкапсуляции | Undo-стек, чекпоинты, сериализация состояния |
| **Observer** (наблюдатель) | Подписка на изменения и уведомление подписчиков | `std::function`-колбэки, сигналы/слоты (Qt), event-листенеры |
| **State** (состояние) | Поведение объекта меняется вместе с его состоянием | Конечные автоматы (соединение TCP, заказ, медиаплеер) |
| **Strategy** (стратегия) | Взаимозаменяемые алгоритмы за общим интерфейсом | Компараторы сортировки, алгоритмы сжатия/шифрования, `std::sort(v, cmp)` |
| **Template Method** (шаблонный метод) | Скелет алгоритма в базовом классе, шаги — в наследниках | `std::sort`/`std::for_each` как обобщённые алгоритмы; базовый класс `Game` с `start()/update()/finish()` |
| **Visitor** (посетитель) | Операции над объектами без изменения их классов | Обход AST компилятора, сериализация иерархии классов, double dispatch |

**Пример (Observer через `std::function`):**
```cpp
class Subject {
    std::vector<std::function<void(int)>> observers_;
public:
    void subscribe(std::function<void(int)> cb) { observers_.push_back(std::move(cb)); }
    void notify(int value) { for (auto& o : observers_) o(value); }
};
// Использование:
Subject s;
s.subscribe([](int v){ std::cout << "A: " << v << '\n'; });
s.subscribe([](int v){ std::cout << "B: " << v << '\n'; });
s.notify(42);
```

**Пример (Strategy):**
```cpp
struct SortStrategy { virtual void sort(std::vector<int>&) = 0; virtual ~SortStrategy() = default; };

class Context {
    std::unique_ptr<SortStrategy> strategy_;
public:
    explicit Context(std::unique_ptr<SortStrategy> s) : strategy_(std::move(s)) {}
    void run(std::vector<int>& v) { strategy_->sort(v); }
};
```

**Пример (State):**
```cpp
class Connection;

class State {
public:
    virtual void open(Connection&) = 0;
    virtual void close(Connection&) = 0;
    virtual ~State() = default;
};

class Connection {
    std::unique_ptr<State> state_;
public:
    void setState(std::unique_ptr<State> s) { state_ = std::move(s); }
    void open()  { state_->open(*this); }
    void close() { state_->close(*this); }
};
```

---

## Краткая сводка

- **Порождающие:** Factory Method, Abstract Factory, Builder, Prototype, Singleton — *как создавать объекты*.
- **Структурные:** Adapter, Bridge, Composite, Decorator, Facade, Flyweight, Proxy — *как компоновать классы и объекты*.
- **Поведенческие:** Chain of Responsibility, Command, Interpreter, Iterator, Mediator, Memento, Observer, State, Strategy, Template Method, Visitor — *как организовывать взаимодействие и распределять обязанности*.

Важно помнить: паттерны — это не самоцель, а инструмент. В современном C++ многие из них реализуются проще благодаря `std::function`, лямбдам, шаблонам, умным указателям и `std::variant` (например, Strategy часто заменяется на `std::function`, а Visitor — на `std::variant` + `std::visit`).