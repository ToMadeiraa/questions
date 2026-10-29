# 165. Напишите thread-safe пул потоков.

## Что такое пул потоков

**Пул потоков** — это паттерн многопоточного программирования, при котором заранее создаётся фиксированное (или ограниченное) количество рабочих потоков. Задачи помещаются в общую очередь, а свободные потоки забирают их оттуда и выполняют. Это позволяет избежать дорогостоящего создания/уничтожения потока на каждую задачу.

## Требования к thread-safe реализации

1. **Потокобезопасная очередь задач** — доступ к ней из нескольких потоков защищён мьютексом.
2. **Синхронизация через condition_variable** — потоки не должны крутиться в busy-wait; они ждут появления задачи.
3. **Корректное завершение (shutdown)** — при разрушении пула нужно дождаться выполнения уже поставленных задач и остановить потоки.
4. **RAII** — потоки и ресурсы должны корректно освобождаться.

## Реализация (C++17)

```cpp
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <type_traits>
#include <memory>
#include <atomic>
#include <stdexcept>

class ThreadPool {
public:
    explicit ThreadPool(std::size_t threadCount = std::thread::hardware_concurrency())
        : stopping_(false)
    {
        if (threadCount == 0) threadCount = 1;
        workers_.reserve(threadCount);
        for (std::size_t i = 0; i < threadCount; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    // Запрещаем копирование/перемещение — владелец потоков должен быть уникален.
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    ~ThreadPool() {
        shutdown();
    }

    // Добавить задачу без ожидания результата.
    template <class F, class... Args>
    void enqueue(F&& f, Args&&... args) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }
            tasks_.emplace(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        }
        cv_.notify_one();
    }

    // Добавить задачу и получить future для результата.
    template <class F, class... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<ReturnType> result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                throw std::runtime_error("submit on stopped ThreadPool");
            }
            tasks_.emplace([task] { (*task)(); });
        }
        cv_.notify_one();
        return result;
    }

    // Корректно остановить пул: доработать очередь и join'нуть потоки.
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) return;
            stopping_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
        workers_.clear();
    }

private:
    void workerLoop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] {
                    return stopping_ || !tasks_.empty();
                });
                // Если остановка инициирована, но задачи ещё есть —
                // продолжаем их выполнять (graceful shutdown).
                if (stopping_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task(); // выполняем вне блокировки
        }
    }

    std::vector<std::thread>           workers_;
    std::queue<std::function<void()>>  tasks_;
    std::mutex                         mutex_;
    std::condition_variable            cv_;
    bool                               stopping_;
};
```

## Ключевые моменты реализации

### 1. Мьютекс + condition_variable
Все обращения к `tasks_` и флагу `stopping_` защищены `mutex_`. Потоки не «спят в цикле», а ждут на `cv_.wait(...)` с предикатом — это исключает ложные пробуждения.

### 2. Предикат в `wait`
```cpp
cv_.wait(lock, [this]{ return stopping_ || !tasks_.empty(); });
```
Поток просыпается только когда есть задача или инициирована остановка. Это устраняет классическую ошибку busy-wait.

### 3. Выполнение задачи вне мьютекса
Задача копируется/перемещается в локальную переменную под блокировкой, а затем выполняется уже **после** её снятия — иначе один долгий таск заблокировал бы весь пул.

### 4. `enqueue` vs `submit`
- `enqueue` — «выстрелил и забыл», возвращает `void`.
- `submit` — обёртывает функцию в `std::packaged_task` и возвращает `std::future<ReturnType>`, чтобы дождаться результата или получить исключение.

Обратите внимание: `packaged_task` **некопируемый**, поэтому храним его через `std::shared_ptr` и кладём в очередь лямбду-обёртку.

### 5. Корректный `shutdown`
- Под мьютексом выставляем `stopping_ = true`.
- `notify_all()` будит все потоки.
- Каждый поток в цикле доделывает оставшиеся задачи (`if (stopping_ && tasks_.empty()) return;` — проверка после `wait`), затем выходит.
- `join` в деструкторе гарантирует, что к моменту возврата из `~ThreadPool` все рабочие потоки завершены, а значит, никакой `this` не «утечёт».

### 6. Запрет копирования и перемещения
Класс владеет потоками и мьютексом — копировать/перемещать его нельзя, иначе получим UB. Удаляем соответствующие операции явно.

### 7. Обработка исключений
- `submit` прокидывает исключение из задачи через `std::future` (это делает `packaged_task`).
- В `enqueue` исключение, брошенное задачей, «потеряется» — при необходимости оберните таск в `try/catch`.

## Пример использования

```cpp
#include <iostream>
#include <chrono>

int main() {
    ThreadPool pool(4);

    // Асинхронные задачи с результатом
    auto f1 = pool.submit([](int x) { return x * x; }, 7);
    auto f2 = pool.submit([] { 
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return std::string("done");
    });

    pool.enqueue([] { std::cout << "fire-and-forget\n"; });

    std::cout << "f1 = " << f1.get() << "\n"; // 49
    std::cout << "f2 = " << f2.get() << "\n"; // done

    pool.shutdown(); // дождётся всех задач
    return 0;
}
```

## Возможные улучшения

- **Динамическое масштабирование** — добавлять/убирать потоки по нагрузке.
- **Приоритеты задач** — заменить `std::queue` на `std::priority_queue`.
- **`std::jthread` (C++20)** — автоматический join и `stop_token` для кооперативной отмены.
- **Ограничение размера очереди** — backpressure, если задачи ставятся быстрее, чем выполняются.
- **`std::move_only_function` (C++23)** — поддержка move-only callable без `shared_ptr`.
- **Пул через `std::latch`/`std::barrier`** — для пакетной обработки.

## Типичные ошибки, которых удалось избежать

| Ошибка | Как решено |
|---|---|
| Busy-wait в цикле потока | `condition_variable::wait` с предикатом |
| Выполнение задачи под мьютексом | Задача извлекается, блокировка снимается, затем вызов |
| Потеря задач при shutdown | Graceful-режим: сначала доделываем очередь, потом выходим |
| Копирование пула → двойной join | Удалены copy/move операции |
| `packaged_task` в `std::function` (некопируемый) | Обёрнут в `std::shared_ptr` |
| Гонка при проверке `stopping_` | Проверка и установка под одним мьютексом |