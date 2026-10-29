# 3. Что такое RPC? Какие библиотеки есть?

## Что такое RPC

**RPC (Remote Procedure Call, удалённый вызов процедур)** — это способ организации взаимодействия между программами, при котором вызов функции в другой процесс (обычно на другой машине, через сеть) выглядит для программиста почти как вызов обычной локальной функции.

Идея стара как мир: клиент пишет `user = userService.GetUser(42)`, а всё «сетевое» — установка соединения, сериализация аргументов, передача, ожидание, разбор ответа, обработка ошибок — спрятано за сгенерированной обёрткой. Это и есть главная ценность RPC: **скрыть транспорт за интерфейсом вызова**, и главная его ловушка: **сеть — не локальный вызов**, и делать вид, что это так, можно лишь до определённого предела.

Исторически RPC появился в 1970–80-х (Sun/ONC RPC, Xerox Courier), расцвёл в 1990-х (CORBA, DCOM, Java RMI), пережил эпоху XML-RPC/SOAP в 2000-х и вернулся в современном виде с gRPC и Apache Thrift.

## Анатомия RPC-вызова

Типичный RPC-стек состоит из нескольких слоёв:

```
клиентский код
     │  вызов метода
     ▼
[ stub / proxy ]  ← сгенерирован из контракта (IDL)
     │  маршалинг (сериализация аргументов)
     ▼
[ транспорт ]     ← TCP, HTTP/2, UDP, Unix-сокет, шина
     ▼
[ skeleton / dispatcher ]  ← разбор запроса, поиск метода
     ▼
серверный код (реализация)
```

Ключевые понятия:

| Понятие | Смысл |
|---|---|
| **IDL** (Interface Definition Language) | Язык описания контракта: методы, типы аргументов и результата. Из него генерируют клиентский и серверный код. Примеры: `.proto` (Protobuf), `.thrift`, `.avdl` (Avro), WSDL (SOAP). |
| **Stub / proxy** | Клиентская заглушка с тем же интерфейсом, что у сервера. Внутри — сериализация и отправка. |
| **Skeleton / dispatcher** | Серверная часть: принимает запрос, десериализует, вызывает реальный метод, отправляет ответ. |
| **Маршалинг (marshalling)** | Преобразование объектов/структур в байты и обратно. Бывает текстовый (JSON, XML) и бинарный (Protobuf, Thrift Binary, Avro, Cap'n Proto, MessagePack). |
| **Транспорт** | HTTP/1.1, HTTP/2, raw TCP, Unix domain socket, AMQP, WebSocket. |
| **Синхронность** | Вызов может блокировать поток до ответа (unary) или быть асинхронным (future/promise), а также потоковым (streaming). |

## Свойства RPC, о которых важно помнить

1. **Прозрачность обманчива.** Локальный вызов — наносекунды, сетевой — миллисекунды; он может упасть, зависнуть, потерять ответ, выполниться дважды. Это классические «8 заблуждений распределённых вычислений»: сеть надёжна, задержка нулевая, пропускная способность бесконечна, топология не меняется и т.д.
2. **Частичный отказ.** Сервер обработал запрос, но ответ потерялся. Клиент не знает, выполнено ли действие. Отсюда — требование **идемпотентности** и/или `idempotency key`, чтобы безопасно повторять запрос.
3. **Дедлайны и таймауты обязательны.** Без них поток клиента может ждать вечно, а ресурсы сервера — копиться.
4. **Ретраи** без идемпотентности превращаются в дубли (двойное списание денег — классика).
5. **Связанность.** RPC-контракт жёстко связывает клиента и сервер: изменение сигнатуры ломает совместимость. Нужны правила версионирования и обратной совместимости (в Protobuf — резервирование номеров полей, запрет менять типы).
6. **Ошибки — часть контракта.** Нужно различать транспортные ошибки, ошибки приложения и бизнес-ошибки; в gRPC для этого есть коды статусов и `details`.
7. **Наблюдаемость.** В распределённой системе нужны трассировка (OpenTelemetry), метрики, логи с correlation id.

## RPC vs REST vs очереди

| Критерий | RPC (gRPC/Thrift) | REST/HTTP | Очереди (Kafka, RabbitMQ) |
|---|---|---|---|
| Стиль | «Вызови метод» | «Действие над ресурсом» | «Отправь событие/сообщение» |
| Связность | Жёсткая (контракт) | Слабая (гипермедиа, но на практике тоже контракт) | Слабая (асинхронность) |
| Формат | Бинарный (обычно) | JSON/XML | Любой |
| Производительность | Высокая (HTTP/2, мультиплексирование, компактный payload) | Ниже из-за текстового формата и накладных расходов | Высокая, но другая модель |
| Потоковая передача | Да (bidirectional streaming) | Ограниченно (SSE, WebSocket) | Да, но иначе |
| Браузеры | Требует gRPC-Web/Connect | Из коробки | Требует моста |
| Публичные API | Редко | Обычно да | Нет |
| Внутренние сервисы | Обычно да | Тоже бывает | Для событий |

Грубое правило: **внутри — RPC, наружу — REST/GraphQL, для событий и развязки — очереди**. На практике всё смешано.

## Библиотеки и фреймворки

### 1. gRPC — де-факто стандарт современного RPC

От Google, поверх HTTP/2 и Protocol Buffers. Поддерживает unary, server streaming, client streaming и bidirectional streaming, дедлайны, метаданные, interceptors, сжатие, балансировку, reflection.

Реализации практически для всех языков:

| Язык | Пакет / библиотека |
|---|---|
| C++ | `grpc` (официальная) |
| Java/Kotlin | `grpc-java`, `grpc-kotlin` |
| Go | `google.golang.org/grpc` |
| Python | `grpcio`, `grpcio-tools` (генерация из `.proto`) |
| C#/.NET | `Grpc.AspNetCore`, `Grpc.Net.Client`, `Google.Protobuf` |
| Node.js | `@grpc/grpc-js` (чистый JS), `@grpc/proto-loader`; старый `grpc` (native) устарел |
| PHP | `grpc/grpc` (расширение) + `google/protobuf` |
| Ruby | `grpc`, `google-protobuf` |
| Dart/Flutter | `grpc`, `protobuf` |
| Swift | `grpc-swift` |
| Браузер | `grpc-web`, `@connectrpc/connect` (Connect), `twirp` |

Вспомогательная экосистема: `buf` (lint, breaking-change detection, генерация), `grpcurl` (CLI как curl для gRPC), `grpc-gateway` (REST→gRPC трансляция), `envoy` (прокси/балансировщик для gRPC), OpenTelemetry-инструментирование.

### 2. Apache Thrift

Facebook → Apache. Свой IDL, генерация кода для C++, Java, Python, Go, C#, Ruby, PHP, Node.js, Erlang и др. Транспорты: TCP, HTTP, TLS; протоколы: Binary, Compact, JSON. Проще и «старее» gRPC, но всё ещё широко используется (Cassandra, HBase, Evernote, Uber). Хорошо подходит, когда нужен свой транспорт или нестандартная сериализация.

### 3. JSON-RPC

Самый простой текстовый протокол: JSON-объекты `{"jsonrpc":"2.0","method":"...","params":[...],"id":1}`. Транспорт — что угодно: HTTP, WebSocket, TCP, stdio.

- Python: `jsonrpcserver`, `json-rpc`, `pytest-jsonrpc`, `aiohttp-json-rpc`;
- Rust: `jsonrpsee` (async, tokio);
- Go: `net/rpc/jsonrpc` (stdlib), `go-jsonrpc`;
- Node.js: `jayson`, `json-rpc-2.0`;
- PHP: `jsonrpcphp`;
- Общий стандарт: JSON-RPC 2.0.

Используется в блокчейн-нодах (Ethereum JSON-RPC), IDE/LSP (Language Server Protocol — по сути JSON-RPC), Ethereum, Bitcoin Core, инструментах вроде `gopls`.

### 4. XML-RPC и SOAP

- **XML-RPC** — примитивный предшественник, HTTP + XML. В Python есть в стандартной библиотеке: `xmlrpc.client`, `xmlrpc.server`. В других языках — Apache XML-RPC (Java), `xmlrpc` для PHP, Wordpress XML-RPC API.
- **SOAP** — тяжёлый корпоративный стандарт на WSDL/XSD, с WS-Security, WS-Addressing и т.д. Реализации: Apache CXF, Apache Axis2 (Java), .NET WCF, `gSOAP` (C/C++), `zeep` и `suds` (Python), `Savon` (Ruby), `node-soap` (Node.js). Жив в энтерпрайзе, банках, госуслугах, интеграциях с 1С/SAP.

### 5. Бинарные и «экзотические» RPC

- **Cap'n Proto RPC** — очень быстрый, zero-copy, с обещаниями (promises) и объектными возможностями; C++, Rust, Python, Go.
- **Apache Avro RPC** — Avro-сериализация + RPC; популярен в Hadoop-экосистеме.
- **MessagePack-RPC** — компактный бинарный JSON-подобный RPC; реализации для Python, Ruby, Go, Node.js.
- **BERT-RPC** — Erlang-ориентированный, исторический.
- **ONC RPC / Sun RPC** — «дедушка» RPC, `rpcgen`, portmapper; на нём до сих пор работает NFS. Есть в Linux/Unix из коробки.
- **D-Bus** — RPC-шина для IPC в Linux (systemd, NetworkManager, desktop-приложения).
- **ZeroC Ice** — промышленный RPC/ORB с собственным IDL, поддержка C++, Java, Python, C#, PHP, Ruby.
- **WAMP** (Web Application Messaging Protocol) — RPC + PubSub поверх WebSocket; `Autobahn` (Python, JS, C++).

### 6. По языкам и экосистемам

**Java/JVM:**
- Java RMI (встроенный, legacy), .NET Remoting — исторические аналоги;
- **Apache Dubbo** — очень популярный в Китае RPC-фреймворк (Alibaba), с registry (ZooKeeper/Nacos), балансировкой, сервис-мешем;
- **Spring Remoting** (HTTP Invoker, Hessian, RMI, JMS);
- **Finagle** (Scala, Twitter), **Akka/Pekko Remote**, **Kryonet** (игры), **Hadoop IPC** (Protobuf-based);
- **gRPC-java**, **Thrift**.

**Go:**
- `net/rpc` + `net/rpc/jsonrpc` из стандартной библиотеки (gob/JSON, простые, но без стриминга и HTTP/2);
- `google.golang.org/grpc`;
- **Kitex** (ByteDance), **Kratos** (Bilibili), **go-zero**, **go-micro**, **Twirp** (Twitch).

**Python:**
- `grpcio`, `thrift`;
- **Pyro5** — «Python-нативный» RPC (объекты, remote objects);
- **RPyC** — прозрачный RPC, позволяет вызывать методы удалённых объектов;
- `xmlrpc.client/server` (stdlib);
- `jsonrpcserver`, `json-rpc`;
- **Nameko** — микросервисы с RPC через RabbitMQ;
- `zeep` (SOAP).

**C#/.NET:**
- **WCF** (SOAP, net.tcp, legacy, но жив),
- **gRPC for .NET** (`Grpc.AspNetCore`),
- **StreamJsonRpc** (используется в Visual Studio),
- **MagicOnion** (gRPC-based C# RPC),
- .NET Remoting (устарел), **SignalR** (не RPC в чистом виде, но часто используется как «RPC поверх WebSocket»).

**Rust:**
- **tonic** (gRPC), **tarpc**, **jsonrpsee**, **capnp-rpc**, **volo** (CloudWeGo), **rpcx**.

**Node.js/TypeScript:**
- `@grpc/grpc-js`, `thrift`, `msgpack-rpc`, `jayson` (JSON-RPC);
- **tRPC** — типобезопасный RPC для TypeScript-монорепозиториев (не про бинарный протокол, а про end-to-end типы);
- **Connect** (`@connectrpc/connect`) — совместим с gRPC, но работает в браузере и по обычному HTTP.

**Erlang/Elixir:**
- распределение Erlang (`:rpc`, `:erpc`), **grpc-elixir**, **Phoenix Channels** (не RPC);
- BERT-RPC — исторический.

**C/C++:**
- `grpc`, `thrift`, `capnp`, `gSOAP`, **brpc** (Baidu), **Tars** (Tencent), **Apache Arrow Flight** (RPC для передачи колоночных данных на gRPC).

**Мобильные/десктоп:**
- gRPC-Swift, grpc-kotlin, `grpc-dart`, .NET MAUI + gRPC.

### 7. RPC поверх брокеров сообщений

Иногда «RPC» делают на очередях, чтобы получить буферизацию и развязку:
- **RabbitMQ RPC** — паттерн с `reply_to` и `correlation_id`;
- **NATS** request/reply;
- **ZeroMQ** REQ/REP;
- **Redis** — не RPC, но pub/sub + списки иногда используют для запрос-ответа;
- **gRPC over AMQP** — редко, но встречается.

## Мини-примеры

**Protobuf + gRPC (контракт):**

```proto
syntax = "proto3";

service UserService {
  rpc GetUser (GetUserRequest) returns (User);
  rpc ListUsers (ListUsersRequest) returns (stream User); // server streaming
}

message GetUserRequest { int64 id = 1; }
message User { int64 id = 1; string name = 2; string email = 3; }
```

**Клиент на Python (иллюстративно):**

```python
import grpc
import user_pb2, user_pb2_grpc

with grpc.insecure_channel("localhost:50051") as channel:
    stub = user_pb2_grpc.UserServiceStub(channel)
    user = stub.GetUser(user_pb2.GetUserRequest(id=42), timeout=2.0)
    print(user.name)
```

**JSON-RPC 2.0 (запрос/ответ):**

```json
{"jsonrpc": "2.0", "method": "subtract", "params": [42, 23], "id": 1}
{"jsonrpc": "2.0", "result": 19, "id": 1}
```

**Thrift IDL:**

```thrift
service UserService {
  User getUser(1: i64 id)
  list<User> listUsers(1: i32 limit)
}
```

## Как выбирать

- **Нужна производительность, стриминг, строгий контракт, полиглот-сервисы** → gRPC (+ Protobuf), при необходимости `buf`, `grpc-gateway`, Envoy.
- **Нужен свой транспорт/сериализация, старая инфраструктура, Cassandra-подобные сценарии** → Thrift.
- **Простота, отладка «глазами», браузер, небольшие внутренние API** → JSON-RPC (или REST).
- **Публичное API, кэширование, HTTP-семантика** → REST/GraphQL, а не RPC.
- **Интеграция с энтерпрайзом, WSDL, WS-Security** → SOAP (WCF/CXF/zeep).
- **TypeScript-монорепозиторий, минимум церемоний** → tRPC/Connect.
- **Linux-IPC** → D-Bus; **NFS-подобное** → ONC RPC.
- **События, развязка по времени, буферизация** → очередь, а не RPC.

## Итог

RPC — это парадигма «вызов метода через границу процесса», а не конкретный протокол. Его сила — в удобном контракте и генерации кода; его слабость — в том, что он маскирует распределённую природу системы. Поэтому зрелое использование RPC — это всегда дисциплина: явные дедлайны, идемпотентность, версионирование контракта, обработка частичных отказов и наблюдаемость. Из библиотек сегодня стоит знать в первую очередь **gRPC**, **Apache Thrift**, **JSON-RPC-реализации**, **SOAP-стек** и нишевые **Cap'n Proto / Dubbo / tRPC** — этого достаточно, чтобы закрыть почти любой практический сценарий.