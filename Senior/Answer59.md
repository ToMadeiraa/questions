# 59. Как синхронизироваться между различными процессами?

## Почему нельзя просто взять `std::mutex`

Стандартная библиотека C++ (и C) синхронизирует только **потоки внутри одного процесса**: `std::mutex`, `std::condition_variable`, `std::shared_mutex`, `std::atomic`-фолбэки, `thread_local` — всё это живёт в адресном пространстве процесса. У разных процессов свои адресные пространства, свои таблицы дескрипторов, свои кучи. Поэтому:

* `std::mutex`, положенный в разделяемую память, **не работает** — это неопределённое поведение (внутри него указатели на process-local структуры, а на некоторых платформах — futex с `FUTEX_PRIVATE_FLAG`);
* `std::condition_variable` — тем более;
* `std::shared_ptr` с контрольным блоком в shared memory — нельзя (счётчик не атомарен между процессами);
* `volatile` — это не синхронизация, а отключение оптимизаций.

Синхронизация между процессами всегда опирается на **объект, который существует вне адресного пространства**: либо это объект ядра (семафор, мьютекс, событие, файловая блокировка, pipe, сокет, очередь сообщений), либо это общая память, в которой лежат примитивы, корректно работающие в shared-режиме (process-shared pthread-объекты, атомики, futex).

---

## Три принципиальных подхода

| Подход | Что это | Когда применять |
|---|---|---|
| **Объекты ядра с именами** | Именованные семафоры, мьютексы, события, очереди сообщений, pipe/socket | Простое сигнализирование, взаимное исключение, передача небольших сообщений |
| **Разделяемая память + примитивы в ней** | `shm_open`/`mmap`/`CreateFileMapping` + process-shared mutex/semaphore/atomics | Большие объёмы данных, минимальные задержки, zero-copy |
| **Передача сообщений** | pipe, UNIX-сокеты, TCP, POSIX/SysV MQ, D-Bus, gRPC | Слабая связность, разные хосты, простота и надёжность |

---

## 1. Разделяемая память

### POSIX

```cpp
int fd = shm_open("/myapp_shm", O_CREAT | O_RDWR, 0600);
if (fd == -1) { /* ошибка */ }
if (ftruncate(fd, sizeof(Shared)) == -1) { /* ошибка */ }

void* p = mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE,
               MAP_SHARED, fd, 0);
close(fd);                       // дескриптор больше не нужен, отображение живёт
Shared* s = static_cast<Shared*>(p);

// ...
munmap(p, sizeof(Shared));
shm_unlink("/myapp_shm");        // имя удаляется; объект живёт, пока есть отображения
```

На Linux это файл в `/dev/shm` (tmpfs). Классические грабли: **tmpfs ограничен** (обычно половина RAM), и при обращении к странице, когда места нет, процесс получает `SIGBUS`, а не `ENOMEM`.

Альтернативы: `shmget/shmat` (SysV, нет автоочистки, чистится через `ipcrm`) или `mmap` обычного файла (`MAP_SHARED`) — даёт персистентность и `msync`, но медленнее и требует ФС.

### Windows

```cpp
HANDLE hMap = CreateFileMappingW(
    INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
    0, sizeof(Shared), L"Local\\MyAppShm");

bool creator = (GetLastError() != ERROR_ALREADY_EXISTS);

Shared* s = static_cast<Shared*>(
    MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));

// ...
UnmapViewOfFile(s);
CloseHandle(hMap);   // объект исчезает, когда закрыт последний handle
```

`INVALID_HANDLE_VALUE` — сегмент в pagefile (не переживает перезагрузку); можно передать handle реального файла. Секции в Windows **автоматически освобождаются**, когда закрыт последний дескриптор, — это большое преимущество перед POSIX.

### Наследование через `fork`

Если процессы — родственники (fork), имена не нужны:

```cpp
Shared* s = static_cast<Shared*>(
    mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE,
         MAP_SHARED | MAP_ANONYMOUS, -1, 0));
pid_t pid = fork();   // оба процесса видят одну и ту же память
```

На Windows аналог — `DuplicateHandle`/наследование handle при `CreateProcess(..., bInheritHandles = TRUE, ...)`.

### Что кладут в shared memory

Структура должна быть **одинаково разложена** во всех процессах:

* только POD/trivially copyable типы, фиксированные размеры (`int32_t`, `uint64_t`, а не `long`);
* **никаких указателей** — только смещения (`offset`/`index`), иначе адрес одного процесса бессмысленен в другом;
* фиксированное выравнивание (`alignas`), явные `#pragma pack`/проверка `sizeof` и `offsetof`;
* поле версии/размера сегмента — чтобы старый и новый бинарник не «съели» друг друга;
* один и тот же компилятор/архитектура/ABI (иначе — только через сериализацию).

---

## 2. Примитивы, живущие в разделяемой памяти (POSIX)

Это самый «нативный» путь для shared memory.

### Process-shared mutex

```cpp
pthread_mutexattr_t ma;
pthread_mutexattr_init(&ma);
pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
pthread_mutexattr_setrobust(&ma, PTHREAD_MUTEX_ROBUST);   // см. ниже
// pthread_mutexattr_setprotocol(&ma, PTHREAD_PRIO_INHERIT); // для RT
pthread_mutex_init(&s->mtx, &ma);
```

Мьютекс обязан лежать **в разделяемой памяти**. Атрибут `PTHREAD_PROCESS_SHARED` говорит glibc не использовать приватные оптимизации.

Важно: `pthread_spinlock_t` **не поддерживает** `PTHREAD_PROCESS_SHARED` — для спинлока в shared memory берут `std::atomic_flag`.

### Process-shared condition variable

```cpp
pthread_condattr_t ca;
pthread_condattr_init(&ca);
pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED);
pthread_cond_init(&s->cv, &ca);
```

Обязательное условие: condvar и связанный с ней мьютекс — в одной и той же разделяемой памяти.

Ограничения, о которых надо знать:

* **condvar не бывает robust**. Если процесс умер, находясь в `pthread_cond_wait`, внутреннее состояние (счётчик ожидающих / futex-word) может остаться несогласованным — возможны потерянные пробуждения и вечное зависание. Для отказоустойчивых схем condvar в shared memory — плохой выбор;
* на macOS нет robust-мьютексов, а `sem_init` с `pshared=1` не поддерживается вообще (`ENOSYS`); `sem_open` — поддерживается;
* в glibc `pthread_cond_wait` может вернуть `EOWNERDEAD`, если владелец мьютекса умер, пока мы ждали.

### Именованные и безымянные семафоры

```cpp
// именованный: годится и для несвязанных процессов
sem_t* sem = sem_open("/myapp_sem", O_CREAT, 0600, 0);
sem_wait(sem);      // P
sem_post(sem);      // V
sem_close(sem);
sem_unlink("/myapp_sem");   // удалить имя

// безымянный: кладётся в shared memory, pshared = 1
sem_init(&s->sem, 1, 0);
```

Именованные семафоры — самый простой кросспроцессный механизм «разбудить/дождаться». Но у них **нет владельца**: если процесс упал между `sem_wait` и `sem_post`, счётчик «протёк» навсегда. Таймаут (`sem_timedwait`) — обязательная страховка.

Также поддерживаются process-shared `pthread_rwlock_t` и `pthread_barrier_t` (аналогичные `*attr_setpshared`), но у них та же беда с отсутствием robustness.

### Futex (Linux) и eventfd

`futex(&word, FUTEX_WAIT, expected, ...)` работает и для shared memory — **главное не передавать `FUTEX_PRIVATE_FLAG`**. На этом строятся самодельные мьютексы/очереди. `eventfd(0, 0)` (без `EFD_SEMAPHORE`/`EFD_CLOEXEC` нюансы) даёт лёгкий счётчик-уведомление, который можно передать другому процессу через `SCM_RIGHTS` (UNIX-сокет) или унаследовать.

---

## 3. Именованные объекты ядра

### POSIX

Уже рассмотрены именованные семафоры. Дополнительно:

* **POSIX message queues**: `mq_open("/q", O_CREAT|O_RDWR, 0600, &attr)`, `mq_send`/`mq_receive`/`mq_timedreceive`, `mq_notify` (асинхронное уведомление сигналом или потоком). Очередь живёт в ядре, сообщения копируются — раскладка структур не важна, но есть лимиты (`/proc/sys/fs/mqueue/*`).
* **SysV queues**: `msgget/ftok/msgsnd/msgrcv` — старый, но всё ещё встречается API.
* **FIFO (named pipe)**: `mkfifo("/tmp/myfifo", 0600)`, затем `open`/`read`/`write`. Блокирующая передача с ядерной буферизацией, без shared layout.
* **UNIX domain sockets**: `socket(AF_UNIX, SOCK_STREAM/SOCK_SEQPACKET/DGRAM)`, `bind` на путь или абстрактное имя (`sun_path[0] == '\0'` на Linux). Умеют передавать дескрипторы (`SCM_RIGHTS`) и credentials — часто лучший выбор для «управляющего» канала.
* **Сигналы**: для синхронизации плохи (коалесцируются, нет очереди, обработчик ограничен async-signal-safe вызовами). Если очень надо — отдельный поток с `sigwait`/`signalfd`, real-time сигналы `SIGRTMIN+n`.

### Windows

```cpp
HANDLE hMtx  = CreateMutexW(nullptr, FALSE, L"Local\\MyAppMutex");
HANDLE hEvt  = CreateEventW(nullptr, FALSE, FALSE, L"Local\\MyAppEvent"); // auto-reset
HANDLE hSem  = CreateSemaphoreW(nullptr, 0, 100, L"Local\\MyAppSem");

DWORD r = WaitForSingleObject(hMtx, INFINITE);
if (r == WAIT_ABANDONED) {
    // предыдущий владелец умер, не отпустив мьютекс;
    // мьютекс теперь НАШ, состояние данных надо проверить/починить
}
// ... критическая секция ...
ReleaseMutex(hMtx);
```

Ключевые моменты:

* **Мьютекс** Windows — с владельцем и рекурсией, при смерти владельца даёт `WAIT_ABANDONED` (это и есть аналог robust-мьютекса). Обязательно `ReleaseMutex`.
* **Событие** (`Event`) — без владельца: `SetEvent`/`ResetEvent`, auto-reset «съедает» сигнал одним ожидающим. `WAIT_ABANDONED` для событий не бывает, но если процесс, который должен был сделать `SetEvent`, упал — ждущие повиснут, поэтому нужны таймауты.
* **Семафор** — счётчик; при падении владельца «единица» теряется навсегда (как в POSIX).
* `WaitForMultipleObjects` — максимум `MAXIMUM_WAIT_OBJECTS` (64) объектов; `WaitForMultipleObjectsEx` с `MWMO_ALERTABLE`/IO.
* **Пространства имён**: `Local\` — в пределах сессии (по умолчанию для Terminal Services), `Global\` — глобально, но требует привилегии `SeCreateGlobalPrivilege` (обычно админ). Голое имя без префикса — сессионное.
* **Безопасность**: объект создаётся с `SECURITY_ATTRIBUTES`/дескриптором; иначе другой пользователь может открыть его по имени.
* Внутрипроцессные `CRITICAL_SECTION` и `SRWLOCK` — **не** кросспроцессные, как и `CONDITION_VARIABLE`. Для кросспроцессной «condvar» используют auto-reset event или семафор.

---

## 4. Файловые блокировки

Когда «разделяемый ресурс» — это файл (или нужна блокировка, переживающая процесс).

### POSIX

```cpp
int fd = open("/var/lock/myapp.lock", O_RDWR | O_CREAT, 0600);

struct flock fl{};
fl.l_type   = F_WRLCK;    // F_RDLCK / F_WRLCK / F_UNLCK
fl.l_whence = SEEK_SET;
fl.l_start  = 0;
fl.l_len    = 0;          // 0 = до конца файла

fcntl(fd, F_SETLK, &fl);          // неблокирующая: EACCES/EAGAIN — занято
// fcntl(fd, F_SETLKW, &fl);      // блокирующая

fl.l_type = F_UNLCK;
fcntl(fd, F_SETLK, &fl);
```

Тонкости:

* `fcntl`-блокировки **привязаны к процессу**, а не к дескриптору: закрытие *любого* fd на этот файл снимает все блокировки процесса. В многопоточной программе это классический источник багов.
* `flock` (BSD) привязан к *open file description*: наследуется через `fork`, но не через `dup`-независимые `open`. Обычно удобнее.
* **OFD locks** (`F_OFD_SETLK`, Linux ≥ 3.15) — привязаны к open file description и работают как POSIX-семантика, но без «эффекта закрытия любого fd».
* На Linux блокировки **advisory** (кооперативные): их видят только те, кто тоже вызывает `fcntl`/`flock`.
* На NFS `flock` может эмулироваться через `fcntl`, надёжность зависит от сервера; для distributed-сценариев это плохая идея.

### Windows

`LockFileEx`/`UnlockFileEx` (или `LockFile`) — блокировки **mandatory**: другая программа получит `ERROR_LOCK_VIOLATION` при попытке чтения/записи заблокированного диапазона. Привязаны к handle и снимаются при закрытии handle или завершении процесса.

---

## 5. Атомики и lock-free в разделяемой памяти

Это работает, но с жёсткими оговорками.

```cpp
struct Shared {
    alignas(64) std::atomic<uint64_t> head{0};
    alignas(64) std::atomic<uint64_t> tail{0};
    alignas(64) std::atomic<uint32_t> buf[CAP];
    alignas(16) std::atomic<int>      state{0};   // 0 = не инициализировано
};
```

Правила:

1. **Только lock-free операции.** Если `std::atomic<T>` не lock-free, libatomic может взять process-local лок — и синхронизации между процессами не будет. Проверяйте `std::atomic<T>::is_always_lock_free` (C++17) или `is_lock_free()` в рантайме и отказывайтесь работать, если false.
2. Стандарт C++ напрямую межпроцессные атомики не описывает, но в `[atomics.lockfree]` есть примечание: lock-free операции должны быть **address-free** (не использовать глобальную таблицу локов по адресу) — именно это делает их пригодными для shared memory.
3. **Модель памяти сохраняется**: `memory_order_relaxed/acquire/release` работают так же, как между потоками. Атомик в shared memory — это обычная физическая память, когерентность кэшей железная.
4. `std::atomic_ref<T>` (C++20) позволяет навесить атомарность на обычное поле POD-структуры, не ломая её C-совместимую раскладку.
5. **Выравнивание**: атомик должен быть выровнен по своему размеру, иначе возможны torn-чтения; `alignas` обязателен, плюс `alignas(64)` против false sharing.
6. `std::atomic<T>::wait/notify_one/notify_all` (C++20) на практике реализованы через futex (`Linux`) / `WaitOnAddress` (`Windows`) по адресу объекта, поэтому для shared memory они обычно работают между процессами — но стандарт этого не гарантирует, это деталь реализации. Проверяйте на целевой платформе.
7. **Спинлок на `atomic_flag`** в shared memory допустим только для очень коротких критических секций и **смертельно опасен**: если владелец умрёт, остальные зависнут навсегда. Нужны таймауты/le``ase или robust-примитивы.

### SPSC ring buffer без блокировок

Классика для «producer в одном процессе, consumer в другом»:

```cpp
// producer
uint64_t t = ring->tail.load(std::memory_order_acquire);
if (ring->head.load(std::memory_order_relaxed) - t < CAP) {
    ring->buf[ring->head.load(std::memory_order_relaxed) % CAP]
        .store(value, std::memory_order_relaxed);
    ring->head.store(ring->head.load(std::memory_order_relaxed) + 1,
                     std::memory_order_release);
}

// consumer
uint64_t h = ring->head.load(std::memory_order_acquire);
uint64_t t = ring->tail.load(std::memory_order_relaxed);
if (h != t) {
    uint32_t v = ring->buf[t % CAP].load(std::memory_order_relaxed);
    ring->tail.store(t + 1, std::memory_order_release);
}
```

Свободные счётчики (`uint64_t`, не сбрасываются) избавляют от неоднозначности «пусто/полно». Для нескольких producer'ов/consumer'ов нужны CAS-циклы или отдельные счётчики на каждого участника.

### Seqlock

Для «много читателей — редкий писатель» (конфиг, таблицы) удобен seqlock: писатель инкрементирует счётчик (нечётный → идёт запись), пишет данные, инкрементирует ещё раз (чётный); читатель читает счётчик, данные, счётчик снова и повторяет, если значения не совпали или было нечётно. Требует аккуратных `acquire`/`release` и `atomic_thread_fence`.

---

## 6. Очереди сообщений и передача сообщений

| Механизм | Платформа | Особенности |
|---|---|---|
| `pipe`/`FIFO` | POSIX | Поток байт, блокирующие read/write, ядерный буфер |
| `socketpair`/UNIX socket | POSIX | Дуплекс, `SCM_RIGHTS` для передачи fd, `SOCK_SEQPACKET` сохраняет границы сообщений |
| POSIX `mq_*` | POSIX | Приоритеты, таймауты, уведомления, лимиты ядра |
| SysV `msg*` | POSIX | Устаревший, но живой |
| Named pipe | Windows | `PIPE_TYPE_MESSAGE`, impersonation, overlapped I/O |
| Mailslot | Windows | Широковещательная передача мелких сообщений |
| TCP/UDP | везде | Работает между машинами, но медленнее и без shared layout |
| D-Bus / gRPC / ZeroMQ | везде | Высокоуровневые, с сериализацией и маршрутизацией |

Главный плюс message passing: **нет проблемы общей раскладки памяти**, нет проблемы «кто инициализирует сегмент», нет висящих мьютексов. Минус — копирование и системные вызовы; для больших данных комбинируют: shared memory для payload + семафор/event/сокет для уведомления («doorbell»).

---

## 7. Проблема инициализации shared-сегмента

Это самая частая ошибка в реальном коде. Два процесса одновременно делают `shm_open(O_CREAT)` или `CreateFileMapping` — кто из них инициализирует мьютекс/condvar/поля? Второй может начать пользоваться ещё не инициализированным объектом.

Рабочие схемы:

1. **`O_CREAT | O_EXCL`** (POSIX) или `GetLastError() == ERROR_ALREADY_EXISTS` (Windows) — определяем создателя. Создатель инициализирует, затем публикует флаг «готово»; остальные ждут на именованном семафоре/событии.
2. **Атомарный state machine** прямо в сегменте:
   ```cpp
   int expected = 0;
   if (s->state.compare_exchange_strong(expected, 1)) {
       // я первый: инициализирую всё, затем state.store(2, release)
   } else {
       while (s->state.load(acquire) != 2) { /* spin / wait */ }
   }
   ```
   Важно: `state` должен быть в **уже обнулённой** памяти (новый сегмент ядра всегда нулевой — на это можно опираться и в POSIX, и в Windows).
3. **Именованный мьютекс вокруг инициализации** — проще и надёжнее, чем самодельный CAS-цикл.

Отдельно: `sem_open("/x", O_CREAT, ...)` тоже имеет гонку «создать/открыть» — используйте `O_EXCL` для детекции создателя.

---

## 8. Отказоустойчивость: что будет, если процесс умрёт

| Примитив | Поведение при смерти владельца |
|---|---|
| `pthread_mutex` (обычный) | Вечная блокировка, `EDEADLK` не будет |
| `pthread_mutex` robust | `EOWNERDEAD` → `pthread_mutex_consistent()` |
| `pthread_condvar` | Возможны потерянные wakeup'ы, зависание |
| POSIX/Windows семафор | Потеря счётчика (утечка «единицы») |
| Windows mutex | `WAIT_ABANDONED` |
| Windows event | Не «принадлежит» никому, но сигнал может не прийти |
| Файловая блокировка | Снимается ядром автоматически |
| Атомик/спинлок | Вечная блокировка (флаг остался взведён) |

Robust-мьютекс:

```cpp
int rc = pthread_mutex_lock(&s->mtx);
if (rc == EOWNERDEAD) {
    // предыдущий владелец умер внутри критической секции:
    // данные в shared memory могли остаться в несогласованном состоянии.
    repair_shared_state(s);                 // по возможности
    pthread_mutex_consistent(&s->mtx);      // иначе мьютекс навсегда «испорчен»
} else if (rc == ENOTRECOVERABLE) {
    // мьютекс уже был признан несогласованным — сегмент бесполезен
}
```

Практические выводы:

* для критических секций в shared memory — **robust + process-shared** мьютекс, либо Windows mutex с обработкой `WAIT_ABANDONED`;
* для ожиданий — всегда таймауты (`pthread_cond_timedwait`, `sem_timedwait`, `WaitForSingleObject(h, ms)`), чтобы не зависеть от чужого падения;
* продумайте **восстановление инвариантов**: недостаточно «взять лок», нужно ещё понять, в каком состоянии остались данные (незавершённая запись, «полуобновлённая» структура). Часто помогает журнал/поколение/контрольная сумма в сегменте;
* **очистка имён**: POSIX-объекты (`/dev/shm/...`) переживают процесс; нужен либо `sem_unlink`/`shm_unlink` при старте с проверкой «жив ли владелец», либо kernel-объекты Windows, которые удаляются автоматически по закрытии последнего handle.

---

## 9. Кроссплатформенные обёртки

Писать всё это руками под две ОС быстро надоедает. Готовые решения:

* **Boost.Interprocess** — `managed_shared_memory`, `managed_mapped_file`, `offset_ptr`, `interprocess_mutex`, `interprocess_condition`, `interprocess_semaphore`, `named_mutex`, `named_semaphore`, `named_condition`, `file_lock`, `message_queue`, `scoped_lock`. Внутри — process-shared pthread на POSIX и именованные объекты ядра на Windows.
* **Qt** — `QSharedMemory` (со встроенным `lock()`), `QSystemSemaphore`, `QLocalSocket`/`QLocalServer`.
* **.NET / Win32-обёртки** — именованные `Mutex`, `Semaphore`, `EventWaitHandle`.
* **Abseil / Folly** — в основном внутрипроцессные, для межпроцессного меньше готового.

---

## 10. Сравнительная таблица

| Механизм | Общая память нужна? | Скорость | Crash-safe | Кроссплатформенность |
|---|---|---|---|---|
| Именованный семафор (`sem_open`) | Нет | Высокая | Утечка счётчика | POSIX; на Windows — `CreateSemaphore` |
| Process-shared `pthread_mutex` | Да | Очень высокая | Только robust | POSIX (macOS — без robust) |
| Process-shared condvar | Да | Высокая | Плохо | POSIX |
| Windows mutex/event/semaphore | Нет | Высокая | Mutex — да, sem — утечка | Только Windows |
| `shm` + `std::atomic` | Да | Максимальная | Нет (спинлок) | Формально негарантированно, на практике да |
| Файловые блокировки | Нет (файл) | Средняя | Да (снимаются ядром) | POSIX advisory / Windows mandatory |
| POSIX MQ | Нет | Средняя | Да (очередь в ядре) | POSIX |
| Pipe / UNIX socket | Нет | Средняя | Да | POSIX; Windows — named pipe |
| TCP / gRPC | Нет | Низкая | Да | Везде, включая разные хосты |

---

## 11. Чек-лист типичных ошибок

1. Положить `std::mutex`/`std::condition_variable` в shared memory — **не работает**.
2. Забыть `PTHREAD_PROCESS_SHARED` у атрибутов.
3. Держать мьютекс и condvar в **разных** сегментах — UB.
4. Указатели внутри shared-структуры — при переносе в другой процесс это мусор.
5. Разная раскладка структур (`#pragma pack`, `long`, версии компилятора, `bool` в C vs C++) — данные «поехали».
6. Гонка инициализации сегмента (два создателя).
7. Забыть `pthread_mutex_consistent` после `EOWNERDEAD` — мьютекс навсегда `ENOTRECOVERABLE`.
8. Ожидание без таймаута — вечное зависание при падении партнёра.
9. Утечка именованных объектов (`sem_unlink`/`shm_unlink` не вызваны; `/dev/shm` забит).
10. `sem_unlink` сразу после `sem_open` у одного из процессов — остальные не найдут имя (правильно: unlink'ать, когда имя больше не нужно *новым* участникам, либо вообще не unlink'ать до завершения).
11. `fcntl`-блокировка и случайное закрытие другого fd на тот же файл в том же процессе — блокировка «испарилась».
12. Использование `volatile` вместо атомиков/барьеров.
13. Атомик, который не lock-free, — libatomic тихо берёт process-local лок.
14. Отсутствие `alignas` — false sharing и torn-доступ.
15. Игнорирование прав доступа: `0600` и `umask`, security descriptor на Windows; иначе чужой процесс может открыть ваш сегмент.
16. `Global\` в Windows без прав администратора — `CreateMutex` вернёт `ERROR_ACCESS_DENIED`.
17. `SIGBUS` при переполнении tmpfs (`/dev/shm`) — сегмент «есть», а страниц нет.
18. `CreateFileMapping` без проверки `GetLastError()` — не узнаете, что вы не создатель.
19. Забыли `CloseHandle`/`munmap` — утечка handle'ов/отображений (на Windows handle'ы кончаются).
20. Считают, что shared memory автоматически «синхронизирует» данные: когерентность кэшей есть, но **порядок** операций и взаимное исключение обеспечиваете вы.

---

## Итог

* Внутрипроцессные примитивы C++ (`std::mutex`, `std::condition_variable`, `std::shared_mutex`) для межпроцессной синхронизации **непригодны**.
* Самый простой путь — **именованные объекты ядра**: семафоры/события/мьютексы (POSIX `sem_open`, Windows `CreateMutex`/`CreateEvent`/`CreateSemaphore`) или pipe/сокет/очередь сообщений.
* Самый быстрый — **shared memory + process-shared примитивы**: robust `pthread_mutex`, `pthread_cond_t` с `PTHREAD_PROCESS_SHARED`, семафоры, либо lock-free структуры на `std::atomic`.
* Для исключительного доступа к файлу — **файловые блокировки** (`flock`/`fcntl`/OFD locks, `LockFileEx`).
* Обязательно проектируйте **восстановление после падения** (robust-мьютексы, `WAIT_ABANDONED`, таймауты, проверка инвариантов) и **корректную инициализацию/очистку** именованных объектов.
* Если процессы могут быть на разных машинах — shared memory отпадает, остаются сокеты/RPC/брокеры и распределённые блокировки с fencing-токенами.