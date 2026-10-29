# 19. Как интегрировать third-party в проект?

**Если сжать до одного абзаца:** интеграция сторонней библиотеки — это всегда настройка трёх независимых каналов: (1) **препроцессор** должен найти заголовки и получить нужные `#define`, (2) **компилятор** должен собрать код с совместимыми флагами, (3) **компоновщик** должен получить нужные `.lib`/`.a`/`.so`/`.dll` в правильном порядке. Почти все «непонятные» ошибки интеграции — это рассинхронизация одного из трёх каналов.

---

## 1. Три канала интеграции

| Канал | Кто потребляет | Как задаётся вручную | Как задаётся в CMake |
|---|---|---|---|
| Заголовки | препроцессор (`#include`) | `-I path` / `/I path` | `target_include_directories` |
| Макросы конфигурации | препроцессор (`#if`) | `-DNAME=VALUE` / `/DNAME=VALUE` | `target_compile_definitions` |
| Бинарники | компоновщик | `-L path -lfoo` / `foo.lib` | `target_link_libraries` |

Ключевая мысль: **подключить заголовок — это ещё не интегрировать библиотеку**. Заголовок лишь объявляет сущности; реализация приходит либо из заголовка (header-only), либо из объектного кода на этапе компоновки. Отсюда два принципиально разных сценария.

---

## 2. Способы получить зависимость

| Способ | Плюсы | Минусы |
|---|---|---|
| **Header-only** (копия в `third_party/`) | ничего линковать не надо, тривиально | раздувание времени сборки, версии вручную |
| **Vendoring** (копия исходников в репозиторий) | воспроизводимость, полный контроль | вес репозитория, ручное обновление |
| **git submodule** | версия зафиксирована, код рядом | неудобно для пользователей, нужен `--recursive` |
| **FetchContent / CPM.cmake** | версия в CMake, автоматическая загрузка | нужен интернет на конфигурации, риск «поплыть» без пиннинга |
| **vcpkg / Conan** | зависимости зависимостей, бинарный кэш, единый инструмент | отдельный инструмент, toolchain-файл, ABI-профили |
| **Системный пакет** (`apt`, `brew`, `pacman`) | привычно, быстро | версия зависит от дистрибутива, воспроизводимость страдает |
| **Готовая сборка** (`.lib` + `.h`, SDK) | не надо собирать | надо угадать ABI/CRT/разрядность |

---

## 3. Header-only библиотеки

Самый простой случай: всё в заголовках, компоновщику ничего не нужно.

```cpp
// main.cpp
#include <nlohmann/json.hpp>   // -I third_party/nlohmann_json/include
```

```bash
# GCC/Clang
g++ -std=c++20 -Ithird_party/nlohmann_json/include main.cpp -o app
# MSVC
cl /std:c++20 /Ithird_party\nlohmann_json\include main.cpp
```

В CMake правильнее не раздавать путь руками, а создать `INTERFACE`-цель — тогда путь и макросы «прилипают» к цели и автоматически доезжают до всех потребителей:

```cmake
add_library(jsonlib INTERFACE)
target_include_directories(jsonlib INTERFACE third_party/nlohmann_json/include)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE jsonlib)   # да, INTERFACE-цель тоже "линкуют"
```

**Особый подвид — single-header библиотеки с реализацией в том же файле** (stb, doctest, fmt в header-only режиме). Они требуют, чтобы реализация была включена ровно в **одной** единице трансляции, иначе получите `multiple definition` / `LNK2005`:

```cpp
// ровно в одном .cpp проекта
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#define FMT_HEADER_ONLY
#include <fmt/format.h>
```

Это чистый препроцессорный механизм: макрос переключает, какая часть заголовка попадёт в трансляцию.

---

## 4. Скомпилированные библиотеки: static и shared

### 4.1. Что физически нужно

| Платформа | Статическая | Динамическая |
|---|---|---|
| Windows / MSVC | `foo.lib` (+ заголовки) | `foo.lib` (import library) + `foo.dll` в рантайме |
| Linux | `libfoo.a` | `libfoo.so` (+ soname-симлинки) |
| macOS | `libfoo.a` | `libfoo.dylib` |

Различайте: на Windows расширение `.lib` носят и статическая библиотека, и **импортная** библиотека для DLL — это разные файлы с одинаковым суффиксом.

### 4.2. Вручную

```bash
# Linux, статическая: порядок важен!
g++ -std=c++20 -Ithird_party/foo/include main.cpp \
    -Lthird_party/foo/lib -lfoo -pthread -o app

# Windows, MSVC
cl /std:c++20 /Ithird_party\foo\include main.cpp third_party\foo\lib\foo.lib
```

Порядок `-l` в GCC/Clang: библиотека должна стоять **после** объектных файлов, которые её используют, а зависимая библиотека — после своей зависимости. Для циклических зависимостей:

```bash
-Wl,--start-group -lfoo -lbar -Wl,--end-group
```

### 4.3. Через CMake — единственный правильный способ

```cmake
find_package(fmt CONFIG REQUIRED)          # ищет fmtConfig.cmake
find_package(Boost REQUIRED COMPONENTS filesystem)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE
    fmt::fmt                                # импортированная цель
    Boost::filesystem                       # импортированная цель
)
```

Смысл импортированных целей: они несут в себе **сразу все три канала** — `INTERFACE_INCLUDE_DIRECTORIES`, `INTERFACE_COMPILE_DEFINITIONS`, `INTERFACE_LINK_LIBRARIES`. Вам не надо вручную помнить про `-DFMT_SHARED` или про транзитивные зависимости.

Если готового `config`-файла нет:

```cmake
find_path(FOO_INCLUDE_DIR foo/foo.h)
find_library(FOO_LIBRARY NAMES foo)
add_library(foo::foo UNKNOWN IMPORTED)
set_target_properties(foo::foo PROPERTIES
    IMPORTED_LOCATION "${FOO_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${FOO_INCLUDE_DIR}")
```

### 4.4. DLL и экспорт символов

При сборке DLL символы надо явно экспортировать, а у потребителя — импортировать. Это делается макросом, который переключается препроцессором:

```cpp
// foo_export.h
#if defined(_WIN32)
#  ifdef FOO_BUILD_DLL
#    define FOO_API __declspec(dllexport)
#  else
#    define FOO_API __declspec(dllimport)
#  endif
#else
#  define FOO_API __attribute__((visibility("default")))
#endif
```

```cmake
target_compile_definitions(foo PRIVATE FOO_BUILD_DLL)   # при сборке самой библиотеки
target_compile_definitions(foo PUBLIC  FOO_USE_DLL)     # доедет до потребителей
```

В CMake есть готовый генератор такого заголовка: `include(GenerateExportHeader)` → `generate_export_header(foo)`.

**Рантайм:** DLL должна лежать рядом с `.exe` (или в `PATH`); `.so` — в стандартном пути, либо через `-Wl,-rpath,'$ORIGIN'`, либо `LD_LIBRARY_PATH`. Иначе программа соберётся, но не запустится: `error while loading shared libraries`.

---

## 5. Пакетные менеджеры

### vcpkg (manifest mode)

`vcpkg.json`:
```json
{
  "name": "myapp",
  "version": "1.0.0",
  "dependencies": ["fmt", "spdlog", { "name": "imgui", "features": ["docking-experimental"] }]
}
```
Конфигурация: `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`. Дальше — обычный `find_package(fmt CONFIG REQUIRED)`.

### Conan 2

`conanfile.txt`:
```
[requires]
fmt/10.2.1
[generators]
CMakeDeps
CMakeToolchain
```
```bash
conan install . --output-folder=build --build=missing
cmake -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake
```

### FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        10.2.1        # всегда пиннить!
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(fmt)
```

Правила гигиены: пиннить тег или `URL_HASH`, отключать чужие тесты/примеры (`FOO_BUILD_TESTS OFF`), не тянуть `master`.

### pkg-config (Linux)

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
target_link_libraries(app PRIVATE PkgConfig::GTK4)
```

---

## 6. Препроцессорные ловушки — главная часть

Именно здесь интеграция «ломается молча», без ошибок компиляции.

### 6.1. Загрязнение макропространства

Классика — `windows.h`:

```cpp
// плохо: min/max ломают std::min, std::max и любой шаблонный код
#include <windows.h>
#include <algorithm>
```

```cpp
// хорошо
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif
```

Лучше — не в коде, а в сборке, чтобы макрос действовал **до** любого включения:

```cmake
target_compile_definitions(app PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
```

Другие известные «агрессоры»: `X11.h` (`None`, `Status`, `Success`, `Bool`, `True`, `False`, `Above`, `Below`, `Expose`), `rpc.h` (`small`, `near`, `far`), `ERROR`/`DELETE`/`interface` из WinAPI. Точечное лечение:

```cpp
#pragma push_macro("None")
#undef None
#include <third_party/gui.h>
#pragma pop_macro("None")
```

`push_macro`/`pop_macro` поддерживаются MSVC, GCC и Clang.

### 6.2. Порядок включения

Некоторые заголовки читают макросы **в момент включения** (`#if defined(FOO_STATIC)`), поэтому `#define` обязан стоять раньше `#include`. Правило: конфигурационные макросы задаются флагами компилятора (`-D`), а не строками в `.cpp` — тогда порядок не зависит от аккуратности автора.

### 6.3. Изоляция предупреждений

Не применяйте `-Werror` к чужому коду. В CMake подключение через `SYSTEM` даёт `-isystem` и глушит диагностику из этих заголовков:

```cmake
target_include_directories(app SYSTEM PRIVATE third_party/foo/include)
```

У импортированных целей `INTERFACE_INCLUDE_DIRECTORIES` и так считается системным.

### 6.4. Один заголовок — разные макросы в разных TU = ODR-нарушение

Самый коварный случай. Заголовок содержит `inline`-функцию:

```cpp
// foo.h
inline int mode() {
#ifdef FOO_FAST
    return 1;
#else
    return 0;
#endif
}
```

Если `a.cpp` собран с `-DFOO_FAST`, а `b.cpp` — без, линкер выберет **одно** определение, и второй TU получит не то поведение. Диагностики не будет — это undefined behavior по ODR. Вывод: **набор макросов, влияющих на содержимое заголовков, должен быть одинаковым для всей программы**. Именно поэтому в CMake используют `PUBLIC`/`INTERFACE`, а не `PRIVATE`, для define'ов, меняющих ABI/поведение.

### 6.5. C-библиотеки и `extern "C"`

C-заголовки нельзя включать в C++ без указания linkage, иначе name mangling сделает символы несовместимыми (`undefined reference` на ровном месте):

```cpp
extern "C" {
#include <zlib.h>
}
```

Хорошая C-библиотека сама содержит:
```c
#ifdef __cplusplus
extern "C" {
#endif
/* ... */
#ifdef __cplusplus
}
#endif
```

### 6.6. Транзитивные включения и PCH

Не полагайтесь на то, что `foo.h` «случайно» тянет `<vector>`. И наоборот: **precompiled header должен собираться с тем же набором `-D`, что и потребители**, иначе MSVC молча пересоберёт или выдаст несовместимость. В CMake: `target_precompile_headers(app PRIVATE <vector> <string>)` — и не кладите туда заголовки, зависящие от конфигурационных макросов.

---

## 7. ABI-совместимость

Заголовки совпали — это только половина дела. Объектный код должен быть совместим по ABI.

| Что должно совпадать | Что ломается при несовпадении |
|---|---|
| Компилятор (и major-версия) | name mangling, layout |
| Стандартная библиотека (libstdc++ / libc++ / MSVC STL) | `std::string`, исключения, аллокаторы |
| Версия стандарта C++ | ABI некоторых типов, inline-переменные |
| **`/MD` vs `/MT`** (MSVC) | две копии CRT, `LNK2038`, падения на `free` |
| Debug vs Release (MSVC) | `_ITERATOR_DEBUG_LEVEL` 2 vs 0, `LNK2038` |
| `_GLIBCXX_USE_CXX11_ABI` (GCC) | старый/новый ABI `std::string` (COW vs SSO) |
| RTTI и исключения (`/GR`, `/EHsc`, `-fno-rtti`) | `dynamic_cast`, `type_info`, раскрутка стека |
| Упаковка структур (`#pragma pack`, выравнивание) | «мусор» в полях, порча памяти |
| Соглашение о вызове (x86: `__cdecl`/`__stdcall`) | порча стека |
| Разрядность (x86 / x64 / ARM64) | вообще не слинкуется |

Практическое следствие для Windows: **нельзя** смешивать библиотеку, собранную с `/MT`, и проект с `/MD` — память, выделенная в одной CRT, не должна освобождаться в другой. Отсюда правило: выделяй и освобождай в пределах одной библиотеки (никаких `malloc` в DLL → `free` в exe).

---

## 8. Типичные ошибки и их истинные причины

| Симптом | Реальная причина |
|---|---|
| `undefined reference to 'foo_init'` / `LNK2019` | не подключена библиотека; неверный порядок `-l`; C-функция без `extern "C"`; не тот ABI |
| `LNK2038: mismatch detected for 'RuntimeLibrary'` | `/MT` vs `/MD` |
| `LNK2038: mismatch detected for '_ITERATOR_DEBUG_LEVEL'` | Debug-библиотека в Release-проекте |
| `LNK2005` / `multiple definition` | реализация в заголовке без `inline`; два экземпляра библиотеки; не задан «один TU с `*_IMPLEMENTATION`» |
| `cannot open shared object file` / DLL не найдена | проблема **рантайма**, не сборки: путь к `.so`/`.dll` |
| Компилируется, но падает в чужом коде | ODR-нарушение из-за разных `-D` в TU |
| `min`/`max` — «не шаблон, а макрос» | `windows.h` без `NOMINMAX` |

Полезная диагностика (когда всё-таки нужно): `g++ -E` / `cl /P` — что реально увидел препроцессор; `g++ -H` / `-M` — откуда взялся заголовок; `nm -C`, `dumpbin /symbols` — какие символы есть в библиотеке; `ldd`, `otool -L` — что подхватилось в рантайме.

---

## 9. Практический чек-лист

1. **Не раздавайте пути руками** — заведите цель (импортированную, `INTERFACE` или собранную `add_subdirectory`), чтобы заголовки, define'ы и линковка шли одним пакетом.
2. **Заголовки — через `SYSTEM`**, свои предупреждения — отдельно; `-Werror` на чужой код не распространяйте.
3. **Конфигурационные макросы — флагами `-D`**, а не `#define` в случайном месте; для влияющих на ABI используйте `PUBLIC`.
4. **Одинаковые макросы во всех TU** — иначе ODR.
5. **Пинните версию**: тег, `URL_HASH`, lock-файл пакетного менеджера.
6. **Соберите одну конфигурацию целиком**: один компилятор, одна stdlib, один CRT, один режим Debug/Release, один набор санитайзеров.
7. **Проверяйте не только сборку, но и запуск** — путь к DLL/`.so` это часть интеграции.
8. **C-библиотеки — только через `extern "C"`**, C++-библиотеки — только с гарантированно совпадающим ABI; для публичного API предпочитайте C-интерфейс, он стабилен.
9. **Изолируйте зависимость**: тонкая обёртка над чужим API упрощает замену библиотеки и не даёт её макросам расползтись по проекту.