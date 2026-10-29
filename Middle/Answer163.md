# 163. Напишите реализацию очереди.

Очередь (queue) — это структура данных, работающая по принципу **FIFO** (First In, First Out): элемент, добавленный первым, извлекается первым. Основные операции: `push` (добавить в конец), `pop` (удалить из начала), `front` (посмотреть первый), `back` (посмотреть последний), `empty`, `size`.

Ниже — несколько вариантов реализации: от простейшей на массиве до идиоматичной C++ на `std::deque` и через связный список.

## Вариант 1. Реализация на кольцевом массиве (фиксированная ёмкость)

Классический и эффективный способ — кольцевой буфер. Все операции O(1).

```cpp
#include <stdexcept>

class Queue {
private:
    int* data;
    size_t capacity;
    size_t head;   // индекс первого элемента
    size_t tail;   // индекс позиции для следующего push
    size_t count;  // текущее количество элементов

public:
    explicit Queue(size_t cap) : capacity(cap), head(0), tail(0), count(0) {
        data = new int[capacity];
    }

    ~Queue() { delete[] data; }

    // Запрещаем копирование, чтобы не было двойного delete[]
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    void push(int value) {
        if (count == capacity)
            throw std::overflow_error("Queue is full");
        data[tail] = value;
        tail = (tail + 1) % capacity;
        ++count;
    }

    void pop() {
        if (empty())
            throw std::underflow_error("Queue is empty");
        head = (head + 1) % capacity;
        --count;
    }

    int& front() {
        if (empty()) throw std::underflow_error("Queue is empty");
        return data[head];
    }

    int& back() {
        if (empty()) throw std::underflow_error("Queue is empty");
        return data[(tail == 0 ? capacity : tail) - 1];
    }

    bool empty() const { return count == 0; }
    size_t size() const { return count; }
};
```

Ключевой момент — взятие индексов по модулю `capacity`, благодаря чему «хвост» очереди переходит в начало массива, и память не тратится впустую.

## Вариант 2. Реализация через связный список (динамический размер)

Плюс — нет ограничения по ёмкости; все операции O(1). Храним указатели на голову и хвост.

```cpp
#include <stdexcept>

class Queue {
private:
    struct Node {
        int value;
        Node* next;
        explicit Node(int v) : value(v), next(nullptr) {}
    };

    Node* head = nullptr;
    Node* tail = nullptr;
    size_t count = 0;

public:
    Queue() = default;
    ~Queue() { clear(); }

    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    void push(int value) {
        Node* node = new Node(value);
        if (tail) tail->next = node;
        else head = node;
        tail = node;
        ++count;
    }

    void pop() {
        if (empty()) throw std::underflow_error("Queue is empty");
        Node* old = head;
        head = head->next;
        if (!head) tail = nullptr;  // очередь опустела
        delete old;
        --count;
    }

    int& front() {
        if (empty()) throw std::underflow_error("Queue is empty");
        return head->value;
    }

    int& back() {
        if (empty()) throw std::underflow_error("Queue is empty");
        return tail->value;
    }

    bool empty() const { return count == 0; }
    size_t size() const { return count; }

    void clear() {
        while (head) {
            Node* next = head->next;
            delete head;
            head = next;
        }
        tail = nullptr;
        count = 0;
    }
};
```

## Вариант 3. Идиоматичный C++: адаптер над std::deque

На практике «с нуля» очередь пишут редко — стандартная библиотека уже содержит контейнерный адаптер `std::queue`, по умолчанию построенный на `std::deque`. Если нужно показать именно использование, достаточно:

```cpp
#include <queue>
#include <iostream>

int main() {
    std::queue<int> q;

    q.push(1);
    q.push(2);
    q.push(3);

    std::cout << q.front() << ' ' << q.back() << '\n';  // 1 3
    q.pop();                                            // удаляет 1
    std::cout << q.front() << '\n';                     // 2

    while (!q.empty()) {
        std::cout << q.front() << ' ';
        q.pop();
    }
    // Вывод: 2 3
}
```

При желании можно своими руками повторить устройство адаптера — обёртку над `std::deque`, реализующую те же пять операций.

## Сравнение вариантов

| Реализация | push | pop | front | Память | Ограничение размера |
|---|---|---|---|---|---|
| Кольцевой массив | O(1) | O(1) | O(1) | компактная, непрерывная | фиксированное |
| Связный список | O(1) | O(1) | O(1) | на каждый элемент указатель | нет |
| `std::queue` / `std::deque` | O(1) | O(1) | O(1) | блоками | нет (кроме RAM) |

## На что обратить внимание на собеседовании/защите

1. **FIFO-инвариант**: `push` добавляет в конец, `pop` удаляет из начала — не перепутать.
2. **Проверка пустоты** перед `pop`/`front`/`back` — иначе UB.
3. **Кольцевой буфер**: правильно считать `back` как `(tail + capacity - 1) % capacity`, а не `tail - 1` (иначе отрицательный индекс).
4. **Правило трёх/пяти**: класс с владеющим `new[]` или сырыми указателями требует корректного деструктора и запрета/реализации копирования, иначе двойное освобождение.
5. **Исключительная безопасность**: в списковой версии сначала выделяем узел, потом меняем указатели — чтобы при `bad_alloc` очередь осталась консистентной.
6. **Амортизированная сложность**: у `std::deque` push/pop с обоих концов — амортизированно O(1), что и делает его стандартной основой для `std::queue`.