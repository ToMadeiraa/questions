# 2. Как разработать систему плагинов на С++?

Плагинная система — это способ наращивать функциональность программы без её пересборки: ядро (хост) при запуске (или по требованию) находит внешние модули, загружает их и вызывает через заранее оговорённый интерфейс. В C++ главная сложность не в `dlopen`, а в **стабильном двоичном интерфейсе (ABI)**: две стороны собраны разными компиляторами, с разными флагами и версиями стандартной библиотеки, и должны всё равно понять друг друга.

Ниже — полный разбор: от выбора архитектуры до кода, сборки и типичных грабель.

## 1. Сначала определить требования

От ответов на эти вопросы зависит вся конструкция:

| Вопрос | Влияние на дизайн |
|---|---|
| Плагины ваши или сторонние? | Свои можно пересобирать вместе с хостом; сторонним нужен железобетонный ABI |
| Нужна горячая перезагрузка? | Требует контроля времени жизни объектов и ссылок |
| Плагины доверенные? | Недоверенные — только отдельный процесс/песочница/WASM |
| Допустимы падения плагина? | Нужна изоляция (отдельный процесс) или хотя бы перехват исключений |
| Какие языки, кроме C++? | C ABI обязателен, если плагины на C, Rust, Go |
| Насколько важна производительность? | In-process + C ABI; IPC добавляет задержки |

## 2. Ключевые задачи, которые надо решить

Любая плагинная система — это набор решений по восьми пунктам:

1. **Обнаружение** — где лежат плагины, как они называются, как узнать их метаданные.
2. **Загрузка** — `dlopen`/`LoadLibrary`, поиск точки входа, проверка совместимости.
3. **Контракт (ABI)** — что именно пересекает границу модуля: типы, вызовы, владение памятью.
4. **Версионирование** — как хост понимает, что плагин слишком старый/новый.
5. **Жизненный цикл** — инициализация, использование, остановка, выгрузка, порядок.
6. **Ошибки** — коды возврата, диагностика, исключения, падения.
7. **Изоляция** — что делать, если плагин завис или упал.
8. **Безопасность** — доверие к коду, который получает полные права процесса.

## 3. Сравнение подходов

| Подход | Стабильность ABI | Сложность | Когда применять |
|---|---|---|---|
| **C ABI + `dlopen`/`LoadLibrary`** | Высокая (при аккуратности) | Средняя | Универсальный выбор, особенно для сторонних плагинов |
| **C++-интерфейс (чистые виртуальные функции)** | Низкая: тот же компилятор, CRT, флаги | Низкая | Внутренние плагины, сборка в одном дереве |
| **COM-подобный подход (IUnknown)** | Высокая, но много кода | Высокая | Windows-экосистема, нужна строгая совместимость |
| **Статическая регистрация (саморегистрация)** | Не применимо (одна сборка) | Низкая | Расширения, известные на этапе компиляции |
| **Встроенный скриптовый движок (Lua, Python, QuickJS)** | Высокая | Средняя | Пользовательские сценарии, частая смена логики |
| **Отдельный процесс + IPC** | Высокая | Высокая | Недоверенный/нестабильный код, изоляция |
| **WASM (wasmtime, wasmer)** | Очень высокая | Высокая | Песочница, мультиязычность |

Практическое правило: **граница модуля должна быть максимально «сишной»**. Всё остальное — детали реализации внутри плагина.

## 4. Базовый вариант: C ABI + динамические библиотеки

### 4.1. Общий заголовок контракта

Один заголовок компилируют и хост, и плагин. В нём — только POD-структуры, целые типы фиксированной ширины, `const char*` в UTF-8 и указатели на функции. Никаких `std::string`, `std::vector`, шаблонов и исключений в сигнатурах.

```cpp
// plugin_api.h — общий контракт хоста и плагина
#pragma once
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#  define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#  define PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

inline constexpr uint32_t kPluginAbiVersion = 3;

// Таблица сервисов, которые хост даёт плагину.
// Новые поля добавляются ТОЛЬКО в конец.
struct HostApi {
    uint32_t struct_size;   // sizeof(HostApi) на стороне хоста
    uint32_t abi_version;

    void  (*log)(int level, const char* msg);
    void* (*alloc)(std::size_t size);
    void  (*free)(void* ptr);
};

struct PluginDescriptor {
    uint32_t struct_size;   // sizeof(PluginDescriptor) на стороне плагина
    uint32_t abi_version;

    const char*        id;          // "com.example.echo" — уникальный
    const char*        name;        // человекочитаемое имя
    const char*        version;     // "1.2.0", semver
    const char* const* capabilities;
    std::size_t        capability_count;

    // Экземпляр плагина: создание/уничтожение.
    void* (*create)(const HostApi* host, const char* config_json);
    void  (*destroy)(void* instance);

    // Основная операция. Возвращает 0 при успехе.
    // *output_utf8 валиден до следующего вызова или destroy.
    int   (*process)(void* instance, const char* input_utf8,
                     const char** output_utf8);
};

// Единственная экспортируемая точка входа.
// nullptr — плагин несовместим с данным ABI хоста.
PLUGIN_EXPORT const PluginDescriptor* plugin_get_descriptor(uint32_t host_abi);
```

Почему так:

- `struct_size` в начале каждой структуры — классический приём расширения ABI: старый хост и новый плагин (и наоборот) могут договориться, какие поля реально существуют.
- `extern "C"` убирает искажение имён, `visibility("default")`/`dllexport` делает символ видимым при `-fvisibility=hidden`.
- `const char*` вместо `std::string` — потому что layout `std::string` различается между libstdc++/libc++, debug/release, GCC 5 dual ABI (`_GLIBCXX_USE_CXX11_ABI`).
- Указатели на функции в `HostApi` — способ дать плагину сервисы хоста, не заставляя его линковаться с хостом (иначе на Windows получится циклическая зависимость и проблемы с символами).

### 4.2. Реализация плагина

```cpp
// echo_plugin.cpp
#include "plugin_api.h"
#include <string>

namespace {

struct Instance {
    const HostApi* host = nullptr;
    std::string    config;
    std::string    output;   // буфер ответа, живёт до следующего вызова

    explicit Instance(const HostApi* h, const char* cfg)
        : host(h), config(cfg ? cfg : "") {}
};

void* create(const HostApi* host, const char* config_json) {
    if (!host || host->abi_version != kPluginAbiVersion) return nullptr;
    try {
        auto* inst = new Instance(host, config_json);
        inst->host->log(1, "echo: создан экземпляр");
        return inst;
    } catch (...) {
        return nullptr;   // исключение не должно покидать модуль
    }
}

void destroy(void* p) {
    delete static_cast<Instance*>(p);
}

int process(void* p, const char* input, const char** output) {
    auto* inst = static_cast<Instance*>(p);
    if (!inst || !input || !output) return -1;
    try {
        inst->output = std::string("echo: ") + input;
        *output = inst->output.c_str();
        return 0;
    } catch (...) {
        return -2;
    }
}

const char* const kCapabilities[] = {"text.transform"};
const PluginDescriptor kDescriptor = {
    sizeof(PluginDescriptor),
    kPluginAbiVersion,
    "com.example.echo",
    "Echo plugin",
    "1.0.0",
    kCapabilities,
    sizeof(kCapabilities) / sizeof(kCapabilities[0]),
    &create,
    &destroy,
    &process
};

} // namespace

PLUGIN_EXPORT const PluginDescriptor* plugin_get_descriptor(uint32_t host_abi) {
    if (host_abi != kPluginAbiVersion) return nullptr;
    return &kDescriptor;
}
```

Ключевые моменты:

- дескриптор — статический объект внутри библиотеки, поэтому указатель на него валиден, пока библиотека загружена;
- все входы проверяются на `nullptr`, все исключения гасятся на границе;
- строка результата принадлежит плагину и живёт до следующего вызова — это четко документированное правило владения;
- никаких глобальных конструкторов с побочными эффектами: они выполнятся при `dlopen`, и ошибку оттуда поймать нельзя.

### 4.3. Загрузка на стороне хоста

```cpp
// plugin_host.cpp
#include "plugin_api.h"
#include <filesystem>
#include <memory>
#include <string>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace {

void* open_library(const std::filesystem::path& p) {
#if defined(_WIN32)
    return ::LoadLibraryW(p.wstring().c_str());
#else
    return ::dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void close_library(void* h) {
#if defined(_WIN32)
    ::FreeLibrary(static_cast<HMODULE>(h));
#else
    ::dlclose(h);
#endif
}

void* load_symbol(void* h, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(
        ::GetProcAddress(static_cast<HMODULE>(h), name));
#else
    return ::dlsym(h, name);
#endif
}

std::string last_error() {
#if defined(_WIN32)
    return "код ошибки WinAPI: " + std::to_string(::GetLastError());
#else
    const char* e = ::dlerror();
    return e ? e : "неизвестная ошибка";
#endif
}

using GetDescriptorFn = const PluginDescriptor* (*)(uint32_t);

struct LoadedPlugin {
    void*                   lib = nullptr;
    const PluginDescriptor* desc = nullptr;
    void*                   obj = nullptr;

    ~LoadedPlugin() {
        if (obj && desc && desc->destroy) desc->destroy(obj);
        if (lib) close_library(lib);   // библиотека — строго последней
    }
};

} // namespace

std::unique_ptr<LoadedPlugin> load_plugin(const std::filesystem::path& path,
                                          const HostApi* host,
                                          std::string& error) {
    void* lib = open_library(path);
    if (!lib) {
        error = "не удалось открыть " + path.string() + ": " + last_error();
        return nullptr;
    }

    auto get_descriptor = reinterpret_cast<GetDescriptorFn>(
        load_symbol(lib, "plugin_get_descriptor"));
    if (!get_descriptor) {
        error = "нет символа plugin_get_descriptor";
        close_library(lib);
        return nullptr;
    }

    const PluginDescriptor* d = get_descriptor(host->abi_version);
    if (!d) {
        error = "плагин отверг ABI хоста";
        close_library(lib);
        return nullptr;
    }

    // Сначала читаем только struct_size — он есть в любой версии структуры.
    if (d->struct_size < sizeof(PluginDescriptor)) {
        error = "дескриптор плагина короче ожидаемого";
        close_library(lib);
        return nullptr;
    }
    if (d->abi_version != host->abi_version) {
        error = "несовпадение версии ABI";
        close_library(lib);
        return nullptr;
    }
    if (!d->id || !d->create || !d->destroy || !d->process) {
        error = "дескриптор заполнен не полностью";
        close_library(lib);
        return nullptr;
    }

    auto p = std::make_unique<LoadedPlugin>();
    p->lib  = lib;
    p->desc = d;

    p->obj = d->create(host, R"({"mode":"default"})");
    if (!p->obj) {
        error = "create() вернул nullptr";
        return nullptr;   // деструктор закроет библиотеку
    }
    return p;
}
```

Использование:

```cpp
int main() {
    HostApi host{};
    host.struct_size  = sizeof(HostApi);
    host.abi_version  = kPluginAbiVersion;
    host.log          = [](int, const char* m) { std::puts(m); };
    host.alloc        = [](std::size_t n) { return std::malloc(n); };
    host.free         = [](void* p) { std::free(p); };

    std::string err;
    auto plugin = load_plugin("./plugins/echo.dll", &host, err);
    if (!plugin) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }

    const char* out = nullptr;
    if (plugin->desc->process(plugin->obj, "привет", &out) == 0)
        std::puts(out);

    return 0;   // ~LoadedPlugin: destroy(), затем FreeLibrary
}
```

## 5. Детали ABI, на которых чаще всего ломается

### 5.1. Владение памятью

Правило: **кто выделил — тот и освобождает**, либо обе стороны используют один аллокатор, переданный через `HostApi`.

Плохо:

```cpp
char* make_greeting();          // плагин выделил new[]
// хост: delete[] ptr;           // другая CRT -> падение
```

Хорошо: плагин отдаёт `const char*` со временем жизни до следующего вызова (как в примере), либо возвращает объект вместе с функцией освобождения, либо использует `host->alloc`/`host->free`.

### 5.2. Исключения

Исключение, пересёкшее границу модуля, — это неопределённое поведение, особенно на Windows при разных CRT. На каждой экспортируемой функции ставим барьер:

```cpp
extern "C" int plugin_process(void* p, const char* in, const char** out) {
    try {
        return do_process(p, in, out);
    } catch (const std::exception& e) {
        log_error(e.what());
        return -1;
    } catch (...) {
        return -2;
    }
}
```

### 5.3. RTTI и `dynamic_cast`

Через границу модуля не работают надёжно: информация о типе привязана к конкретной реализации RTTI. Поэтому в C-подходе — только явные `id`/`type`-теги и функции приведения.

### 5.4. Версионирование

Три уровня, которые стоит различать:

- **Версия ABI** (`abi_version`) — целое число, меняется при любом несовместимом изменении структур или сигнатур. Несовпадение — плагин не грузим.
- **Версия API плагина** (semver в `version`) — для информации, UI и зависимостей.
- **Размер структуры** (`struct_size`) — позволяет добавлять поля в конец без смены версии ABI.

Проверка дополнительных полей:

```cpp
template <class T>
bool has_field(const HostApi* h, std::size_t offset) {
    return h->struct_size >= offset + sizeof(T);
}

// использование:
if (has_field<void (*)(const char*)>(host, offsetof(HostApi, log))) {
    host->log(2, "предупреждение");
}
```

Политика совместимости должна быть записана явно: например, «хост с ABI 3 принимает плагины с ABI 3; плагины с ABI 2 не грузятся».

### 5.5. Видимость символов и линковка

- Linux/macOS: собирать с `-fvisibility=hidden -fvisibility-inlines-hidden`, экспортировать только нужное.
- Windows: `__declspec(dllexport)` на точке входа; остальное скрыто по умолчанию.
- Не отдавать наружу inline-функции и шаблоны, которые могут разойтись по ODR.
- На Windows: не смешивать debug/release CRT и `_ITERATOR_DEBUG_LEVEL`; на Linux — не смешивать `_GLIBCXX_USE_CXX11_ABI=0` и `=1`.

### 5.6. Порядок выгрузки

Сначала уничтожаются все объекты плагина, затем вызывается `dlclose`/`FreeLibrary`. Если выгрузить библиотеку, пока живы объекты с виртуальными таблицами внутри неё, следующий вызов превратится в прыжок по освобождённой памяти. На Windows `FreeLibrary` вообще небезопасен, если в этот момент другой поток исполняет код библиотеки.

## 6. Вариант с C++-интерфейсами

Если плагины собираются в том же проекте тем же компилятором, можно упростить контракт до абстрактного класса:

```cpp
// iplugin.h
struct IHost {
    virtual ~IHost() = default;
    virtual void log(const char* msg) = 0;
};

struct IPlugin {
    virtual ~IPlugin() = default;
    virtual const char* name() const = 0;
    virtual bool init(IHost& host) = 0;
    virtual int  process(const char* in, std::string& out) = 0;
};

extern "C" PLUGIN_EXPORT IPlugin* create_plugin();
extern "C" PLUGIN_EXPORT void destroy_plugin(IPlugin*);
```

Что здесь всё ещё обязательно:

- `extern "C"` для точек входа (иначе имя будет искажено и `dlsym` его не найдёт);
- виртуальный деструктор в интерфейсе;
- фабрика и «уничтожитель» в самом плагине, чтобы `delete` происходил в той же библиотеке;
- отсутствие `std::string` в сигнатурах, если есть хоть малейший риск разных CRT; здесь `std::string&` уже опасен;
- запрет на исключения через границу;
- те же компилятор, стандартная библиотека, разрядность и набор флагов у хоста и плагина.

Этот вариант удобнее в разработке, но хрупок при обновлениях: изменение любого класса ломает все плагины, а значит требуется пересборка. Для внутренних плагинов это нормально, для публичного SDK — нет.

## 7. Статические плагины и саморегистрация

Когда набор расширений известен на этапе сборки, динамическая загрузка не нужна:

```cpp
// registry.h
class PluginRegistry {
public:
    using Factory = std::unique_ptr<IPlugin> (*)();

    static PluginRegistry& instance() {
        static PluginRegistry r;
        return r;
    }

    void add(std::string id, Factory f) { factories_.emplace(std::move(id), f); }

    std::unique_ptr<IPlugin> create(const std::string& id) const {
        auto it = factories_.find(id);
        return it == factories_.end() ? nullptr : it->second();
    }

private:
    std::unordered_map<std::string, Factory> factories_;
};

template <class T>
struct PluginRegistrar {
    explicit PluginRegistrar(const char* id) {
        PluginRegistry::instance().add(id, [] { return std::make_unique<T>(); });
    }
};

#define REGISTER_PLUGIN(TYPE, ID) \
    namespace { static ::PluginRegistrar<TYPE> g_reg_##TYPE(ID); }
```

```cpp
// my_plugin.cpp
class MyPlugin final : public IPlugin { /* ... */ };
REGISTER_PLUGIN(MyPlugin, "com.example.my")
```

Подводные камни:

- **Статические библиотеки**: линкер может выбросить объектный файл, если из него нечего использовать. Лечится `--whole-archive`/`/WHOLEARCHIVE`, `OBJECT`-библиотеками в CMake или явным списком регистраций.
- **Порядок инициализации статики**: регистрация в глобальном конструкторе до готовности реестра — классическая ошибка. Лучше «ленивый» реестр-синглтон (как выше, через локальную статику) или явная функция `register_builtin_plugins()`.
- **Время жизни**: глобальные объекты уничтожаются в обратном порядке — не стоит в их деструкторах обращаться к реестру.

## 8. Хост-сервисы: как плагин общается с программой

Плагину почти всегда нужны возможности хоста: логирование, доступ к документу, выделение памяти, регистрация команд. Три способа:

1. **Таблица указателей на функции** (`HostApi` выше) — самый переносимый и стабильный.
2. **Абстрактный интерфейс** `IHost&` — удобнее, но требует совместимости C++ ABI.
3. **Событийная шина** (имена событий + `void*`-полезная нагрузка или сериализованный формат) — гибко, но медленнее и менее типобезопасно.

Для публичного SDK почти всегда выбирают (1) плюс, при необходимости, тонкую C++-обёртку над ним внутри заголовков.

## 9. Обнаружение плагинов

Разумный минимум:

1. Каталог(и) плагинов: рядом с исполняемым файлом, в пользовательском каталоге, пути из переменной окружения/конфига.
2. Соглашение об именах файлов (`*.dll`, `*.so`, `*.dylib`) либо отдельный манифест.
3. **Манифест** (JSON/TOML) с `id`, версией, точкой входа, зависимостями, хешем/подписью — позволяет не загружать библиотеку, чтобы прочитать метаданные, и отсеивать несовместимые плагины заранее.
4. Кэш просканированного (путь + mtime + размер → метаданные), чтобы не открывать сотни библиотек при каждом старте.
5. Обработка дубликатов `id`: приоритет по каталогу, версии или явной настройке.

Пример манифеста:

```json
{
  "id": "com.example.echo",
  "name": "Echo plugin",
  "version": "1.0.0",
  "abi": 3,
  "entry": "plugin_get_descriptor",
  "library": "echo.dll",
  "capabilities": ["text.transform"]
}
```

## 10. Жизненный цикл и горячая перезагрузка

Типичные состояния: **найден → проверен → загружен → инициализирован → активен → остановлен → выгружен**.

Правила, которые экономят недели отладки:

- `init` должен быть отдельным шагом: после `dlopen` библиотека уже может падать, поэтому полезно иметь «безопасный» режим загрузки и таймаут инициализации (в отдельном потоке).
- Плагин не должен сам решать, когда выгружаться; это делает хост.
- Для горячей перезагрузки нужен учёт ссылок: новые объекты не создаются, старые доживают, затем `dlclose`. На практике проще перезагружать не библиотеку, а её *экземпляры*, оставляя код загруженным до перезапуска процесса.
- На Linux `dlclose` может не выгрузить библиотеку из-за статического TLS или `RTLD_NODELETE`; на Windows выгрузка при активных потоках — прямой путь к падению. Поэтому «горячая перезагрузка» часто реализуется через отдельный процесс-сателлит.

## 11. Ошибки, диагностика, изоляция

- **Коды возврата** вместо исключений на границе; отдельный тип `enum class PluginStatus`.
- **Канал диагностики**: `host->log(level, msg)` — плагин не должен писать в `stdout`/`stderr` напрямую.
- **Проверка входа**: `nullptr`, размеры буферов, кодировка, лимиты.
- **Зависания**: таймаут на вызов (в отдельном потоке) и возможность аварийно завершить работу с плагином.
- **Падения**: в одном процессе падение плагина роняет всё приложение. Если это неприемлемо — выносите плагины в отдельные процессы (gRPC/именованные каналы/разделяемая память) или в WASM-песочницу.

## 12. Безопасность

Загруженная библиотека исполняется с полными правами процесса — это не «песочница», а «доверенный код». Поэтому:

- не грузить плагины из каталогов, доступных на запись непривилегированным пользователям;
- проверять подпись/хеш манифеста и самой библиотеки;
- на Windows использовать `LoadLibraryEx` с `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS` и заранее вызвать `SetDefaultDllDirectories`, чтобы исключить подмену зависимых DLL;
- на Linux — `dlopen` по абсолютному пути, без `RPATH` на пользовательские каталоги;
- для недоверенных расширений — только отдельный процесс с урезанными правами или WASM.

## 13. Сборка (CMake)

```cmake
# Плагин — MODULE, а не SHARED: это библиотека для dlopen/LoadLibrary,
# её не нужно линковать к другим целям.
add_library(echo_plugin MODULE echo_plugin.cpp)
target_include_directories(echo_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(echo_plugin PRIVATE cxx_std_17)

set_target_properties(echo_plugin PROPERTIES
    PREFIX ""                                # без "lib" на Unix
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

# Хост
add_executable(app main.cpp)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(app PRIVATE PLUGIN_ABI_VERSION=3)
```

Полезные детали:

- `MODULE` не создаёт импортную библиотеку и не участвует в линковке хоста.
- Один и тот же заголовок контракта — через `target_include_directories` у обеих целей.
- Для проверки ABI в CI: собрать «эталонный» плагин старым компилятором и загрузить его новым хостом; плюс `abi-compliance-checker`/`abidiff` для сравнения версий SDK.
- Санитайзеры (`ASan`/`UBSan`) отлично ловят нарушения на границе, если собрать с ними и хост, и плагин.

## 14. Готовые решения

- **Boost.DLL** — кроссплатформенная обёртка над `dlopen`/`LoadLibrary`: `boost::dll::shared_library`, `import_symbol`, `load_mode::append_decorations`. ABI-проблему не решает, но убирает платформенную рутину.
- **Qt Plugin System** — `Q_DECLARE_INTERFACE`, `Q_PLUGIN_METADATA`, `QPluginLoader`, `qobject_cast`. Удобно, если приложение уже на Qt; требует совместимой версии Qt и moc.
- **Poco::SharedLibrary / Poco::ClassLoader** — то же для Poco.
- **Встроенные движки**: Lua + sol2, Python (pybind11), QuickJS, Wasmtime — когда «плагин» пишут пользователи, а не разработчики.

## 15. Типичные ошибки

1. `std::string`/`std::vector` в сигнатурах через границу модуля.
2. Исключение, вылетающее из плагина в хост.
3. `new` в плагине, `delete` в хосте (разные CRT/аллокаторы).
4. Забытый `extern "C"` — символ не находится через `dlsym`.
5. Сборка с `-fvisibility=hidden` без экспорта точки входа.
6. Изменение структуры дескриптора без смены версии ABI.
7. Выгрузка библиотеки при живых объектах плагина.
8. Отсутствие проверки `nullptr` и версий на границе.
9. Глобальные конструкторы в плагине, выполняющие тяжёлую работу при `dlopen`.
10. Предположение о потокобезопасности: контракт должен явно говорить, какие методы можно звать параллельно.
11. Смешивание debug/release и разных CRT на Windows.
12. Загрузка плагинов из недоверенных каталогов.

## 16. Чек-лист

- [ ] Контракт вынесен в отдельный заголовок, общий для хоста и плагинов.
- [ ] Граница — только C-совместимые типы и `extern "C"`-функции.
- [ ] Есть версия ABI и `struct_size` для расширения структур.
- [ ] Определено владение памятью и временем жизни всех возвращаемых данных.
- [ ] Исключения не пересекают границу; ошибки — кодами возврата.
- [ ] Есть `create`/`destroy`, деструктор виртуальный (если интерфейс C++).
- [ ] Определён порядок: `destroy` объектов → `dlclose`.
- [ ] Есть канал логирования и человекочитаемая диагностика загрузки.
- [ ] Реализован поиск плагинов и обработка дубликатов/несовместимых версий.
- [ ] Продумана изоляция: таймауты, а для недоверенного кода — отдельный процесс/WASM.
- [ ] Есть тесты: эталонный плагин, проверка ABI в CI, санитайзеры.
- [ ] Описана политика безопасности: откуда можно грузить и как проверяется подпись.

**Итог.** Для большинства задач оптимален вариант «C ABI + таблица хост-сервисов + динамические библиотеки»: он переносим, переживает смену компилятора и позволяет писать плагины не только на C++. C++-интерфейсы с виртуальными функциями уместны для внутренних расширений, собираемых вместе с хостом, а для недоверенного или нестабильного кода единственный по-настоящему надёжный путь — изоляция в отдельном процессе или WASM-песочнице.