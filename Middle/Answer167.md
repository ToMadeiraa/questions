# 168. Маленькая задача на написание кода, чтобы проверить умение проектировать интерфейсы и придерживаться принципов SOLID, DRY, KISS

> **Коротко.** Это мета-вопрос: конкретную задачу задаст интервьюер, а проверять будут процесс — как вы уточняете требования, находите оси изменчивости, проводите границы ответственности и умеете вовремя остановиться. Ниже — алгоритм, который работает для любой такой задачи, и полный разбор на типовом примере «расчёт и оплата заказа».

---

## 1. Что на самом деле проверяют

Интервьюеру не нужен «ещё один калькулятор». Он смотрит на пять вещей:

1. **Уточняете ли вы требования до кода.** «А скидки складываются или применяются последовательно?», «НДС считается до или после скидки?», «Отказ банка — это ошибка или штатный сценарий?»
2. **Умеете ли выделять домен и инварианты.** Где деньги, где правила, где инфраструктура, что нельзя сломать.
3. **Ставите ли интерфейс в точку изменчивости.** Не «абстракция ради абстракции», а там, где бизнес действительно просит расширять.
4. **Думаете ли о тестируемости и владении.** Можно ли проверить логику без сети и БД; кто кого владеет; что будет при ошибке.
5. **Не переусложняете ли.** Умение сказать «здесь хватит конкретного класса» ценится не меньше умения спроектировать интерфейс.

Формулировка «любая задача» означает, что оценивают **переносимый навык**, а не знание конкретного API.

---

## 2. Алгоритм на интервью: шесть шагов

1. **Уточнить требования и границы.** Записать вопросы, зафиксировать допущения.
2. **Определить критерии готовности.** Что должно работать, какие инварианты, что явно вне задачи.
3. **Написать самый простой работающий вариант.** KISS: сначала прямая реализация, без абстракций.
4. **Найти оси изменчивости.** Что чаще всего будут менять: скидки, налоги, доставку, платёжный провайдер, формат хранения.
5. **Выделить интерфейсы только на этих осях.** Проверить контракты (LSP), узость (ISP), направление зависимостей (DIP).
6. **Убрать дубли и показать тест.** DRY по знаниям, а не по тексту; тест с fake-реализацией вместо сети.

На интервью это выглядит так: 5 минут вопросов → 10 минут наивного кода → 15 минут рефакторинга с объяснением → 5–10 минут тестов и обсуждения trade-offs.

---

## 3. Демо-задача и допущения

### 3.1 Формулировка

> Реализовать расчёт стоимости корзины интернет-магазина и проведение оплаты. В корзине — товары с ценой и количеством. Нужны скидки (процент, промокод с минимальной суммой, акция «2+1»), НДС, доставка (бесплатно от суммы). Оплата — картой. Новые правила скидок и новые способы оплаты должны добавляться без правок существующего кода. Логику нужно тестировать без сети и базы данных.

### 3.2 Уточняющие вопросы

Их стоит задать до первой строки кода — это половина успеха:

- Валюта и правила округления? Округляем на каждом шаге или только итог?
- Скидки складываются, применяются последовательно или конкурируют? Есть ли приоритеты и ограничение «не больше 100 %»?
- НДС включён в цену или начисляется сверху? Считается от цены до скидки или после?
- Доставка зависит от суммы до скидки, после скидки или от веса/количества?
- Отказ банка — это ошибка или нормальный результат? Нужны ли повторные попытки, возвраты, идемпотентность?
- Нужно ли сохранять заказ? Есть ли конкурентные запросы?
- Какой стандарт C++ и можно ли использовать C++23?

### 3.3 Допущения для примера

- Валюта — рубли, деньги хранятся в копейках (`int64_t`), без `double`.
- Скидки считаются независимо от исходного подытога и складываются; суммарная скидка не превышает подытог.
- НДС начисляется сверху после скидки.
- Порог бесплатной доставки сравнивается с суммой после скидки, но до налога.
- Отказ и недоступность платёжного шлюза — штатные результаты, а не исключения.

Эти допущения важно **проговорить вслух**: они показывают, что вы понимаете границы своей модели.

---

## 4. Антипример: как выглядит плохое решение

```cpp
double checkout(std::vector<Item> items,
                const std::string& payment,
                const std::string& discount,
                const std::string& promo) {
    double total = 0.0;
    for (const auto& i : items) total += i.price * i.count;

    if (discount == "percent")      total *= 0.9;
    else if (discount == "promo" && promo == "SALE100") total -= 100;

    if (payment == "card") {
        // ... прямо здесь вызов конкретного банка, HTTP, логирование
    } else if (payment == "cash") {
        // ...
    }

    std::cout << "Total: " << total << "\n";
    return total;
}
```

| Принцип | Что нарушено |
|---|---|
| **SRP** | Одна функция считает, применяет скидки, выбирает платёж, логирует и печатает |
| **OCP** | Новая скидка или способ оплаты = правка `if/else` в этой же функции |
| **LSP** | Контрактов нет: «скидка» может увести сумму в минус, «оплата» может бросить что угодно |
| **ISP** | Интерфейсов нет вообще; клиент зависит от всей функции целиком |
| **DIP** | Код знает про конкретный банк и сеть; тестировать без сети нельзя |
| **DRY** | Округление, формула налога и проверки размазаны по веткам |
| **KISS** | Простота мнимая: чем больше правил, тем дороже каждая правка |

Ключевой момент: наивный вариант **не является KISS-решением**. KISS — это «минимальная сложность, достаточная для требований», а не «одна большая функция».

---

## 5. Декомпозиция: кто за что отвечает

| Понятие | Ответственность | Причина изменения |
|---|---|---|
| `Money` | Арифметика денег, округление | Правила округления, валюта |
| `Cart` | Состав заказа, инварианты позиций | Структура заказа |
| `Discount` | Одно правило скидки | Маркетинг |
| `TaxPolicy` | Налог | Законодательство |
| `ShippingPolicy` | Стоимость доставки | Логистика |
| `PaymentGateway` | Списание денег | Платёжная интеграция |
| `CheckoutService` | Порядок шагов сценария | Бизнес-процесс |
| `Receipt` | Результат расчёта | Отчётность |

Это и есть SRP в действии: **одна причина изменения на класс**, а не «один класс на всё».

---

## 6. Код

> Фрагменты ниже — один файл. Используется C++23 из-за `std::expected`; на C++20 его заменяют на `tl::expected`, `std::variant` или исключения — на дизайн это не влияет.

### 6.1 `Money`: деньги — целые копейки

```cpp
#include <algorithm>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shop {

class Money {
public:
    constexpr Money() noexcept = default;

    static constexpr Money cents(std::int64_t c) noexcept { return Money{c}; }

    // rub(1999, 90) == 1999.90
    static constexpr Money rub(std::int64_t whole, std::int64_t kop = 0) noexcept {
        const std::int64_t sign = whole < 0 ? -1 : 1;
        return Money{whole * 100 + sign * kop};
    }

    [[nodiscard]] constexpr std::int64_t in_cents() const noexcept { return cents_; }
    [[nodiscard]] constexpr bool is_zero() const noexcept { return cents_ == 0; }
    [[nodiscard]] constexpr bool is_negative() const noexcept { return cents_ < 0; }

    // Процент в базисных пунктах: 2000 bp = 20 %.
    // Округление — половина вверх, в одном месте на весь проект.
    [[nodiscard]] constexpr Money percent_bp(std::int32_t bp) const noexcept {
        const std::int64_t scaled = cents_ * bp;
        const std::int64_t rounded = scaled >= 0 ? scaled + 5'000 : scaled - 5'000;
        return Money{rounded / 10'000};
    }

    friend constexpr Money operator+(Money a, Money b) noexcept {
        return Money{a.cents_ + b.cents_};
    }
    friend constexpr Money operator-(Money a, Money b) noexcept {
        return Money{a.cents_ - b.cents_};
    }
    friend constexpr Money operator*(Money a, std::int64_t k) noexcept {
        return Money{a.cents_ * k};
    }
    friend constexpr bool operator==(Money a, Money b) noexcept {
        return a.cents_ == b.cents_;
    }
    friend constexpr auto operator<=>(Money a, Money b) noexcept {
        return a.cents_ <=> b.cents_;
    }

private:
    explicit constexpr Money(std::int64_t c) noexcept : cents_{c} {}
    std::int64_t cents_{0};
};
```

Почему так:

- `double` для денег — источник ошибок `0.1 + 0.2`; копейки снимают проблему.
- Конструктор приватный: нельзя случайно создать деньги из «сырого» числа.
- Округление — **единственное место** (`percent_bp`), а не в каждой скидке и налоге.
- `[[nodiscard]]`, `noexcept`, `constexpr` — часть контракта типа.

### 6.2 `Cart`: агрегат, который держит свои инварианты

```cpp
struct LineItem {
    std::string sku;
    Money unit_price;
    int quantity{1};
};

class Cart {
public:
    // Единственное место проверки инвариантов позиции.
    void add(LineItem item) {
        if (item.quantity <= 0) {
            throw std::invalid_argument("quantity must be positive");
        }
        if (item.unit_price.is_negative()) {
            throw std::invalid_argument("price must be non-negative");
        }
        items_.push_back(std::move(item));
    }

    [[nodiscard]] Money subtotal() const noexcept {
        Money sum{};
        for (const LineItem& item : items_) {
            sum = sum + item.unit_price * item.quantity;
        }
        return sum;
    }

    // Не светим контейнером: клиенту нужен только вид на элементы.
    [[nodiscard]] std::span<const LineItem> items() const noexcept { return items_; }
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }

private:
    std::vector<LineItem> items_;
};
```

`std::span<const LineItem>` вместо `const std::vector<LineItem>&` — мелочь, которая показывает культуру интерфейсов: клиент не начинает зависеть от `std::vector`.

### 6.3 Ось изменчивости №1: скидки

```cpp
class Discount {
public:
    virtual ~Discount() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // Контракт:
    //   0 <= amount_for(cart) <= cart.subtotal();
    //   не бросает исключений.
    [[nodiscard]] virtual Money amount_for(const Cart& cart) const = 0;
};

using DiscountRef = std::reference_wrapper<const Discount>;

class PercentDiscount final : public Discount {
public:
    PercentDiscount(std::string name, std::int32_t bp)
        : name_{std::move(name)}, bp_{bp} {
        if (bp < 0 || bp > 10'000) {
            throw std::invalid_argument("bp must be in [0, 10000]");
        }
    }

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }

    [[nodiscard]] Money amount_for(const Cart& cart) const override {
        return cart.subtotal().percent_bp(bp_);
    }

private:
    std::string name_;
    std::int32_t bp_;
};

class PromoDiscount final : public Discount {
public:
    PromoDiscount(std::string code, Money amount, Money min_subtotal)
        : code_{std::move(code)}, amount_{amount}, min_subtotal_{min_subtotal} {
        if (amount.is_negative()) {
            throw std::invalid_argument("discount must be non-negative");
        }
    }

    [[nodiscard]] std::string_view name() const noexcept override { return code_; }

    [[nodiscard]] Money amount_for(const Cart& cart) const override {
        const Money subtotal = cart.subtotal();
        if (subtotal < min_subtotal_) return Money{};
        return std::min(amount_, subtotal); // контракт: не больше подытога
    }

private:
    std::string code_;
    Money amount_;
    Money min_subtotal_;
};

// Новое правило — новый класс; CheckoutService не меняется.
class BuyTwoGetOneFree final : public Discount {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "2+1"; }

    [[nodiscard]] Money amount_for(const Cart& cart) const override {
        Money free_items{};
        for (const LineItem& item : cart.items()) {
            free_items = free_items + item.unit_price * (item.quantity / 3);
        }
        return free_items;
    }
};
```

Здесь важны **контракты**, а не просто виртуальные функции:

- `amount_for` возвращает сумму скидки, а не новую цену — так правила композируются.
- Результат неотрицателен и не больше подытога — это будущий LSP.
- Метод не бросает: отказ от скидки выражается нулём.
- `name()` нужен для чека и логов; `noexcept` здесь честный.

### 6.4 Оси изменчивости №2 и №3: налог и доставка

```cpp
class TaxPolicy {
public:
    virtual ~TaxPolicy() = default;

    // Контракт: tax_for(0) == 0; результат неотрицателен.
    [[nodiscard]] virtual Money tax_for(Money taxable) const = 0;
};

class Vat final : public TaxPolicy {
public:
    explicit Vat(std::int32_t bp) : bp_{bp} {
        if (bp < 0 || bp > 10'000) {
            throw std::invalid_argument("VAT bp out of range");
        }
    }

    [[nodiscard]] Money tax_for(Money taxable) const override {
        return taxable.percent_bp(bp_);
    }

private:
    std::int32_t bp_;
};

class ShippingPolicy {
public:
    virtual ~ShippingPolicy() = default;

    [[nodiscard]] virtual Money cost_for(const Cart& cart, Money payable) const = 0;
};

class FreeAbove final : public ShippingPolicy {
public:
    FreeAbove(Money threshold, Money fee) : threshold_{threshold}, fee_{fee} {
        if (threshold.is_negative() || fee.is_negative()) {
            throw std::invalid_argument("negative shipping params");
        }
    }

    [[nodiscard]] Money cost_for(const Cart&, Money payable) const override {
        return payable < threshold_ ? fee_ : Money{};
    }

private:
    Money threshold_;
    Money fee_;
};
```

`Vat` и `PercentDiscount` оба используют `percent_bp`, но это **не дублирование**: у них разные причины изменения (законодательство и маркетинг). Общее у них — только знание об округлении, и оно живёт в `Money`.

### 6.5 Ось изменчивости №4: оплата

```cpp
struct PaymentFailure {
    std::string reason;
    bool retryable{false};
};

using TransactionId = std::string;

class PaymentGateway {
public:
    virtual ~PaymentGateway() = default;

    // Контракт: отказ и недоступность — штатный результат, а не исключение.
    [[nodiscard]] virtual std::expected<TransactionId, PaymentFailure>
    charge(Money amount, std::string_view card_token) = 0;
};
```

Интерфейс узкий: только `charge`. Возвраты, отмены и подписки — это **другие** интерфейсы (`RefundGateway`, `SubscriptionGateway`), а не методы «на всякий случай». Иначе получится «божественный» `IPaymentService` на 15 методов, от которого зависят все клиенты.

### 6.6 Оркестрация: `CheckoutService`

```cpp
struct AppliedDiscount {
    std::string name;
    Money amount;
};

struct Receipt {
    Money subtotal{};
    Money discount{};
    Money tax{};
    Money shipping{};
    Money total{};
    std::vector<AppliedDiscount> discounts;
    TransactionId transaction_id;
};

enum class CheckoutErrorCode { EmptyCart, PaymentDeclined, PaymentUnavailable };

struct CheckoutError {
    CheckoutErrorCode code{};
    std::string message;
};

class CheckoutService {
public:
    CheckoutService(const TaxPolicy& tax,
                    const ShippingPolicy& shipping,
                    PaymentGateway& payments) noexcept
        : tax_{tax}, shipping_{shipping}, payments_{payments} {}

    [[nodiscard]] std::expected<Receipt, CheckoutError>
    checkout(const Cart& cart,
             std::span<const DiscountRef> discounts,
             std::string_view card_token) {
        if (cart.empty()) {
            return std::unexpected(CheckoutError{
                .code = CheckoutErrorCode::EmptyCart,
                .message = "cart is empty",
            });
        }

        const Money subtotal = cart.subtotal();

        Money discount_total{};
        std::vector<AppliedDiscount> applied;
        applied.reserve(discounts.size());

        for (const Discount& discount : discounts) {
            Money part = discount.amount_for(cart);
            if (part.is_negative()) {
                throw std::logic_error("Discount::amount_for broke its contract");
            }
            // Защита инварианта: суммарная скидка не больше подытога.
            part = std::min(part, subtotal - discount_total);
            if (part.is_zero()) continue;

            discount_total = discount_total + part;
            applied.push_back(AppliedDiscount{std::string{discount.name()}, part});
        }

        const Money taxable = subtotal - discount_total;
        const Money tax = tax_.tax_for(taxable);
        const Money shipping = shipping_.cost_for(cart, taxable);
        const Money total = taxable + tax + shipping;

        auto payment = payments_.charge(total, card_token);
        if (!payment) {
            return std::unexpected(CheckoutError{
                .code = payment.error().retryable
                            ? CheckoutErrorCode::PaymentUnavailable
                            : CheckoutErrorCode::PaymentDeclined,
                .message = payment.error().reason,
            });
        }

        return Receipt{
            .subtotal = subtotal,
            .discount = discount_total,
            .tax = tax,
            .shipping = shipping,
            .total = total,
            .discounts = std::move(applied),
            .transaction_id = std::move(*payment),
        };
    }

private:
    const TaxPolicy& tax_;
    const ShippingPolicy& shipping_;
    PaymentGateway& payments_;
};

} // namespace shop
```

Что здесь принципиально:

- **Сервис — единственное место, где известен порядок шагов.** Меняется процесс (например, скидка после налога) — меняется только он.
- **Зависимости — ссылки на абстракции**, владение ими не передаётся. Время жизни — не меньше времени жизни сервиса; это записано в контракте класса.
- **Ошибки разделены на два вида:** нарушение контракта (исключение, баг) и штатный исход (значение `std::expected`). Отказ банка — не исключение.
- **Инвариант «скидка ≤ подытог»** проверяется в одном месте, а не в каждом правиле.

### 6.7 Тесты: fake вместо сети

```cpp
#include <cassert>

class FakeGateway final : public shop::PaymentGateway {
public:
    std::expected<shop::TransactionId, shop::PaymentFailure>
    charge(shop::Money amount, std::string_view token) override {
        last_amount = amount;
        ++calls;
        if (decline) {
            return std::unexpected(shop::PaymentFailure{"card declined", false});
        }
        return shop::TransactionId{"tx-" + std::to_string(calls)};
    }

    shop::Money last_amount{};
    int calls{0};
    bool decline{false};
};

int main() {
    using namespace shop;

    Cart cart;
    cart.add({"book", Money::rub(1000), 2}); // 2000
    cart.add({"pen", Money::rub(500), 1});   //  500, подытог 2500

    PercentDiscount ten{"10%", 1000};
    PromoDiscount promo{"SALE100", Money::rub(100), Money::rub(2000)};
    Vat vat{2000};                                        // 20 %
    FreeAbove shipping{Money::rub(3000), Money::rub(300)};

    FakeGateway gateway;
    CheckoutService service{vat, shipping, gateway};

    std::array<DiscountRef, 2> discounts{std::cref(ten), std::cref(promo)};
    const auto receipt = service.checkout(cart, discounts, "tok_visa");

    assert(receipt.has_value());
    assert(receipt->subtotal == Money::rub(2500));
    assert(receipt->discount == Money::rub(350));
    assert(receipt->tax == Money::rub(430));
    assert(receipt->shipping == Money::rub(300));
    assert(receipt->total == Money::rub(2880));
    assert(gateway.last_amount == Money::rub(2880));

    gateway.decline = true;
    const auto declined = service.checkout(cart, discounts, "tok_bad");
    assert(!declined.has_value());
    assert(declined.error().code == CheckoutErrorCode::PaymentDeclined);
}
```

Тест занимает несколько строк и **не требует ни сети, ни БД, ни мок-фреймворка** — это прямое следствие DIP. В реальном проекте вместо `assert` будет Catch2/GoogleTest, а `FakeGateway` переедет в общие тестовые утилиты.

---

## 7. Разбор по SOLID

### SRP — единственная причина изменения

| Класс | Причина изменения |
|---|---|
| `Money` | Правила округления, валюта |
| `Cart` | Структура заказа |
| `PercentDiscount`, `PromoDiscount`, `BuyTwoGetOneFree` | Маркетинговые правила |
| `Vat` | Налоговое законодательство |
| `FreeAbove` | Логистика |
| `PaymentGateway` | Платёжная интеграция |
| `CheckoutService` | Бизнес-процесс оформления |
| `Receipt` | Формат результата |

Ни один класс не «знает всё». При этом SRP не означает «один метод на класс»: `CheckoutService` намеренно оркестрирует несколько шагов — это его единственная ответственность.

### OCP — открыт для расширения, закрыт для изменения

- Новая скидка — новый класс-наследник `Discount`; `CheckoutService` не трогаем.
- Новый налог — новый `TaxPolicy`.
- Новая доставка — новый `ShippingPolicy`.
- Новый банк — новый `PaymentGateway`.

Проверка: добавление `BuyTwoGetOneFree` не потребовало ни одной правки в сервисе. Это и есть практический критерий OCP.

Важная оговорка: OCP **не абсолютен**. Если меняется сам процесс (скидки стали применяться последовательно), сервис придётся менять — это не нарушение, а изменение требования. OCP защищает от изменений **по осям вариативности**, а не от любых изменений.

### LSP — подстановка без сюрпризов

Контракт `Discount`:

- результат в диапазоне `[0, subtotal]`;
- метод не бросает;
- правила независимы (считаются от исходного подытога).

Любая реализация, которая вернёт отрицательную скидку, бросит исключение или превысит подытог, ломает LSP: клиент вынужден знать про особенность подкласса. Поэтому контракт записан в базовом классе, а сервис дополнительно страхует инвариант.

Контракт `PaymentGateway`: отказ — это `PaymentFailure` в `expected`, а не исключение. Любой шлюз (реальный, fake, «всегда одобряет») подставляется без изменения клиента.

Классический антипример LSP — «квадрат как прямоугольник»: если `setWidth` у квадрата меняет и высоту, клиент прямоугольника ломается. Здесь аналогичный риск — «скидка, которая иногда бросает».

### ISP — узкие интерфейсы

Четыре независимых интерфейса вместо одного `IOrderService`:

- `Discount` — только `name` и `amount_for`;
- `TaxPolicy` — только `tax_for`;
- `ShippingPolicy` — только `cost_for`;
- `PaymentGateway` — только `charge`.

`CheckoutService` зависит ровно от того, что использует. Если завтра понадобятся возвраты, появится отдельный `RefundGateway`, и клиенты, которым возвраты не нужны, о нём не узнают.

### DIP — зависимости направлены на абстракции

- Высокоуровневый сценарий (`CheckoutService`) не зависит от низкоуровневых деталей (HTTP, БД, конкретный банк).
- Конкретные реализации собираются в **composition root** — в `main` или в фабрике приложения.
- Домен не включает заголовки сети и БД — это ещё и ускорение сборки.
- Тест подставляет `FakeGateway` без изменения продакшн-кода.

---

## 8. DRY — про знания, а не про текст

Что действительно продублировано и вынесено:

- **Округление** — только в `Money::percent_bp`. Если завтра бизнес скажет «округление вниз», правится одна функция.
- **Проверка позиции** — только в `Cart::add`.
- **Подытог** — только в `Cart::subtotal`.
- **Формула налога** — только в `Vat::tax_for`.
- **Инвариант «скидка ≤ подытог»** — в одном месте, в сервисе.

Что **не** стоит объединять:

- `PercentDiscount` и `Vat` похожи syntactic, но это разные знания с разными причинами изменения. Их слияние нарушило бы SRP и усложнило бы эволюцию.
- `PromoDiscount` и `PercentDiscount` не сводятся к «одному if»: у них разные данные и разные условия применимости.

Правило: **DRY применяется к бизнес-правилам и формулам, а не к случайно похожему коду.** Дублирование двух похожих классов дешевле, чем неверная абстракция, которую потом придётся разбирать.

---

## 9. KISS и YAGNI — где остановиться

Что сознательно **не** сделано:

- нет DI-контейнера, фабрик, билдеров и синглтонов;
- нет шаблонных policy-классов и CRTP;
- нет сериализации, БД, репозитория и миграций;
- нет `shared_ptr` — зависимости не разделяются по владению;
- нет интерфейсов у `Money`, `LineItem`, `Receipt` — это значения, у них не будет двух реализаций;
- нет обработки возвратов, мультивалютности, идемпотентности и асинхронности — это следующие требования, а не текущие.

Четыре интерфейса появились не «потому что SOLID», а потому что в условии явно названы четыре оси изменчивости: скидки, налог, доставка, оплата. Если бы условие было «посчитать итог по фиксированной ставке», правильным KISS-решением была бы одна функция без единого виртуального метода.

**Преждевременная абстракция дороже дублирования**: она добавляет косвенность, усложняет отладку и часто оказывается не в той точке изменения.

---

## 10. Эволюция без правок существующего кода

Добавляем скидку «первый заказ — 15 %»:

```cpp
class FirstOrderDiscount final : public Discount {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "first-order"; }
    [[nodiscard]] Money amount_for(const Cart& cart) const override {
        return cart.subtotal().percent_bp(1500);
    }
};
```

`CheckoutService`, `Cart`, `Money`, `Vat`, `FreeAbove`, `PaymentGateway` — без изменений. Меняется только `main`, где собирается список скидок.

Добавляем оплату СБП:

```cpp
class SbpGateway final : public PaymentGateway {
public:
    std::expected<TransactionId, PaymentFailure>
    charge(Money amount, std::string_view token) override {
        // ... вызов конкретного API
    }
};
```

Сервис снова не меняется. Это и есть проверка OCP на практике: **точка расширения должна быть в условии задачи**, а не в голове автора.

---

## 11. Trade-offs, которые стоит проговорить

### `std::expected` против исключений

- Штатные бизнес-исходы (отказ банка, пустая корзина) — значения.
- Нарушения контракта (отрицательная скидка, битые входные данные) — исключения.
- На C++20: `tl::expected`, `std::variant` или исключения. Главное — **единая политика** на весь интерфейс.

### Виртуальные функции против шаблонов

- Набор реализаций открыт и выбирается в runtime (конфиг, плагины) — виртуальные функции.
- Набор закрыт и известен на этапе компиляции, важна производительность — шаблоны/concepts.
- Одиночная лямбда — `std::function`, но именованный интерфейс лучше документирует контракт.

### `Discount` принимает `Cart` или `Money`

- `Cart` — общее решение: правило «2+1» смотрит на состав.
- `Money` — уже, но не позволяет правилам смотреть на товары.
- Выбор зависит от требований; если все правила работают только с суммой, интерфейс стоит сузить (ISP).

### Скидки независимые или последовательные

- Сейчас каждая считается от исходного подытога — просто и предсказуемо.
- Если бизнес хочет «10 %, затем промокод на остаток», интерфейс должен принимать текущую сумму. Это изменение требования, а не «недоработка OCP».

### `int64` копейки против `double`

- `double` — только для отображения и аналитики, не для расчётов.
- Для мультивалютности — `Money` с кодом валюты и запретом арифметики между валютами без конвертации.

### Ссылки против `unique_ptr`

- Ссылки — невладение; сервис не отвечает за время жизни шлюза.
- `unique_ptr` — если сервис владеет зависимостью.
- `shared_ptr` — только если владение действительно разделяется; по умолчанию это лишняя косвенность.

### Зажим скидки против ошибки

- Сейчас сервис зажимает сумму (`std::min`), чтобы не уйти в минус.
- Альтернатива — вернуть ошибку «скидки превышают заказ». Зажим удобнее для пользователя, ошибка — честнее для отладки. Решение должно быть проговорено.

---

## 12. Типичные ошибки на таком вопросе

1. **Начать с десяти интерфейсов**, не выяснив требования. Это нарушение KISS и YAGNI одновременно.
2. **`double` для денег** — почти гарантированный минус на ревью.
3. **«Божественный» интерфейс** `IOrderService` с методами `calculate`, `pay`, `refund`, `save`, `sendEmail`.
4. **Интерфейс без контракта**: не описано, что можно возвращать, какие ошибки, кто владеет объектом.
5. **Возврат `const std::vector&`** из доменного класса — клиент начинает зависеть от контейнера.
6. **`shared_ptr` по умолчанию** — размывает владение и усложняет тесты.
7. **LSP-нарушение**: скидка бросает исключение или возвращает отрицательную сумму.
8. **Тесты через реальную сеть** — медленно, нестабильно, не проверяют логику.
9. **SOLID ради SOLID**: абстракция там, где нет второй реализации и не планируется.
10. **Отсутствие обработки ошибок**: «всё всегда работает», отказ банка роняет программу.

---

## 13. Вероятные follow-up вопросы

**«А если скидок 50 и они конфликтуют?»**
Появляется `DiscountEngine`: выбирает применимые правила, учитывает приоритеты и лимиты. `CheckoutService` по-прежнему оркестрирует, но делегирует выбор. Это следующая итерация, а не то, что нужно писать сразу.

**«А возвраты?»**
Отдельный `RefundGateway` (ISP), состояние заказа (state machine), идемпотентность. В текущем дизайне это добавление, а не переделка.

**«Потокобезопасность?»**
`CheckoutService` не хранит изменяемого состояния — сам по себе потокобезопасен, если потокобезопасны зависимости. Контракт шлюза должен это явно оговаривать; добавлять `mutex` «на всякий случай» не нужно.

**«Идемпотентность платежа?»**
Ключ идемпотентности на попытку оформления, передаётся в `charge`; шлюз обязан вернуть тот же результат при повторе. Это расширение интерфейса, продиктованное требованием.

**«Мультивалютность?»**
`Money` получает код валюты; арифметика между разными валютами запрещена на уровне типа; конвертация — отдельный сервис с явным курсом.

**«Почему не шаблоны?»**
Набор правил открыт и конфигурируется в runtime, поэтому виртуальные функции уместнее. Шаблоны выигрывают, когда набор закрыт и важна производительность.

**«Где хранить заказ?»**
`OrderRepository` — отдельная абстракция, не часть расчёта. Транзакционная граница и outbox — вопросы инфраструктуры, а не домена.

**«Как логировать?»**
Декоратор над `PaymentGateway` или над сервисом, а не `std::cout` внутри бизнес-логики.

---

## 14. Чек-лист самопроверки

- [ ] Требования и допущения записаны до кода.
- [ ] Деньги — целые минорные единицы, округление в одном месте.
- [ ] Инварианты проверяются там, где создаётся состояние.
- [ ] Интерфейсы узкие, с записанными контрактами (диапазоны, ошибки, владение).
- [ ] Зависимости внедряются, домен не знает про сеть и БД.
- [ ] Нет `new`/`delete`, правило нуля, `virtual ~` у базовых классов.
- [ ] Тест с fake-реализацией проходит без сети.
- [ ] Добавление нового правила не трогает сервис.
- [ ] Нет фабрик, контейнеров и шаблонов «на всякий случай».
- [ ] Названия отражают бизнес-смысл, а не паттерн.

---

## Итог: что сказать в конце

> «Я выделил четыре оси изменчивости — скидки, налог, доставку и оплату — и поставил интерфейсы только на них. Деньги хранятся в копейках, округление централизовано, инвариант “скидка не больше заказа” проверяется в одном месте. Зависимости инвертированы: сервис не знает, какой банк и какая база под ним, поэтому тест подставляет fake. Сознательно не добавлял возвраты, мультивалютность и идемпотентность — это следующие требования; если они появятся, точки расширения уже готовы».

Именно такой ответ — с кодом, контрактами, тестом и объяснением границ — закрывает вопрос 168: видно и умение проектировать интерфейсы, и SOLID, и DRY, и KISS.