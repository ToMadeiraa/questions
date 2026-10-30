# 58. Как реализовать синхронизацию в задаче producer-consumer?

## 1. Постановка задачи

Есть разделяемый буфер (ограниченный или нет), \(N\) потоков-поставщиков кладут в него элементы, \(M\) потоков-потребителей забирают. Синхронизировать нужно три вещи:

1. **Взаимное исключение** — доступ к самому буферу и его индексам/счётчикам (иначе гонка данных и порча структуры).
2. **Ожидание условия** — потребитель должен спать, пока буфер пуст, поставщик — пока полон, причём без busy-wait (иначе сожжённое CPU и деградация).
3. **Согласованное завершение** — потоки должны уметь корректно и не «залипнуть» навсегда.

Плюс невидимая часть: не должно быть **потерянных пробуждений** (lost wakeup) и должна быть корректная обработка **ложных пробуждений** (spurious wakeup).

## 2. Инварианты корректного решения

Их полезно держать в голове, они объясняют почти все детали ниже:

1. **Предикат проверяется под тем же мьютексом**, который защищает состояние.
2. **Между проверкой предиката и засыпанием нет окна** — именно это и обеспечивает `condition_variable`: она атомарно отпускает мьютекс и засыпает.
3. **Пробуждение — это только повод перепроверить предикат**, а не гарантия, что условие истинно (мог «украсть» другой поток, могло быть ложное пробуждение).
4. **Состояние меняется под мьютексом, `notify` идёт после изменения** (можно как под локом, так и после разблокировки).
5. **Завершение — это отдельное состояние** (`closed`/`done`), а не «просто перестали класть элементы».

## 3. Каноническое решение: `std::mutex` + две `condition_variable`

Базовая и самая переносимая схема — ограниченная очередь с двумя условиями: «не пусто» и «не полон».

```cpp
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>
#include <cstddef>

template <class T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : cap_(capacity) {}

    // ---- producer ----
    bool push(T value) {
        std::unique_lock lk(m_);
        // ждём, пока есть место ИЛИ пока не закрылись
        not_full_.wait(lk, [this] { return closed_ || q_.size() < cap_; });
        if (closed_) return false;

        q_.push(std::move(value));

        lk.unlock();                 // можно и под локом — см. ниже
        not_empty_.notify_one();     // разбудили ровно одного потребителя
        return true;
    }

    // ---- consumer ----
    std::optional<T> pop() {
        std::unique_lock lk(m_);
        not_empty_.wait(lk, [this] { return closed_ || !q_.empty(); });

        if (q_.empty()) return std::nullopt;   // закрыто и всё вычерпано

        T v = std::move(q_.front());
        q_.pop();

        lk.unlock();
        not_full_.notify_one();
        return v;
    }

    // ---- корректное завершение ----
    void close() {
        {
            std::lock_guard lk(m_);
            closed_ = true;
        }
        not_empty_.notify_all();   // разбудить всех, кто ждёт
        not_full_.notify_all();
    }

private:
    std::mutex              m_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::queue<T>           q_;
    std::size_t             cap_;
    bool                    closed_ = false;
};
```

### Почему именно так

**`std::unique_lock`, а не `lock_guard`.** `wait` должна уметь отпустить мьютекс на время сна и захватить обратно при пробуждении. `lock_guard` этого не умеет, `unique_lock` — умеет.

**`wait(lk, pred)` вместо `wait(lk)`.** Форма с предикатом эквивалентна

```cpp
while (!pred()) cv.wait(lk);
```

и закрывает сразу две проблемы: ложные пробуждения (POSIX разрешает их, стандарт C++ тоже) и «украденные» пробуждения (пока поток просыпался и шёл за мьютексом, другой поток уже забрал элемент). Если писать `if (q_.empty()) cv.wait(lk);` — это классический баг, который ловится раз в неделю на проде.

**Две разные `condition_variable`, а не одна.** У пустоты и полноты разные предикаты. Если держать один condvar и звать `notify_one`, возможна ситуация: буфер был полон, потребитель забрал элемент и разбудил... другого потребителя, который увидел пустой буфер и снова уснул. Поставщик при этом не проснулся — прогресс встал (до следующего события). С двумя condvar адресация точная: `not_empty_` будят только потребителей, `not_full_` — только поставщиков. Альтернатива — один condvar и `notify_all`, но это лишние пробуждения и «thundering herd».

**`notify_one`, а не `notify_all`.** Каждый `push` добавляет ровно один элемент, значит будить достаточно одного потребителя; каждый `pop` освобождает ровно один слот — достаточно одного поставщика. `notify_all` нужен только там, где меняется «глобальное» состояние — при `close()`.

**`notify` под локом или после `unlock`?** Корректны оба варианта. После `unlock` обычно чуть быстрее: разбуженный поток не встаёт сразу в очередь на мьютекс («hurry up and wait»), а заодно исчезает лишний контекст-свитч. Под локом — проще рассуждать и на некоторых платформах предсказуемее по латентности. Главное — состояние уже изменено до `notify`, а предикат всё равно перепроверяется под мьютексом, поэтому «потерять» пробуждение нельзя: если в момент `notify` ждущих не было, то любой будущий ждущий проверит предикат уже после изменения состояния и не заснёт.

**Обработка элемента — вне лока.** Из-под мьютекса забираем только указатель/значение, а всю полезную работу делаем после `unlock`. Иначе критическая секция растягивается на время обработки и параллелизм убивается.

### Пример использования

```cpp
#include <atomic>
#include <thread>
#include <vector>

int main() {
    BoundedQueue<int> q(8);          // буфер на 8 элементов -> backpressure

    std::vector<std::jthread> producers;
    for (int p = 0; p < 2; ++p) {
        producers.emplace_back([&q, p] {
            for (int i = 0; i < 1000; ++i)
                if (!q.push(p * 1000 + i)) break;
        });
    }

    std::atomic<int> consumed{0};
    std::vector<std::jthread> consumers;
    for (int c = 0; c < 3; ++c) {
        consumers.emplace_back([&] {
            while (auto v = q.pop()) {          // optional -> bool = has_value
                // ... обработка вне критической секции ...
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : producers) t.join();
    q.close();                        // потребители дочитают остаток и выйдут
    // jthread'ы consumer'ов джойнятся в деструкторах
}
```

## 4. Завершение работы: три подхода

1. **Флаг `closed_` + `notify_all`** (как выше). Плюс: работает для любого числа потоков, потребители дочерпывают остаток. Минус: флаг надо аккуратно защищать тем же мьютексом.
2. **Poison pill** — кладём в очередь специальные маркеры «конец». Плюс: не нужен отдельный флаг и отдельный `notify_all`. Минус: нужно ровно \(M\) маркеров (по числу потребителей), легко ошибиться; при нескольких очередях/маршрутизации маркер может уехать не тому потребителю.
3. **`std::stop_token` (C++20)** — `condition_variable_any::wait(lk, stop_token, pred)` умеет прерывать ожидание по внешнему запросу. Удобно, когда отменой управляет кто-то снаружи.

Важно: `close()` вызывается **после** join'а производителей, а разрушение очереди — **после** join'а всех потоков. Иначе `condition_variable`/`mutex` уничтожаются под ногами у ждущего потока — это UB, который иногда «просто работает».

## 5. Вариант на семафорах (C++20, `<semaphore>`)

Идея: два семафора считают ресурсы — свободные слоты и готовые элементы. Мьютекс остаётся только на саму вставку/извлечение (защита контейнера).

```cpp
#include <semaphore>
#include <mutex>
#include <queue>

template <class T>
class SemQueue {
public:
    explicit SemQueue(std::ptrdiff_t cap) : free_(cap), items_(0) {}

    void push(T v) {
        free_.acquire();                 // ждём свободный слот
        {
            std::lock_guard lk(m_);
            q_.push(std::move(v));
        }
        items_.release();                // сообщаем: появился элемент
    }

    T pop() {
        items_.acquire();                // ждём элемент
        std::unique_lock lk(m_);
        T v = std::move(q_.front());
        q_.pop();
        lk.unlock();
        free_.release();                 // сообщаем: освободился слот
        return v;
    }

private:
    std::mutex              m_;
    std::queue<T>           q_;
    std::counting_semaphore<> free_, items_;
};
```

Плюсы: нет предикатов и связанных с ними ошибок, нет ложных пробуждений, есть `try_acquire_for`/`try_acquire_until` (таймауты «из коробки»). Минусы: нет встроенного «закрытия» — при завершении придётся отдельно `release`-ить семафоры, чтобы никто не залип; при исключении между `acquire` и `release` слот «утечёт» (нужен RAII-страж). Порядок захвата здесь безопасен: каждый поток берёт не более одного семафора, циклов ожидания нет.

## 6. `std::atomic::wait` / `notify` (C++20)

`atomic<T>::wait(old)` блокирует поток, пока значение не изменится с `old`, `notify_one/all` будят. Внутри — futex на Linux, `WaitOnAddress` на Windows, то есть без мьютекса и без спин-лока. Это хороший строительный блок для флагов и счётчиков:

```cpp
std::atomic<int> count{0};

// consumer
int c;
while ((c = count.load(std::memory_order_acquire)) == 0)
    count.wait(0, std::memory_order_acquire);

// producer
count.fetch_add(1, std::memory_order_release);
count.notify_one();
```

Но сам по себе счётчик не передаёт элемент: для очереди всё равно нужен либо мьютекс, либо lock-free структура. Часто используют как «дешёвый шлюз»: сначала спин/`wait` на атомарном флаге, а уже потом мьютекс.

## 7. Lock-free SPSC-кольцо на атомиках

Если производитель ровно один и потребитель ровно один, мьютекс не нужен вообще — достаточно кольцевого буфера и двух индексов с правильными `memory_order`.

```cpp
#include <atomic>
#include <array>

template <class T, std::size_t N>   // N — степень двойки
class SpscRing {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");

    alignas(64) std::atomic<std::size_t> head_{0};  // пишет producer
    alignas(64) std::atomic<std::size_t> tail_{0};  // пишет consumer
    alignas(64) std::array<T, N>         buf_{};

public:
    bool try_push(const T& v) {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        if (h - t == N) return false;               // полно

        buf_[h & (N - 1)] = v;                       // данные
        head_.store(h + 1, std::memory_order_release); // публикуем
        return true;
    }

    bool try_pop(T& out) {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        const std::size_t h = head_.load(std::memory_order_acquire);
        if (t == h) return false;                   // пусто

        out = buf_[t & (N - 1)];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }
};
```

Почему порядки именно такие:

- `head_`/`tail_` — это «публикация» данных. Producer пишет в `buf_`, затем **release**-store в `head_`; consumer делает **acquire**-load `head_` и только потом читает `buf_` — получается happens-before, данные видны.
- Симметрично consumer читает `buf_`, затем release-store `tail_`, а producer acquire-load'ит `tail_` перед перезаписью слота — старые данные не «затрут» ещё не прочитанные.
- Свой индекс читается `relaxed` — его пишет только этот же поток.
- `h - t` на `size_t` корректно работает при переполнении (беззнаковая арифметика по модулю \(2^{64}\)), пока разница меньше \(2^{63}\).
- `alignas(64)` разносит `head_` и `tail_` по разным кэш-линиям — иначе получим **false sharing** и просадку в разы.

Это **не** универсальное решение: для нескольких производителей/потребителей нужна более сложная схема (например, bounded MPMC-очередь Выюкова с per-cell sequence number или Michael–Scott), и написать её правильно с первого раза почти нереально — лучше брать готовую библиотеку.

Блокирующий вариант поверх lock-free: сначала несколько итераций спина с `_mm_pause()`/`__builtin_ia32_pause()`, потом засыпание на семафоре/condvar/`atomic::wait`. Гибрид «spin then block» — стандартный приём для низкой латентности.

## 8. То же самое на «голом» C

### POSIX (`pthreads`)

```c
typedef struct {
    int              *buf;
    size_t            cap, head, tail, count;
    pthread_mutex_t   mtx;
    pthread_cond_t    not_empty;
    pthread_cond_t    not_full;
    int               closed;
} pcq_t;

int pcq_push(pcq_t *q, int v) {
    pthread_mutex_lock(&q->mtx);
    while (!q->closed && q->count == q->cap)
        pthread_cond_wait(&q->not_full, &q->mtx);
    if (q->closed) { pthread_mutex_unlock(&q->mtx); return -1; }

    q->buf[q->tail] = v;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;

    pthread_mutex_unlock(&q->mtx);       // сначала состояние, потом сигнал
    pthread_cond_signal(&q->not_empty);
    return 0;
}

int pcq_pop(pcq_t *q, int *out) {
    pthread_mutex_lock(&q->mtx);
    while (!q->closed && q->count == 0)
        pthread_cond_wait(&q->not_empty, &q->mtx);
    if (q->count == 0) {                 // closed и пусто
        pthread_mutex_unlock(&q->mtx);
        return -1;
    }

    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;

    pthread_mutex_unlock(&q->mtx);
    pthread_cond_signal(&q->not_full);
    return 0;
}
```

Здесь та же логика: `while`, а не `if`; две переменные условия; изменение под мьютексом; сигнал после изменения. На завершение — `closed = 1; pthread_cond_broadcast(&q->not_empty); pthread_cond_broadcast(&q->not_full);`.

### POSIX-семафоры

То же, что C++20-вариант: `sem_init(&free, 0, cap)`, `sem_init(&items, 0, 0)`, `sem_wait`/`sem_post`, мьютекс только на контейнер.

### Win32

| POSIX | Win32 |
|---|---|
| `pthread_mutex_t` | `SRWLOCK` + `AcquireSRWLockExclusive` / `CRITICAL_SECTION` |
| `pthread_cond_t` | `CONDITION_VARIABLE` + `SleepConditionVariableSRW` |
| `sem_t` | `Semaphore` / `CreateSemaphore` |
| `pthread_cond_broadcast` | `WakeAllConditionVariable` |

```c
SRWLOCK           lock = SRWLOCK_INIT;
CONDITION_VARIABLE not_empty = CONDITION_VARIABLE_INIT;

AcquireSRWLockExclusive(&lock);
while (!closed && count == 0)
    SleepConditionVariableSRW(&not_empty, &lock, INFINITE, 0);
/* ... забрали элемент ... */
ReleaseSRWLockExclusive(&lock);
WakeConditionVariable(&not_empty);
```

Ещё есть C11 `<threads.h>` (`mtx_t`, `cnd_t`, `thrd_t`) — переносимая обёртка, но качество реализаций разное.

## 9. Сравнение подходов

| Подход | Плюсы | Минусы | Когда брать |
|---|---|---|---|
| `mutex` + 2 `condition_variable` | переносимо, гибко, понятно | многословно, легко ошибиться в предикате | по умолчанию, 95% задач |
| семафоры (C++20) | короче, таймауты, нет ложных пробуждений | нет «закрытия» из коробки, нужен RAII для слотов | когда нужен простой счётный ресурс |
| `atomic::wait/notify` | без мьютекса, эффективно | только флаг/счётчик, не очередь | event/флаг, гибридный spin |
| lock-free SPSC-кольцо | нет блокировок, предсказуемая латентность | только 1P1C, сложно расширять | аудио, HFT, драйверы, сетевое кольцо |
| MPMC lock-free (Vyukov) | масштабируется | очень легко получить UB, ABA | только осознанно или из библиотеки |
| готовая библиотека | проверено, быстро | зависимость | продакшн по умолчанию |

Готовые решения, которые обычно и стоит брать: `moodycamel::BlockingConcurrentQueue`, `boost::lockfree::spsc_queue`, `tbb::concurrent_bounded_queue`, `folly::ProducerConsumerQueue`, идея LMAX Disruptor (ring buffer + sequence barriers).

## 10. Типичные ошибки

1. **`if` вместо `while`/предиката** при `wait` — ложное пробуждение или украденный элемент, и поток работает с пустой очередью.
2. **Проверка предиката без мьютекса** («я же быстро посмотрел») — окно между проверкой и `wait`, потерянное пробуждение, вечный сон.
3. **Один condvar на два предиката + `notify_one`** — будим «не того» ждущего, прогресс останавливается.
4. **`notify` до изменения состояния** — ждущий просыпается, видит старый предикат, снова засыпает, и больше его никто не разбудит.
5. **`notify_all` на каждый `push`** — thundering herd, лишние свитчи, падение throughput.
6. **Долгая работа под мьютексом** — параллелизм превращается в последовательный.
7. **Busy-wait на `atomic<bool>`** без `wait`/паузы — 100% CPU на ядро и деградация соседей.
8. **`volatile` вместо `std::atomic`** — в C++ это не даёт ни атомарности, ни ordering.
9. **Разрушение очереди до join'а потоков** — UB, «иногда работает».
10. **Забыли разбудить ждущих при `close()`** — вечное зависание на shutdown.
11. **Неограниченная очередь** — нет backpressure, OOM при перегрузке потребителей.
12. **False sharing** `head`/`tail` в одной кэш-линии — падение производительности в разы.
13. **Starvation** из-за `notify_one` и нечестного `std::mutex`: один потребитель может монополизировать очередь. Если важна честность — семафоры (на Linux они ближе к FIFO) или тикет-лок.
14. **Исключение при `push`** (например, `bad_alloc`): `unique_lock` разблокирует мьютекс, состояние контейнера останется целым (сильная гарантия `std::queue::push`), но элемент может быть потерян — если это критично, предусмотрите обработку.
15. **`condition_variable` + несколько мьютексов** без единого порядка захвата — потенциальный deadlock.

## 11. Производительность: что реально влияет

- **Размер буфера.** Маленький — потоки часто спят и просыпаются (дорого, ~1–2 мкс на пробуждение); большой — растёт латентность и потребление памяти, слабеет backpressure. Часто оптимум — десятки–сотни элементов, подбирается замером.
- **Батчинг.** Забирать/класть пачками по 8–32 элемента за один захват лока — резко снижает накладные расходы на синхронизацию.
- **Длина критической секции.** Только манипуляции с контейнером; всё остальное — снаружи.
- **`notify` после `unlock`**, спин перед засыпанием, `alignas(64)` для горячих счётчиков.
- **Шардирование.** Вместо одной очереди на всех — очередь на поток-потребитель (или work stealing), тогда конкуренция за мьютекс падает.
- **Измерять, а не гадать:** `perf stat`, `perf c2c` (false sharing), замеры latency-перцентилей, а не только среднего.

## 12. Тестирование

- **ThreadSanitizer** (`-fsanitize=thread`) и **helgrind** — обязательный минимум, ловят гонки и неправильное использование condvar.
- **Стресс-тест**: много producer'ов/consumer'ов, маленький буфер, `close()` в случайный момент, проверка, что ни один элемент не потерян и не обработан дважды.
- **Проверка инвариантов** под локом в отладочной сборке: `count == (tail - head + cap) % cap`, `count <= cap`.
- **Тест на зависание**: `close()` при полном буфере, при пустом, одновременно с активными push/pop.

## 13. Чеклист

- [ ] состояние буфера защищено одним мьютексом;
- [ ] ожидание — через `wait` с предикатом (или семафор), без busy-wait;
- [ ] два разных условия — две `condition_variable` (или `notify_all` на одной);
- [ ] `notify` идёт после изменения состояния, `notify_one` там, где достаточно одного;
- [ ] обработка элемента — вне критической секции;
- [ ] есть явное состояние `closed` + `notify_all` при завершении;
- [ ] `close()` после join'а производителей, разрушение — после join'а всех;
- [ ] буфер ограничен (backpressure) либо это осознанное решение;
- [ ] прогнано под TSan и стресс-тестом;
- [ ] для 1P1C рассмотрен lock-free SPSC, для продакшна — готовая библиотека.