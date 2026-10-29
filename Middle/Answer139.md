# 139. Как создать поток с помощью std::thread?

`std::thread` — класс из заголовка `<thread>` (C++11), представляющий один поток исполнения. Поток начинает выполняться сразу после конструирования объекта `std::thread`.

## Базовый синтаксис

```cpp
#include <thread>
#include <iostream>

void hello() {
    std::cout << "Привет из потока!\n";
}

int main() {
    std::thread t(hello);   // поток запускается сразу
    t.join();               // ждём завершения
}
```

## Способы создания

### 1. Свободная функция

```cpp
void func(int x) { /* ... */ }
std::thread t(func, 42);
```

### 2. Лямбда-выражение

```cpp
std::thread t([] {
    std::cout << "lambda thread\n";
});
t.join();
```

### 3. Функтор (объект с `operator()`)

```cpp
struct Task {
    void operator()() const { /* ... */ }
};
std::thread t(Task{});
```

### 4. Функция-член класса

```cpp
class Worker {
public:
    void run(int id) { /* ... */ }
};

Worker w;
std::thread t(&Worker::run, &w, 1);  // передаём указатель на объект
```

### 5. Статическая функция-член

```cpp
class Util {
public:
    static void work() { /* ... */ }
};
std::thread t(&Util::work);
```

## Передача аргументов

Аргументы **копируются** (или перемещаются) в хранилище потока. Важные нюансы:

```cpp
void byRef(int& x) { x = 10; }

int a = 0;
std::thread t(byRef, std::ref(a));  // std::ref — передача по ссылке
t.join();
// a == 10
```

Без `std::ref` компилятор попытается скопировать `a` в `int&` и выдаст ошибку. Для константных ссылок копия создаётся автоматически и передаётся как `const&`.

## Важные правила

- **`join()` или `detach()` обязательны.** Если объект `std::thread` уничтожается, будучи *joinable*, вызывается `std::terminate()`.
- **`joinable()`** возвращает `true`, если поток ещё не был присоединён/отсоединён.
- **`join()`** блокирует вызывающий поток до завершения работы потока.
- **`detach()`** отделяет поток — он работает независимо, и `std::thread` больше им не управляет.
- **Нельзя копировать** `std::thread`, но можно **перемещать** (`std::move`).

```cpp
std::thread t1(hello);
std::thread t2 = std::move(t1);  // ок
// std::thread t3 = t2;          // ошибка компиляции
```

- **Исключения в потоке**, не пойманные внутри, приводят к вызову `std::terminate()`.

## RAII-обёртка (рекомендуется)

Ручное управление `join`/`detach` чревато ошибками. Часто пишут RAII-класс:

```cpp
class ThreadGuard {
    std::thread t_;
public:
    explicit ThreadGuard(std::thread t) : t_(std::move(t)) {
        if (!t_.joinable())
            throw std::logic_error("thread not joinable");
    }
    ~ThreadGuard() { if (t_.joinable()) t_.join(); }
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;
};
```

## Практический пример

```cpp
#include <thread>
#include <vector>
#include <iostream>

int main() {
    std::vector<std::thread> pool;
    for (int i = 0; i < 4; ++i) {
        pool.emplace_back([i] {
            std::cout << "thread " << i << "\n";
        });
    }
    for (auto& t : pool) t.join();
}
```

Обратите внимание: `emplace_back` конструирует `std::thread` на месте, избегая лишних перемещений.