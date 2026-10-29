# 167. Напишите класс, который достает из базы список товаров по фильтру и показывает на консоли. Напишите тесты для него.

## Архитектура решения

Чтобы класс был тестируемым, разделим ответственность:

- **`IProductRepository`** — абстрактный интерфейс доступа к данным (позволяет подменять БД в тестах).
- **`IOutput`** — абстракция вывода (чтобы проверять, что именно печатается).
- **`ProductFilter`** — структура фильтра.
- **`ProductPrinter`** — сам класс, который достаёт товары по фильтру и печатает их.

Тестируем через «моки» (ручные фейки), без внешних фреймворков — чистый C++17.

---

## Реализация

```cpp
// product.h
#pragma once
#include <string>

struct Product {
    int         id;
    std::string name;
    std::string category;
    double      price;
};

struct ProductFilter {
    std::string category;   // пусто = без фильтра
    double      minPrice = 0.0;
    double      maxPrice = 1e18;
};
```

```cpp
// i_product_repository.h
#pragma once
#include <vector>
#include "product.h"

class IProductRepository {
public:
    virtual ~IProductRepository() = default;
    virtual std::vector<Product> findByFilter(const ProductFilter& f) = 0;
};
```

```cpp
// i_output.h
#pragma once
#include <string>

class IOutput {
public:
    virtual ~IOutput() = default;
    virtual void println(const std::string& s) = 0;
};
```

```cpp
// product_printer.h
#pragma once
#include <memory>
#include <sstream>
#include <iomanip>
#include "i_product_repository.h"
#include "i_output.h"

class ProductPrinter {
public:
    ProductPrinter(IProductRepository& repo, IOutput& out)
        : repo_(repo), out_(out) {}

    // Достаёт товары по фильтру и печатает их в консоль/выход
    void printByFilter(const ProductFilter& filter) {
        auto products = repo_.findByFilter(filter);

        if (products.empty()) {
            out_.println("No products found.");
            return;
        }

        out_.println("ID | Name | Category | Price");
        for (const auto& p : products) {
            std::ostringstream oss;
            oss << p.id << " | " << p.name << " | "
                << p.category << " | "
                << std::fixed << std::setprecision(2) << p.price;
            out_.println(oss.str());
        }
    }

private:
    IProductRepository& repo_;
    IOutput&            out_;
};
```

```cpp
// console_output.h
#pragma once
#include <iostream>
#include "i_output.h"

class ConsoleOutput : public IOutput {
public:
    void println(const std::string& s) override { std::cout << s << '\n'; }
};
```

---

## Тесты

Используем лёгкие ручные фейки: `FakeRepository` возвращает заранее заданные данные и запоминает переданный фильтр, `FakeOutput` копит строки для проверок. Проверки — через `assert` (можно заменить на GoogleTest/Catch2 — структура кода та же).

```cpp
// tests.cpp
#include <cassert>
#include <iostream>
#include <vector>
#include "product_printer.h"

class FakeRepository : public IProductRepository {
public:
    std::vector<Product> products;
    ProductFilter lastFilter{};
    int callCount = 0;

    std::vector<Product> findByFilter(const ProductFilter& f) override {
        lastFilter = f;
        ++callCount;
        // Простая фильтрация как в реальной БД
        std::vector<Product> result;
        for (const auto& p : products) {
            if (!f.category.empty() && p.category != f.category) continue;
            if (p.price < f.minPrice || p.price > f.maxPrice) continue;
            result.push_back(p);
        }
        return result;
    }
};

class FakeOutput : public IOutput {
public:
    std::vector<std::string> lines;
    void println(const std::string& s) override { lines.push_back(s); }
};

void test_prints_filtered_products() {
    FakeRepository repo;
    repo.products = {
        {1, "Apple",  "fruit", 10.0},
        {2, "Banana", "fruit", 5.0},
        {3, "Hammer", "tools", 25.0},
    };
    FakeOutput out;
    ProductPrinter printer(repo, out);

    ProductFilter f;
    f.category = "fruit";
    printer.printByFilter(f);

    // Проверяем, что в репозиторий ушёл именно наш фильтр
    assert(repo.callCount == 1);
    assert(repo.lastFilter.category == "fruit");

    // Заголовок + 2 строки
    assert(out.lines.size() == 3);
    assert(out.lines[0] == "ID | Name | Category | Price");
    assert(out.lines[1].find("Apple")  != std::string::npos);
    assert(out.lines[2].find("Banana") != std::string::npos);
}

void test_price_range_filter() {
    FakeRepository repo;
    repo.products = {
        {1, "Cheap",  "x", 3.0},
        {2, "Middle", "x", 15.0},
        {3, "Expensive", "x", 100.0},
    };
    FakeOutput out;
    ProductPrinter printer(repo, out);

    ProductFilter f;
    f.minPrice = 5.0;
    f.maxPrice = 50.0;
    printer.printByFilter(f);

    assert(out.lines.size() == 2);  // заголовок + Middle
    assert(out.lines[1].find("Middle") != std::string::npos);
}

void test_empty_result_prints_message() {
    FakeRepository repo;   // пустая БД
    FakeOutput out;
    ProductPrinter printer(repo, out);

    printer.printByFilter(ProductFilter{});

    assert(out.lines.size() == 1);
    assert(out.lines[0] == "No products found.");
}

void test_price_formatting() {
    FakeRepository repo;
    repo.products = { {7, "Widget", "misc", 12.5} };
    FakeOutput out;
    ProductPrinter printer(repo, out);

    printer.printByFilter(ProductFilter{});

    assert(out.lines.size() == 2);
    // Цена с двумя знаками после запятой
    assert(out.lines[1].find("12.50") != std::string::npos);
}

int main() {
    test_prints_filtered_products();
    test_price_range_filter();
    test_empty_result_prints_message();
    test_price_formatting();
    std::cout << "All tests passed\n";
    return 0;
}
```

Сборка и запуск:

```bash
g++ -std=c++17 -Wall -Wextra tests.cpp -o tests && ./tests
```

---

## Почему так

1. **Dependency injection через интерфейсы.** `ProductPrinter` не знает, откуда берутся данные (SQLite/PostgreSQL) и куда печатать (`std::cout`/файл/логгер) — это позволяет подменить их в тестах.
2. **Фейки вместо моков-фреймворков.** `FakeRepository` одновременно и проверяет переданный фильтр (`lastFilter`), и возвращает контролируемые данные — минимально достаточно для юнит-тестов.
3. **Проверяется поведение, а не реализация:** фильтр дошёл до репозитория, нужные строки напечатаны, пустой результат обработан, формат цены корректен.
4. **Расширяемость.** Реальная реализация `SqlProductRepository : IProductRepository` подключается в `main()` без изменения `ProductPrinter` и тестов.

## Что стоит добавить в реальном проекте

- Тесты на исключения из БД (например, `RepositoryThrows` → класс должен пробросить/залогировать).
- Тесты с GoogleTest/Catch2 для параметризации.
- Интеграционный тест с in-memory SQLite (`:memory:`) против реального `SqlProductRepository`.
- Логирование ошибок и валидацию фильтра (например, `minPrice > maxPrice`).