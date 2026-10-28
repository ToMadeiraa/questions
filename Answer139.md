## 139. Что такое сокет?

**Сокет (socket)** — это программный интерфейс (абстракция), представляющий собой конечную точку (endpoint) двустороннего обмена данными между двумя программами в сети (или на одной машине). Сокет можно рассматривать как «дверь», через которую процесс отправляет и принимает данные по сети.

Сокет идентифицируется парой **IP-адрес + номер порта** (например, `192.168.1.10:8080`). Полный сетевой обмен описывается парой сокетов — клиентским и серверным, поэтому говорят о **соединении как о паре сокетов** (socket pair).

### Основные характеристики

- Сокет — это **дескриптор** (в POSIX-системах — файловый дескриптор типа `int`), с которым можно работать через системные вызовы.
- Сокеты абстрагируют детали сетевых протоколов (TCP, UDP, IP) от прикладного программиста.
- В Windows сокеты работают через библиотеку **Winsock** (`ws2_32.lib`), в Linux/Unix — через стандартный API BSD sockets.
- Сокеты бывают **сетевые** (AF_INET, AF_INET6) и **локальные** (AF_UNIX) для межпроцессного взаимодействия на одной машине.

### Типы сокетов

| Тип | Константа | Протокол | Особенности |
|-----|-----------|----------|-------------|
| Потоковый | `SOCK_STREAM` | TCP | Надёжная доставка, установка соединения, порядок сохраняется |
| Дейтаграммный | `SOCK_DGRAM` | UDP | Без соединения, без гарантии доставки, низкие накладные расходы |
| Сырой | `SOCK_RAW` | — | Прямой доступ к заголовкам протоколов (требует прав) |

### Жизненный цикл TCP-сокета

**Сервер:**
1. `socket()` — создать сокет.
2. `bind()` — привязать к IP-адресу и порту.
3. `listen()` — перевести в режим прослушивания.
4. `accept()` — принять входящее соединение (возвращает **новый** сокет для обмена с клиентом).
5. `recv()` / `send()` — обмен данными.
6. `close()` — закрыть сокет.

**Клиент:**
1. `socket()` — создать сокет.
2. `connect()` — установить соединение с сервером.
3. `send()` / `recv()` — обмен данными.
4. `close()` — закрыть сокет.

### Пример на C++ (Windows, Winsock)

**Сервер:**
```cpp
#include <winsock2.h>
#include <iostream>
#pragma comment(lib, "ws2_32.lib")

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);

    bind(server, (sockaddr*)&addr, sizeof(addr));
    listen(server, SOMAXCONN);

    SOCKET client = accept(server, nullptr, nullptr);

    char buf[1024];
    int n = recv(client, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        std::cout << "Получено: " << buf << std::endl;
    }

    send(client, "Hello, client!", 14, 0);
    closesocket(client);
    closesocket(server);
    WSACleanup();
    return 0;
}
```

**Клиент:**
```cpp
#include <winsock2.h>
#include <iostream>
#pragma comment(lib, "ws2_32.lib")

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    connect(sock, (sockaddr*)&addr, sizeof(addr));

    send(sock, "Hi, server!", 11, 0);

    char buf[1024];
    int n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        std::cout << "Ответ: " << buf << std::endl;
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
```

> **Примечание:** в Linux/Unix вместо `closesocket()` используется `close()`, а `WSAStartup`/`WSACleanup` не нужны — подключение идёт через `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`.

### Важные детали, которые часто спрашивают

- **`htons()`/`htonl()`** — преобразование чисел в сетевой порядок байт (big-endian), т.к. в сети принят именно он.
- **`accept()` возвращает новый сокет** — исходный (слушающий) остаётся открытым для приёма следующих подключений.
- **Сокет — это не «канал»**, а именно конечная точка; соединение — это пара сокетов.
- **Блокирующий режим по умолчанию**: `accept()`, `recv()` ждут данных; для асинхронной работы используют `select()`, `poll()`, `epoll` (Linux) или `WSAAsyncSelect`/IOCP (Windows), а также неблокирующий режим (`ioctlsocket`/`fcntl`).
- **Порт** — 16-битное число (0–65535); порты 0–1023 привилегированные, требуют прав администратора.

### Краткое резюме

Сокет — это программная абстракция сетевого соединения, идентифицируемая IP-адресом и портом, через которую процессы обмениваются данными. В C++ работа с сокетами ведётся через системные вызовы (BSD sockets в POSIX, Winsock в Windows), а выбор типа сокета (`SOCK_STREAM`/`SOCK_DGRAM`) определяет используемый транспортный протокол — TCP или UDP.