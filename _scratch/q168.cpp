// checkout.cpp — один файл для интервью.
// Сборка: g++ -std=c++23 -Wall -Wextra -Wpedantic checkout.cpp -o checkout
#include <algorithm>
#include <array>
#include <compare>
#include <cstdint>
#include <expected>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shop {

// ---------------------------------------------------------------------------
// Money: деньги — целые копейки, а не double.
// ---------------------------------------------------------------------------
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

    // Процент в базисных пунктах (2000 bp = 20 %), округление половины вверх.
    [[nodiscard]] constexpr Money percent_bp(std::int32_t bp) const noexcept {
        const std::int64_t scaled = cents_ * bp;
        const std::int64_t rounded = scaled >= 0 ? scaled + 5'000 : scaled - 5'000;
        return Money{rounded / 10'000};
    }

    friend constexpr Money operator+(Money a, Money b) noexcept { return Money{a.cents_ + b.cents_}; }
    friend constexpr Money operator-(Money a, Money b) noexcept { return Money{a.cents_ - b.cents_}; }
    friend constexpr Money operator*(Money a, std::int64_t k) noexcept { return Money{a.cents_ * k}; }
    friend constexpr bool operator==(Money a, Money b) noexcept { return a.cents_ == b.cents_; }
    friend constexpr auto operator<=>(Money a, Money b) noexcept { return a.cents_ <=> b.cents_; }

private:
    explicit constexpr Money(std::int64_t c) noexcept : cents_{c} {}
    std::int64_t cents_{0};
};

[[nodiscard]] inline std::string to_string(Money m) {
    const std::int64_t c = m.in_cents();
    const std::int64_t abs = c < 0 ? -c : c;
    std::string s = std::to_string(abs / 100) + ".";
    const std::int64_t frac = abs % 100;
    if (frac < 10) s += "0";
    s += std::to_string(frac);
    return c < 0 ? "-" + s : s;
}

// ---------------------------------------------------------------------------
// Корзина: агрегат, который сам держит свои инварианты.
// ---------------------------------------------------------------------------
struct LineItem {
    std::string sku;
    Money unit_price;
    int quantity{1};
};

class Cart {
public:
    // Единственное место, где проверяются инварианты позиции (DRY).
    void add(LineItem item) {
        if (item.quantity <= 0) throw std::invalid_argument("quantity must be positive");
        if (item.unit_price.is_negative()) throw std::invalid_argument("price must be non-negative");
        items_.push_back(std::move(item));
    }

    [[nodiscard]] Money subtotal() const noexcept {
        Money sum{};
        for (const LineItem& item : items_) sum = sum + item.unit_price * item.quantity;
        return sum;
    }

    [[nodiscard]] int total_quantity() const noexcept {
        int n = 0;
        for (const LineItem& item : items_) n += item.quantity;
        return n;
    }

    [[nodiscard]] const std::vector<LineItem>& items() const noexcept { return items_; }
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }

private:
    std::vector<LineItem> items_;
};

// ---------------------------------------------------------------------------
// Ось изменчивости №1: правила скидок.
// ---------------------------------------------------------------------------
class Discount {
public:
    virtual ~Discount() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // Контракт: 0 <= результат <= cart.subtotal(); исключений не бросает.
    [[nodiscard]] virtual Money amount_for(const Cart& cart) const = 0;
};

using DiscountRef = std::reference_wrapper<const Discount>;

class PercentDiscount final : public Discount {
public:
    PercentDiscount(std::string name, std::int32_t bp)
        : name_{std::move(name)}, bp_{bp} {
        if (bp < 0 || bp > 10'000) throw std::invalid_argument("bp must be in [0, 10000]");
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
        if (amount.is_negative()) throw std::invalid_argument("discount must be non-negative");
    }

    [[nodiscard]] std::string_view name() const noexcept override { return code_; }

    [[nodiscard]] Money amount_for(const Cart& cart) const override {
        const Money subtotal = cart.subtotal();
        if (subtotal < min_subtotal_) return Money{};
        return std::min(amount_, subtotal);
    }

private:
    std::string code_;
    Money amount_;
    Money min_subtotal_;
};

// Демонстрация OCP: новое правило — новый класс, CheckoutService не меняется.
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

// ---------------------------------------------------------------------------
// Ось изменчивости №2: налоги.
// ---------------------------------------------------------------------------
class TaxPolicy {
public:
    virtual ~TaxPolicy() = default;

    // Контракт: tax_for(0) == 0, результат неотрицателен.
    [[nodiscard]] virtual Money tax_for(Money taxable) const = 0;
};

class Vat final : public TaxPolicy {
public:
    explicit Vat(std::int32_t bp) : bp_{bp} {
        if (bp < 0 || bp > 10'000) throw std::invalid_argument("VAT bp out of range");
    }

    [[nodiscard]] Money tax_for(Money taxable) const override { return taxable.percent_bp(bp_); }

private:
    std::int32_t bp_;
};

// ---------------------------------------------------------------------------
// Ось изменчивости №3: доставка.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Ось изменчивости №4: платёжный провайдер.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Результат и ошибки сценария.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Оркестрация: единственное место, где известен порядок шагов.
// ---------------------------------------------------------------------------
class CheckoutService {
public:
    CheckoutService(const TaxPolicy& tax, const ShippingPolicy& shipping, PaymentGateway& payments) noexcept
        : tax_{tax}, shipping_{shipping}, payments_{payments} {}

    [[nodiscard]] std::expected<Receipt, CheckoutError>
    checkout(const Cart& cart, std::span<const DiscountRef> discounts, std::string_view card_token) {
        if (cart.empty()) {
            return std::unexpected(CheckoutError{CheckoutErrorCode::EmptyCart, "cart is empty"});
        }

        const Money subtotal = cart.subtotal();

        Money discount_total{};
        std::vector<AppliedDiscount> applied;
        applied.reserve(discounts.size());

        for (const Discount& discount : discounts) {
            Money part = discount.amount_for(cart);
            if (part.is_negative()) {
                throw std::logic_error("Discount::amount_for violated its contract (negative)");
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
                payment.error().retryable ? CheckoutErrorCode::PaymentUnavailable
                                          : CheckoutErrorCode::PaymentDeclined,
                payment.error().reason});
        }

        return Receipt{subtotal,   discount_total, tax,          shipping,
                       total,      std::move(applied), std::move(*payment)};
    }

private:
    const TaxPolicy& tax_;
    const ShippingPolicy& shipping_;
    PaymentGateway& payments_;
};

} // namespace shop

// ---------------------------------------------------------------------------
// Тесты: fake-шлюз вместо сети, детерминированные деньги.
// ---------------------------------------------------------------------------
namespace {

class FakeGateway final : public shop::PaymentGateway {
public:
    std::expected<shop::TransactionId, shop::PaymentFailure>
    charge(shop::Money amount, std::string_view card_token) override {
        last_amount = amount;
        last_token = std::string{card_token};
        ++calls;
        if (decline) return std::unexpected(shop::PaymentFailure{"card declined", false});
        return shop::TransactionId{"tx-" + std::to_string(calls)};
    }

    shop::Money last_amount{};
    std::string last_token;
    int calls{0};
    bool decline{false};
};

} // namespace

int main() {
    using namespace shop;

    int failures = 0;
    const auto check = [&failures](bool ok, std::string_view what) {
        if (!ok) {
            std::cerr << "FAIL: " << what << '\n';
            ++failures;
        }
    };
    const auto eq = [&check](Money actual, Money expected, std::string_view what) {
        check(actual == expected,
              std::string{what} + ": " + to_string(actual) + " != " + to_string(expected));
    };

    // --- сценарий 1: две скидки, НДС, платная доставка ---
    Cart cart;
    cart.add({"book", Money::rub(1000), 2}); // 2000
    cart.add({"pen", Money::rub(500), 1});   //  500, subtotal 2500

    PercentDiscount ten{"10%", 1000};
    PromoDiscount promo{"SALE100", Money::rub(100), Money::rub(2000)};
    Vat vat{2000};                                        // 20 %
    FreeAbove shipping{Money::rub(3000), Money::rub(300)}; // 2500 < 3000 -> 300

    FakeGateway gateway;
    CheckoutService service{vat, shipping, gateway};

    std::array<DiscountRef, 2> discounts{std::cref(ten), std::cref(promo)};
    auto receipt = service.checkout(cart, discounts, "tok_visa");

    check(receipt.has_value(), "checkout succeeded");
    if (receipt) {
        eq(receipt->subtotal, Money::rub(2500), "subtotal");
        eq(receipt->discount, Money::rub(350), "discount");
        eq(receipt->tax, Money::rub(430), "tax");
        eq(receipt->shipping, Money::rub(300), "shipping");
        eq(receipt->total, Money::rub(2880), "total");
        check(receipt->discounts.size() == 2, "two discounts applied");
        eq(gateway.last_amount, Money::rub(2880), "charged exactly total");
    }

    // --- сценарий 2: отказ банка ---
    gateway.decline = true;
    auto declined = service.checkout(cart, discounts, "tok_bad");
    check(!declined.has_value(), "declined has no receipt");
    check(declined.error().code == CheckoutErrorCode::PaymentDeclined, "decline code");
    gateway.decline = false;

    // --- сценарий 3: пустая корзина ---
    Cart empty;
    auto empty_result = service.checkout(empty, {}, "tok");
    check(!empty_result.has_value() &&
              empty_result.error().code == CheckoutErrorCode::EmptyCart,
          "empty cart rejected");

    // --- сценарий 4: скидка не может превысить подытог ---
    Cart small;
    small.add({"sticker", Money::rub(100), 1});
    PromoDiscount huge{"HUGE", Money::rub(500), Money::rub(0)};
    std::array<DiscountRef, 1> one{std::cref(huge)};
    auto capped = service.checkout(small, one, "tok");
    check(capped.has_value(), "capped checkout ok");
    if (capped) {
        eq(capped->discount, Money::rub(100), "discount capped by subtotal");
        eq(capped->tax, Money::rub(0), "no tax on zero taxable");
    }

    // --- сценарий 5: новое правило 2+1 без правок сервиса (OCP) ---
    Cart mugs;
    mugs.add({"mug", Money::rub(500), 3}); // 1500
    BuyTwoGetOneFree promo21;
    std::array<DiscountRef, 1> d21{std::cref(promo21)};
    FreeAbove no_shipping{Money::rub(0), Money::rub(0)};
    CheckoutService service2{vat, no_shipping, gateway};
    auto r21 = service2.checkout(mugs, d21, "tok");
    check(r21.has_value(), "2+1 checkout ok");
    if (r21) {
        eq(r21->subtotal, Money::rub(1500), "2+1 subtotal");
        eq(r21->discount, Money::rub(500), "2+1 discount");
        eq(r21->total, Money::rub(1200), "2+1 total");
    }

    // --- сценарий 6: округление денег ---
    eq(Money::rub(1999, 99).percent_bp(1000), Money::rub(200, 0), "10% of 1999.99");
    eq(Money::cents(1).percent_bp(5000), Money::cents(1), "half a kopeck rounds up");
    eq(Money::rub(500).percent_bp(1000), Money::rub(50), "10% of 500");

    // --- сценарий 7: инварианты корзины ---
    bool threw = false;
    try {
        cart.add({"bad", Money::rub(1), 0});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "non-positive quantity rejected");

    if (failures == 0) {
        std::cout << "All tests passed\n";
        return 0;
    }
    std::cout << failures << " test(s) failed\n";
    return 1;
}
