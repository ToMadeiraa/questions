# 172. Для структуры типа односвязный список напишите функцию вставки элемента.

## Структура односвязного списка

```cpp
struct Node {
    int data;       // полезные данные (может быть любого типа)
    Node* next;     // указатель на следующий узел
};
```

## Варианты функции вставки

В зависимости от задачи вставка может выполняться в начало списка, в конец или после заданного узла. Ниже приведены все три варианта — на практике в задании обычно требуется один из них.

### 1. Вставка в начало списка

```cpp
// Возвращает новый указатель на голову списка
Node* insertFront(Node* head, int value) {
    Node* newNode = new Node{value, head};
    return newNode;
}
```

Использование:

```cpp
Node* head = nullptr;
head = insertFront(head, 10);
head = insertFront(head, 20);  // список: 20 -> 10
```

### 2. Вставка в конец списка

```cpp
Node* insertBack(Node* head, int value) {
    Node* newNode = new Node{value, nullptr};
    if (head == nullptr) {
        return newNode;            // список был пуст
    }
    Node* cur = head;
    while (cur->next != nullptr) { // идём до последнего узла
        cur = cur->next;
    }
    cur->next = newNode;
    return head;
}
```

### 3. Вставка после заданного узла

```cpp
// prev — узел, после которого вставляем; prev != nullptr
void insertAfter(Node* prev, int value) {
    Node* newNode = new Node{value, prev->next};
    prev->next = newNode;
}
```

## Классический «учебный» вариант (вставка с сортировкой по возрастанию)

Часто в практических заданиях требуется вставка с сохранением упорядоченности:

```cpp
Node* insertSorted(Node* head, int value) {
    Node* newNode = new Node{value, nullptr};

    // Вставка в начало (пустой список или значение меньше первого)
    if (head == nullptr || value < head->data) {
        newNode->next = head;
        return newNode;
    }

    // Поиск места вставки
    Node* cur = head;
    while (cur->next != nullptr && cur->next->data < value) {
        cur = cur->next;
    }
    newNode->next = cur->next;
    cur->next = newNode;
    return head;
}
```

## Пояснения и важные моменты

- **Выделение памяти**: новый узел создаётся через `new`; при удалении списка память нужно освобождать через `delete` (иначе утечка).
- **Возврат указателя**: если вставка возможна в голову списка (в начало), функция должна возвращать новый `head`, поскольку он может измениться. Альтернатива — передавать `Node*& head` по ссылке.
- **Порядок операций**: сначала `newNode->next = prev->next`, затем `prev->next = newNode` — иначе потеряется остаток списка.
- **Пустой список** обрабатывается отдельно в вариантах 2 и 3.
- **Сложность**: вставка в начало — O(1); в конец и с сортировкой — O(n), так как требуется проход по списку.

## Пример полной программы

```cpp
#include <iostream>

struct Node {
    int data;
    Node* next;
};

Node* insertSorted(Node* head, int value) {
    Node* newNode = new Node{value, nullptr};
    if (head == nullptr || value < head->data) {
        newNode->next = head;
        return newNode;
    }
    Node* cur = head;
    while (cur->next != nullptr && cur->next->data < value) {
        cur = cur->next;
    }
    newNode->next = cur->next;
    cur->next = newNode;
    return head;
}

void print(Node* head) {
    for (Node* p = head; p; p = p->next)
        std::cout << p->data << " -> ";
    std::cout << "nullptr\n";
}

int main() {
    Node* head = nullptr;
    head = insertSorted(head, 30);
    head = insertSorted(head, 10);
    head = insertSorted(head, 20);
    print(head);  // 10 -> 20 -> 30 -> nullptr
}
```