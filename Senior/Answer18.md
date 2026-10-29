# 18. Как работать с билд-системами: Make, CMake.

## 1. Зачем нужна билд-система

Компиляция C++ — это конвейер из четырёх шагов, и билд-система управляет первыми тремя для каждой единицы трансляции, а затем вызывает линковщик:

```
foo.cpp ──(cpp: препроцессор)──> foo.ii ──(cc1plus: компилятор)──> foo.s ──(as)──> foo.o ──┐
bar.cpp ──...──────────────────────────────────────────────────────────────────────────────┼──(ld)──> app
                                                                                            │
lib.a / lib.so ─────────────────────────────────────────────────────────────────────────────┘
```

Собирать вручную (`g++ -std=c++20 -Iinclude src/*.cpp -o app`) можно только для одного-двух файлов. Как только проект разрастается, появляются задачи, которые билд-система берёт на себя:

- **инкрементальность** — пересобирать только то, что реально устарело (по mtime и графу зависимостей), а не весь проект;
- **граф зависимостей** — объектник зависит не только от `.cpp`, но и от всех включённых заголовков; изменился `config.h` — должны пересобраться все, кто его включает;
- **параллелизм** — независимые цели собираются одновременно (`make -j`, Ninja);
- **портируемость** — разные компиляторы, флаги, пути, разделители, расширения файлов;
- **масштабирование** — сотни целей, библиотек, внешних зависимостей, генерация кода;
- **конфигурации** — Debug/Release, разные наборы `-D`-макросов (что напрямую влияет на препроцессор).

Ключевая идея: **билд-система — это декларативное описание графа зависимостей**, а не скрипт «сделай шаг 1, шаг 2». Движок сам решает, что и в каком порядке запускать.

---

## 2. Make

### 2.1 Модель: цель — зависимости — рецепт

Правило (rule) состоит из трёх частей:

```make
цель: зависимость1 зависимость2
<TAB>команда сборки цели
```

Семантика: «если цель отсутствует или **старше** любой из зависимостей — выполнить рецепт». Это и есть механизм инкрементальной сборки; никакой магии, только сравнение времени модификации.

### 2.2 Минимальный Makefile

```make
# ВАЖНО: строки рецептов начинаются с TAB, а не с пробелов!
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic

app: main.o util.o
	$(CXX) $^ -o $@

main.o: main.cpp util.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

util.o: util.cpp util.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f app *.o
```

### 2.3 Переменные и автоматические переменные

| Переменная | Смысл |
|---|---|
| `$@` | имя цели |
| `$<` | первая зависимость (обычно исходник) |
| `$^` | все зависимости, без дублей |
| `$+` | все зависимости, с дублями |
| `$*` | «стем» в шаблонном правиле (`%`) |
| `$(@D)`, `$(@F)` | каталог и имя файла цели |

Присваивания: `=` (ленивое), `:=` (немедленное), `?=` (если не задано), `+=` (дописать). Переменные окружения и аргументы командной строки (`make CXX=clang++`) перекрывают значения из Makefile, если не использовать `override`.

Стандартные имена, которые ожидают пользователи и внешние скрипты:

- `CPPFLAGS` — флаги препроцессора (`-I`, `-D`, `-U`);
- `CXXFLAGS` / `CFLAGS` — флаги компилятора;
- `LDFLAGS` — флаги линковщика (`-L`);
- `LDLIBS` — библиотеки (`-l`).

Именно `CPPFLAGS` — то место, где живёт работа препроцессора: пути поиска заголовков и определения макросов.

### 2.4 Шаблонные правила и функции

```make
SRC_DIR   := src
BUILD_DIR := build

SOURCES := $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
```

`%` — это «любая подстрока»; правило с `%` называется *pattern rule* и применяется ко всем подходящим целям. Функции `wildcard`, `patsubst`, `subst`, `dir`, `notdir`, `addprefix`, `filter`, `shell` дают минимальный язык программирования.

### 2.5 `.PHONY` и служебные цели

```make
.PHONY: all clean test
all: app
```

Без `.PHONY` цель `clean` ломается, если в каталоге случайно появится файл с именем `clean`. Типовой набор: `all` (по умолчанию — первая цель), `clean`, `test`, `install`, `dist`.

### 2.6 Главная проблема: зависимости от заголовков

Make **не знает** о `#include`. Если перечислить заголовки руками, рано или поздно кто-то забудет — и получится классический баг «изменил `.h`, а поведение старое». Правильное решение — заставить сам компилятор сгенерировать список зависимостей:

```make
DEPS := $(OBJECTS:.o=.d)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

-include $(DEPS)
```

- `-MMD` — писать `.d`-файл с пользовательскими заголовками (без системных);
- `-MP` — добавить фиктивные цели для заголовков, чтобы удаление заголовка не ломало сборку;
- `-include` (с минусом) — не считать отсутствие `.d` ошибкой на первой сборке;
- `| $(BUILD_DIR)` — *order-only prerequisite*: каталог должен существовать, но его mtime не должен инициировать пересборку объектов.

### 2.7 Параллельная сборка и порядок

```bash
make -j$(nproc)          # или make -j8
make -n                  # dry-run: показать команды
make -B                  # пересобрать всё принудительно
make -k                  # продолжать после ошибок
make -p                  # распечатать базу правил и переменных
```

`make -j` корректен только если граф зависимостей описан полностью. Порядок имеет значение для сгенерированных файлов: генератор заголовка должен отработать **до** компиляции тех, кто его включает — это выражается явной зависимостью, а не порядком строк.

### 2.8 Полный пример с отдельным каталогом сборки

```make
CXX      ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra
CPPFLAGS ?=
LDFLAGS  ?=
LDLIBS   ?=

SRC_DIR   := src
BUILD_DIR := build
TARGET    := $(BUILD_DIR)/app

SOURCES := $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS    := $(OBJECTS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

-include $(DEPS)

.PHONY: all clean
clean:
	rm -rf $(BUILD_DIR)
```

Конфигурации обычно делают через переменные (`make BUILD_DIR=build-debug CPPFLAGS=-DDEBUG`), а не через отдельные Makefile.

### 2.9 Ограничения Make

- язык рецептов — shell, поэтому нет переносимости (Windows требует `nmake`/MSYS, `mkdir -p` и `rm -rf` не работают в `cmd`);
- нет обнаружения зависимостей «из коробки» (нужен `-MMD`);
- нет поиска и версионирования внешних библиотек — только `-l`/`-L` и надежда на `pkg-config`;
- рекурсивный Make (`$(MAKE) -C sub`) ломает общий граф и параллелизм;
- табы, пробелы, `$` в путях — источники неочевидных ошибок;
- правила вида «сгенерируй файл» пишутся вручную.

---

## 3. CMake

### 3.1 Что это такое

CMake — **не сборщик**, а **генератор систем сборки**. Он читает `CMakeLists.txt`, проверяет компилятор и окружение, строит внутреннюю модель целей и генерирует файлы для конкретного *генератора*: `Unix Makefiles`, `Ninja`, `Visual Studio`, `Xcode`, `Ninja Multi-Config`. Дальше сборку выполняет уже Make/Ninja/MSBuild.

Отсюда двухфазность:

```bash
cmake -S . -B build            # фаза конфигурации: читает CMakeLists.txt, создаёт кэш
cmake --build build -j 8       # фаза сборки: вызывает сгенерированный сборщик
```

**Правило №1: сборка вне исходников (out-of-source).** Каталог `build/` создаётся отдельно и не коммитится; удалить его — значит сбросить состояние. Внутри исходников CMake работать умеет, но это гарантированные конфликты и мусор.

### 3.2 Минимальный CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyApp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)   # -std=c++20, а не -std=gnu++20

add_executable(app src/main.cpp src/util.cpp)
target_include_directories(app PRIVATE include)
```

`cmake_minimum_required` задаёт политики совместимости — его нужно поднимать при использовании новых возможностей. `project()` обязателен и задаёт имя, версию и языки.

### 3.3 Modern CMake: цели и их свойства

Главный принцип современного CMake: **не трогать глобальные переменные, а настраивать цели**. Всё выражается через `target_*`:

```cmake
add_library(core STATIC src/core.cpp src/parser.cpp)
target_include_directories(core
    PUBLIC  include          # нужно и самой библиотеке, и её потребителям
    PRIVATE src              # нужно только при сборке core
)
target_compile_features(core PUBLIC cxx_std_20)
target_compile_definitions(core PRIVATE CORE_BUILD=1)
target_compile_options(core PRIVATE -Wall -Wextra)

add_executable(app src/main.cpp)
target_link_libraries(app PRIVATE core)
```

Ключевые слова `PUBLIC` / `PRIVATE` / `INTERFACE` — это и есть механизм транзитивного распространения требований:

- `PRIVATE` — только для сборки самой цели;
- `INTERFACE` — только для потребителей (header-only библиотеки);
- `PUBLIC` = `PRIVATE` + `INTERFACE`.

Так «связывание с `core`» автоматически приносит потребителю нужные `-I`, `-D` и зависимости, и в проекте не появляется глобальный `include_directories(...)`. Устаревшие команды `include_directories`, `link_libraries`, `add_definitions`, `set(CMAKE_CXX_FLAGS ...)` лучше не использовать: они протекают на все цели подкаталога.

### 3.4 Многофайловый проект

```
project/
├── CMakeLists.txt
├── include/core/core.h
├── src/
│   ├── CMakeLists.txt
│   ├── core/core.cpp
│   └── app/main.cpp
└── tests/
    └── CMakeLists.txt
```

```cmake
# корневой CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(MyApp VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src)
option(MYAPP_BUILD_TESTS "Build tests" ON)
if(MYAPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

```cmake
# src/CMakeLists.txt
add_library(core STATIC core/core.cpp)
target_include_directories(core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(core PUBLIC cxx_std_20)

add_executable(app app/main.cpp)
target_link_libraries(app PRIVATE core)
```

### 3.5 Типы библиотек

- `STATIC` → `.a` / `.lib` — код копируется в исполняемый файл;
- `SHARED` → `.so` / `.dll` — динамическая линковка, нужен `-fPIC` (CMake ставит его сам);
- `MODULE` — плагин, не линкуется на этапе сборки;
- `INTERFACE` — библиотека без исходников, только требования (header-only, например `fmt::fmt-header-only`);
- `OBJECT` — набор объектных файлов без архива;
- `add_library(... ALIAS ...)` — удобное имя (`core::core`).

### 3.6 Конфигурации сборки

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release     # для многоконфигурационных генераторов (VS, Xcode)
```

Стандартные значения: `Debug` (`-g`), `Release` (`-O3 -DNDEBUG`), `RelWithDebInfo`, `MinSizeRel`. Многоконфигурационные генераторы хранят все конфигурации в одном дереве; одноконфигурационные (Make, Ninja) — требуют отдельного `build/` на конфигурацию.

**Важно для препроцессора:** `-DNDEBUG` в Release меняет семантику `assert`, а разные `-D` могут менять layout структур и inline-функции. Смешивать объекты из Debug и Release в одном линковании нельзя — это ODR-нарушение, которое проявится непредсказуемо. Отсюда правило: своя директория сборки на каждую конфигурацию.

### 3.7 Внешние зависимости

Три способа, по возрастанию «жадности»:

```cmake
# 1. Системный пакет (vcpkg / Conan / системный менеджер)
find_package(fmt CONFIG REQUIRED)
target_link_libraries(app PRIVATE fmt::fmt)

# 2. Сборка зависимости из исходников внутри проекта
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googletest)

# 3. pkg-config для «старых» библиотек
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
target_link_libraries(app PRIVATE PkgConfig::ZLIB)
```

`find_package(... REQUIRED)` без `CONFIG` идёт по модулям `Find*.cmake`; с `CONFIG` — ищет `*Config.cmake`, который экспортирует импортированные цели (их и надо линковать, а не `-lfoo` вручную).

### 3.8 Генераторы и производительность

```bash
cmake -S . -B build -G Ninja          # быстрее Make, лучше параллелизм
cmake -S . -B build -G "Unix Makefiles"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

Ninja обычно заметно быстрее Make на больших проектах за счёт минимального оверхеда и точного планирования. Список доступных генераторов: `cmake --help`.

### 3.9 Presets — воспроизводимость

Чтобы не запоминать десятки `-D`-флагов, конфигурации фиксируют в `CMakePresets.json` (коммитится в репозиторий):

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/debug",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" }
    },
    {
      "name": "release",
      "inherits": "debug",
      "binaryDir": "${sourceDir}/build/release",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
    }
  ],
  "buildPresets": [
    { "name": "debug",   "configurePreset": "debug" },
    { "name": "release", "configurePreset": "release" }
  ]
}
```

```bash
cmake --preset debug
cmake --build --preset debug -j
ctest --preset debug
```

### 3.10 Кросс-компиляция и toolchain

Всё, что относится к «другой платформе», выносится в toolchain-файл:

```cmake
# toolchain-arm.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
set(CMAKE_FIND_ROOT_PATH /opt/arm-sysroot)
```

```bash
cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE=toolchain-arm.cmake
```

### 3.11 Установка, тесты, упаковка

```cmake
include(GNUInstallDirs)
install(TARGETS app core
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE core GTest::gtest_main)
add_test(NAME core COMMAND test_core)
```

```bash
cmake --install build --prefix /usr/local
ctest --test-dir build --output-on-failure
cpack                      # если подключён CPack
```

---

## 4. Препроцессор в контексте билд-систем

Это самая частая точка пересечения теории препроцессора и практики сборки.

| Задача | Make | CMake |
|---|---|---|
| Пути поиска заголовков | `CPPFLAGS += -Iinclude` | `target_include_directories(t PUBLIC include)` |
| Определить макрос | `CPPFLAGS += -DDEBUG=1` | `target_compile_definitions(t PRIVATE DEBUG=1)` |
| Отменить макрос | `-UFOO` | `target_compile_definitions(t PRIVATE FOO)` нельзя — использовать `-U` в опциях |
| Системные заголовки | `-isystem /path` | `target_include_directories(t SYSTEM PUBLIC ...)` |
| Сгенерированный заголовок | явная зависимость + правило | `add_custom_command(OUTPUT ...)` / `configure_file` |
| Зависимости от `#include` | `-MMD -MP` + `-include *.d` | автоматически (depfile от генератора) |
| Предкомпилированные заголовки | вручную `.gch` | `target_precompile_headers(t PRIVATE pch.h)` |

Несколько практических следствий:

1. **Порядок `-I` важен.** `#include <foo.h>` ищется по каталогам в порядке их перечисления; при двух разных `foo.h` победит первый. В CMake это означает, что `PUBLIC`-пути библиотеки подмешиваются к потребителю в порядке линковки, и конфликт одноимённых заголовков — реальная проблема, которую лучше решать уникальными именами/каталогами.

2. **`"..."` vs `<...>`.** Кавычечная форма сначала ищет рядом с включающим файлом, затем по `-I`; угловая — только по `-I`/`-isystem`. Билд-система управляет именно списком `-I`.

3. **Сгенерированные заголовки.** Классика: `config.h` генерируется из шаблона.

   ```cmake
   configure_file(config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)
   target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
   ```
   ```c
   /* config.h.in */
   #cmakedefine HAVE_ZLIB 1
   #define APP_VERSION "@PROJECT_VERSION@"
   ```

   Здесь `@VAR@` подставляет CMake, а `#cmakedefine` превращается в `#define` или `/* #undef */`. Дальше работает обычный препроцессор: код видит `HAVE_ZLIB` и `APP_VERSION` как литералы.

4. **Генерация кода до компиляции.** Если заголовок создаёт внешний инструмент, в Make это явная зависимость, в CMake — `add_custom_command(OUTPUT ...)` + `add_custom_target`, и обязательно указать, что объекты зависят от сгенерированного файла, иначе параллельная сборка возьмёт старую/отсутствующую версию.

5. **Единство флагов.** Все единицы трансляции одной цели должны компилироваться с одинаковыми `-D`, `-std`, `-fno-exceptions` и т.п. Разные значения макросов в разных `.cpp` — это разные определения одного и того же класса/функции, то есть ODR-нарушение и «необъяснимые» падения в рантайме, а не ошибка компиляции.

6. **Модули C++20.** Для `import` нужен сканирующий проход; CMake поддерживает это начиная с 3.28 (`CMAKE_CXX_SCAN_FOR_MODULES`), но требует свежего компилятора и генератора. В Make это пришлось бы реализовывать вручную — наглядный пример того, зачем нужен современный генератор.

---

## 5. Make против CMake

| Критерий | Make | CMake |
|---|---|---|
| Уровень | движок сборки | генератор систем сборки |
| Синтаксис | правила + shell | собственный декларативный язык |
| Зависимости от заголовков | вручную (`-MMD`) | автоматически |
| Внешние библиотеки | `-l`/`-L`, `pkg-config` | `find_package`, `FetchContent`, импортированные цели |
| Кроссплатформенность | низкая | высокая (VS, Xcode, Ninja, Make) |
| IDE-интеграция | слабая | генерация проектов VS/Xcode, `compile_commands.json` |
| Порог входа | низкий | средний |
| Гибкость для нестандартных шагов | высокая (любой shell) | высокая, но через `add_custom_command` |
| Типичное применение | небольшие проекты, Linux, встраиваемое | всё, что больше пары файлов; де-факто стандарт |

Практический вывод: **Make знать обязательно** (это то, во что CMake разворачивается, и это язык, на котором живут миллионы legacy-проектов), **CMake использовать по умолчанию** для новых проектов.

---

## 6. Рабочий процесс и типичные ошибки

**Ежедневный цикл с CMake:**

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/app
ctest --test-dir build --output-on-failure
cmake --build build --target clean
rm -rf build && cmake -S . -B build    # полный сброс кэша
```

**Типичные ошибки:**

- **Пробелы вместо TAB** в рецепте Makefile — `missing separator`.
- **Забытая зависимость от заголовка** — «изменил, а не пересобралось». Лечится `-MMD -MP`, а не `make clean`.
- **`make clean` как основной инструмент** — признак неполного графа зависимостей.
- **Сборка в исходниках** (`cmake .`) — мусор в репозитории и конфликты.
- **`file(GLOB ...)` для списка исходников в CMake** — новые файлы не подхватываются без повторной конфигурации; перечисляйте файлы явно.
- **Глобальные `CMAKE_CXX_FLAGS`** — протекают на все цели, включая сторонние; используйте `target_compile_options`.
- **Смешивание Debug/Release** в одном дереве — ODR и странные падения.
- **Параллельная сборка при неполном графе** — гонки на сгенерированных файлах.
- **Устаревший кэш CMake** после смены компилятора/путей — помогает `rm -rf build` или `cmake --fresh` (CMake ≥ 3.24).
- **`-I` вместо `-isystem`** для внешних библиотек — их предупреждения засоряют вывод и ломают `-Werror`.

---

## 7. Шпаргалка

**Make**

```bash
make                 # цель по умолчанию (первая в файле)
make target          # конкретная цель
make -j8             # параллельно
make -n              # показать команды, не выполняя
make -B              # пересобрать принудительно
make -k              # не останавливаться на первой ошибке
make CXX=clang++     # переопределить переменную
make -p              # дамп правил и переменных
```

**CMake**

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --build build --target app
cmake --install build --prefix /usr/local
ctest --test-dir build --output-on-failure
cmake --preset release
cmake --fresh -S . -B build
```

**Ключевые команды CMake**

```cmake
cmake_minimum_required(VERSION 3.20)
project(Name VERSION 1.0 LANGUAGES CXX)
add_executable(app main.cpp)
add_library(core STATIC core.cpp)
add_subdirectory(dir)
target_include_directories(t PUBLIC|PRIVATE|INTERFACE dir)
target_link_libraries(t PUBLIC|PRIVATE|INTERFACE dep)
target_compile_definitions(t PRIVATE FOO=1)
target_compile_options(t PRIVATE -Wall)
target_compile_features(t PUBLIC cxx_std_20)
find_package(Pkg CONFIG REQUIRED)
configure_file(in.h.in out.h @ONLY)
install(TARGETS app RUNTIME DESTINATION bin)
enable_testing(); add_test(NAME t COMMAND app)
```

Суть ответа в одном абзаце: билд-система описывает **граф целей и зависимостей**, включая невидимые для компилятора зависимости от заголовков и сгенерированных файлов; Make даёт минимальный движок и требует ручного описания всего, CMake добавляет переносимую модель целей с транзитивными требованиями (`PUBLIC`/`PRIVATE`/`INTERFACE`), автоматическое отслеживание `#include`, поиск зависимостей и генерацию проектов под любую платформу — и именно через флаги `-I`/`-D` и генерацию заголовков билд-система напрямую управляет тем, что увидит препроцессор.