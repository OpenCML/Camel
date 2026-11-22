// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/compile/literal.h"

#include "camel/core/rtdata/half.h"
#include "camel/utils/number_text.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace camel::compile::literal {

namespace {

// =============================================================================
// A signed integer of any size, in 32-bit limbs (little endian).
//
// Literal arithmetic must not overflow on the way to its result: `9223372036854775807 + 1`
// is 9223372036854775808 exactly, and only then does the target type (here the default
// `int`) say that it cannot hold it. Every operation below keeps the whole value.
// =============================================================================

using Limb  = std::uint32_t;
using Limbs = std::vector<Limb>;

/// A bound on what a fold may compute. A literal expression that would need more bits than
/// this (`2 ^ 1000000`) is left to run time instead of being folded, so that a program cannot
/// make the compiler compute for ever.
constexpr std::size_t kMaxBits = std::size_t(1) << 17;

int compareMagnitude(const Limbs &a, const Limbs &b) {
    if (a.size() != b.size()) {
        return a.size() < b.size() ? -1 : 1;
    }
    for (std::size_t i = a.size(); i-- > 0;) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

Limbs addMagnitude(const Limbs &a, const Limbs &b) {
    Limbs out(std::max(a.size(), b.size()) + 1, 0);
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        std::uint64_t sum = carry;
        if (i < a.size()) {
            sum += a[i];
        }
        if (i < b.size()) {
            sum += b[i];
        }
        out[i] = static_cast<Limb>(sum);
        carry  = sum >> 32;
    }
    while (!out.empty() && out.back() == 0) {
        out.pop_back();
    }
    return out;
}

/// `a - b` with `a >= b`.
Limbs subMagnitude(const Limbs &a, const Limbs &b) {
    Limbs out(a.size(), 0);
    std::int64_t borrow = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::int64_t diff = static_cast<std::int64_t>(a[i]) - borrow;
        if (i < b.size()) {
            diff -= static_cast<std::int64_t>(b[i]);
        }
        if (diff < 0) {
            diff += std::int64_t(1) << 32;
            borrow = 1;
        } else {
            borrow = 0;
        }
        out[i] = static_cast<Limb>(diff);
    }
    while (!out.empty() && out.back() == 0) {
        out.pop_back();
    }
    return out;
}

Limbs multiplyMagnitude(const Limbs &a, const Limbs &b) {
    if (a.empty() || b.empty()) {
        return {};
    }
    Limbs out(a.size() + b.size(), 0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::uint64_t carry = 0;
        for (std::size_t j = 0; j < b.size(); ++j) {
            std::uint64_t current = static_cast<std::uint64_t>(a[i]) * b[j] + out[i + j] + carry;
            out[i + j]            = static_cast<Limb>(current);
            carry                 = current >> 32;
        }
        std::size_t k = i + b.size();
        while (carry != 0) {
            std::uint64_t current = static_cast<std::uint64_t>(out[k]) + carry;
            out[k]                = static_cast<Limb>(current);
            carry                 = current >> 32;
            ++k;
        }
    }
    while (!out.empty() && out.back() == 0) {
        out.pop_back();
    }
    return out;
}

std::size_t bitLength(const Limbs &a) {
    if (a.empty()) {
        return 0;
    }
    Limb top         = a.back();
    std::size_t bits = (a.size() - 1) * 32;
    while (top != 0) {
        ++bits;
        top >>= 1;
    }
    return bits;
}

bool bitAt(const Limbs &a, std::size_t index) {
    const std::size_t limb = index / 32;
    return limb < a.size() && ((a[limb] >> (index % 32)) & 1u) != 0;
}

void setBit(Limbs &a, std::size_t index) {
    const std::size_t limb = index / 32;
    if (a.size() <= limb) {
        a.resize(limb + 1, 0);
    }
    a[limb] |= Limb(1) << (index % 32);
}

Limbs shiftLeft(const Limbs &a, std::size_t bits) {
    if (a.empty() || bits == 0) {
        return a;
    }
    const std::size_t limbs = bits / 32;
    const std::size_t rest  = bits % 32;
    Limbs out(a.size() + limbs + 1, 0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        const std::uint64_t value = static_cast<std::uint64_t>(a[i]) << rest;
        out[i + limbs] |= static_cast<Limb>(value);
        out[i + limbs + 1] |= static_cast<Limb>(value >> 32);
    }
    while (!out.empty() && out.back() == 0) {
        out.pop_back();
    }
    return out;
}

Limbs shiftRight(const Limbs &a, std::size_t bits) {
    const std::size_t limbs = bits / 32;
    const std::size_t rest  = bits % 32;
    if (limbs >= a.size()) {
        return {};
    }
    Limbs out(a.size() - limbs, 0);
    for (std::size_t i = 0; i < out.size(); ++i) {
        std::uint64_t value = a[i + limbs];
        if (rest != 0) {
            value >>= rest;
            if (i + limbs + 1 < a.size()) {
                value |= static_cast<std::uint64_t>(a[i + limbs + 1]) << (32 - rest);
            }
        }
        out[i] = static_cast<Limb>(value);
    }
    while (!out.empty() && out.back() == 0) {
        out.pop_back();
    }
    return out;
}

/// Truncating division of magnitudes; `divisor` is not zero.
void divideMagnitude(const Limbs &a, const Limbs &b, Limbs &quotient, Limbs &remainder) {
    quotient.assign(a.size(), 0);
    remainder.clear();
    for (std::size_t bit = bitLength(a); bit-- > 0;) {
        remainder = shiftLeft(remainder, 1);
        if (bitAt(a, bit)) {
            setBit(remainder, 0);
        }
        if (compareMagnitude(remainder, b) >= 0) {
            remainder = subMagnitude(remainder, b);
            setBit(quotient, bit);
        }
    }
    while (!quotient.empty() && quotient.back() == 0) {
        quotient.pop_back();
    }
}

class BigInt {
  public:
    BigInt() = default;

    explicit BigInt(std::int64_t value) {
        neg_ = value < 0;
        // INT64_MIN has no positive counterpart in int64.
        std::uint64_t magnitude = value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1
                                            : static_cast<std::uint64_t>(value);
        while (magnitude != 0) {
            limbs_.push_back(static_cast<Limb>(magnitude));
            magnitude >>= 32;
        }
    }

    static BigInt fromMagnitude(Limbs limbs, bool negative) {
        BigInt out;
        out.limbs_ = std::move(limbs);
        out.neg_   = negative && !out.limbs_.empty();
        while (!out.limbs_.empty() && out.limbs_.back() == 0) {
            out.limbs_.pop_back();
        }
        return out;
    }

    /// `text` is digits only, in `radix`.
    static std::optional<BigInt> fromDigits(std::string_view text, int radix) {
        if (text.empty()) {
            return std::nullopt;
        }
        BigInt out;
        BigInt factor(static_cast<std::int64_t>(radix));
        for (const char c : text) {
            int digit = -1;
            if (c >= '0' && c <= '9') {
                digit = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                digit = c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                digit = c - 'A' + 10;
            }
            if (digit < 0 || digit >= radix) {
                return std::nullopt;
            }
            out = out * factor + BigInt(static_cast<std::int64_t>(digit));
        }
        return out;
    }

    bool isZero() const { return limbs_.empty(); }
    bool isNegative() const { return neg_; }
    bool isOne() const { return !neg_ && limbs_.size() == 1 && limbs_[0] == 1; }

    BigInt abs() const { return fromMagnitude(limbs_, false); }
    BigInt negated() const {
        BigInt out = *this;
        out.neg_   = !out.neg_ && !out.limbs_.empty();
        return out;
    }

    int compare(const BigInt &other) const {
        if (neg_ != other.neg_) {
            return neg_ ? -1 : 1;
        }
        const int magnitude = compareMagnitude(limbs_, other.limbs_);
        return neg_ ? -magnitude : magnitude;
    }

    BigInt operator+(const BigInt &other) const {
        if (neg_ == other.neg_) {
            return fromMagnitude(addMagnitude(limbs_, other.limbs_), neg_);
        }
        if (compareMagnitude(limbs_, other.limbs_) >= 0) {
            return fromMagnitude(subMagnitude(limbs_, other.limbs_), neg_);
        }
        return fromMagnitude(subMagnitude(other.limbs_, limbs_), other.neg_);
    }

    BigInt operator-(const BigInt &other) const { return *this + other.negated(); }

    BigInt operator*(const BigInt &other) const {
        return fromMagnitude(
            multiplyMagnitude(limbs_, other.limbs_),
            neg_ != other.neg_ && !isZero() && !other.isZero());
    }

    /// Truncating division; `other` is not zero. The remainder takes the sign of this value.
    std::pair<BigInt, BigInt> divMod(const BigInt &other) const {
        Limbs quotient, remainder;
        divideMagnitude(limbs_, other.limbs_, quotient, remainder);
        return {
            fromMagnitude(std::move(quotient), neg_ != other.neg_),
            fromMagnitude(std::move(remainder), neg_)};
    }

    BigInt shiftedLeft(std::size_t bits) const {
        return fromMagnitude(shiftLeft(limbs_, bits), neg_);
    }
    BigInt shiftedRight(std::size_t bits) const {
        return fromMagnitude(shiftRight(limbs_, bits), neg_);
    }
    std::size_t bits() const { return bitLength(limbs_); }

    /// How many factors of two the magnitude has (of a value that is not zero).
    std::size_t trailingZeroBits() const {
        std::size_t count = 0;
        for (const Limb limb : limbs_) {
            if (limb == 0) {
                count += 32;
                continue;
            }
            Limb value = limb;
            while ((value & 1u) == 0) {
                value >>= 1;
                ++count;
            }
            return count;
        }
        return 0;
    }

    bool fitsInt64() const {
        constexpr std::size_t kBits = 63;
        if (limbs_.empty()) {
            return true;
        }
        if (bitLength(limbs_) > kBits + 1) {
            return false;
        }
        const std::uint64_t magnitude = low64();
        const std::uint64_t limit =
            neg_ ? (std::uint64_t(1) << kBits) : (std::uint64_t(1) << kBits) - 1;
        return magnitude <= limit;
    }

    std::int64_t toInt64() const {
        const std::uint64_t magnitude = low64();
        return neg_ ? static_cast<std::int64_t>(~magnitude + 1)
                    : static_cast<std::int64_t>(magnitude);
    }

    std::uint64_t low64() const {
        std::uint64_t out = 0;
        if (!limbs_.empty()) {
            out = limbs_[0];
        }
        if (limbs_.size() > 1) {
            out |= static_cast<std::uint64_t>(limbs_[1]) << 32;
        }
        return out;
    }

    std::string toDecimal() const {
        if (limbs_.empty()) {
            return "0";
        }
        static constexpr std::uint32_t kChunk = 1000000000u;
        Limbs work                            = limbs_;
        std::string text;
        while (!work.empty()) {
            std::uint64_t remainder = 0;
            for (std::size_t i = work.size(); i-- > 0;) {
                const std::uint64_t current = (remainder << 32) | work[i];
                work[i]                     = static_cast<Limb>(current / kChunk);
                remainder                   = current % kChunk;
            }
            while (!work.empty() && work.back() == 0) {
                work.pop_back();
            }
            std::string part = std::to_string(remainder);
            if (!work.empty()) {
                part.insert(0, 9 - part.size(), '0');
            }
            text.insert(0, part);
        }
        return neg_ ? "-" + text : text;
    }

  private:
    Limbs limbs_;
    bool neg_ = false;
};

/// `base ^ exponent` for a non-negative exponent.
BigInt powerOf(const BigInt &base, std::uint64_t exponent) {
    BigInt result(1);
    BigInt square = base;
    while (exponent != 0) {
        if ((exponent & 1) != 0) {
            result = result * square;
        }
        exponent >>= 1;
        if (exponent != 0) {
            square = square * square;
        }
    }
    return result;
}

BigInt powerOfLimb(std::uint64_t base, std::size_t exponent) {
    return powerOf(BigInt(static_cast<std::int64_t>(base)), exponent);
}

// =============================================================================
// An exact rational, reduced, with a positive denominator.
// =============================================================================

struct Rational {
    BigInt num;
    BigInt den = BigInt(1);
};

Rational makeRational(BigInt num, BigInt den) {
    if (den.isNegative()) {
        num = num.negated();
        den = den.negated();
    }
    if (num.isZero()) {
        return {BigInt(), BigInt(1)};
    }
    BigInt a = num.abs();
    BigInt b = den;
    // Euclid on the two magnitudes.
    while (!b.isZero()) {
        auto [quotient, remainder] = a.divMod(b);
        a                          = std::move(b);
        b                          = std::move(remainder);
    }
    if (!a.isOne()) {
        num = num.divMod(a).first;
        den = den.divMod(a).first;
    }
    return {std::move(num), std::move(den)};
}

Rational rationalAdd(const Rational &left, const Rational &right) {
    return makeRational(left.num * right.den + right.num * left.den, left.den * right.den);
}

Rational rationalSub(const Rational &left, const Rational &right) {
    return makeRational(left.num * right.den - right.num * left.den, left.den * right.den);
}

Rational rationalMul(const Rational &left, const Rational &right) {
    return makeRational(left.num * right.num, left.den * right.den);
}

std::optional<Rational> rationalDiv(const Rational &left, const Rational &right) {
    if (right.num.isZero()) {
        return std::nullopt;
    }
    return makeRational(left.num * right.den, left.den * right.num);
}

int rationalCompare(const Rational &left, const Rational &right) {
    return (left.num * right.den).compare(right.num * left.den);
}

bool rationalIntegral(const Rational &value) { return value.den.isOne(); }

// =============================================================================
// Decimal text of a rational: exact when it terminates, otherwise a rounded quotient.
// =============================================================================

/// The exact decimal of `value` (no exponent), or nothing when the decimal does not terminate
/// or would ask for more precision than is worth computing.
std::optional<std::string> exactDecimal(const Rational &value) {
    if (value.num.isZero()) {
        return std::string("0");
    }
    BigInt numerator = value.num.abs();
    BigInt reduced   = value.den;
    if (reduced.bits() > 32768) {
        return std::nullopt;
    }
    // Only a denominator of the form 2^a * 5^b has a terminating decimal. The factors of two
    // are counted from the low end; the factors of five by a binary search over the powers of
    // five that could divide it (5^k > 2^(2k), so k is at most half the bits).
    const std::size_t twos = reduced.trailingZeroBits();
    if (twos != 0) {
        reduced = reduced.shiftedRight(twos);
    }
    std::size_t fives = 0;
    std::size_t upper = std::min<std::size_t>(reduced.bits() / 2 + 1, 4096);
    std::size_t low   = 0;
    std::size_t high  = upper;
    while (low < high) {
        const std::size_t middle = low + (high - low + 1) / 2;
        bool divides             = false;
        if (const BigInt power = powerOfLimb(5, middle); power.bits() <= reduced.bits()) {
            divides = reduced.divMod(power).second.isZero();
        }
        if (divides) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    fives = low;
    if (fives != 0) {
        reduced = reduced.divMod(powerOfLimb(5, fives)).first;
    }
    if (!reduced.isOne()) {
        return std::nullopt;
    }
    const std::size_t places = std::max(twos, fives);
    BigInt scaled            = numerator;
    if (places > twos) {
        scaled = scaled * powerOfLimb(2, places - twos);
    }
    if (places > fives) {
        scaled = scaled * powerOfLimb(5, places - fives);
    }
    std::string digits = scaled.toDecimal();
    if (places == 0) {
        return (value.num.isNegative() ? "-" : "") + digits;
    }
    if (digits.size() <= static_cast<std::size_t>(places)) {
        digits.insert(0, static_cast<std::size_t>(places) - digits.size() + 1, '0');
    }
    digits.insert(digits.size() - static_cast<std::size_t>(places), ".");
    while (digits.size() > 1 && digits.back() == '0') {
        digits.pop_back();
    }
    if (digits.back() == '.') {
        digits.pop_back();
    }
    return (value.num.isNegative() ? "-" : "") + digits;
}

/// `significant` digits of the quotient `value`, rounded to the nearest at the last one, in
/// plain decimal (no exponent). Used for a value whose decimal does not terminate.
std::string approximateDecimal(const Rational &value, std::size_t significant) {
    if (value.num.isZero()) {
        return "0";
    }
    const BigInt numerator = value.num.abs();
    const BigInt &den      = value.den;
    // Where the first significant digit is: the binary exponent times log10(2), which is
    // within a digit of the decimal one; the loop below corrects it.
    std::int64_t exponent = static_cast<std::int64_t>(std::floor(
        static_cast<double>(
            static_cast<std::int64_t>(numerator.bits()) - static_cast<std::int64_t>(den.bits())) *
        0.30102999566398120));
    exponent              = std::clamp<std::int64_t>(exponent, -8000, 8000);
    std::int64_t scale    = static_cast<std::int64_t>(significant) - 1 - exponent;
    BigInt quotient;
    BigInt remainder;
    BigInt scaledDen;
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (scale > 8192 || scale < -8192) {
            return "0";
        }
        BigInt scaledNumerator = numerator;
        scaledDen              = den;
        if (scale >= 0) {
            scaledNumerator = scaledNumerator * powerOfLimb(10, static_cast<std::size_t>(scale));
        } else {
            scaledDen = scaledDen * powerOfLimb(10, static_cast<std::size_t>(-scale));
        }
        auto divided             = scaledNumerator.divMod(scaledDen);
        quotient                 = std::move(divided.first);
        remainder                = std::move(divided.second);
        const std::size_t digits = quotient.toDecimal().size();
        if (digits == significant) {
            break;
        }
        if (digits < significant) {
            scale += static_cast<std::int64_t>(significant - digits);
        } else {
            scale -= static_cast<std::int64_t>(digits - significant);
        }
    }
    if (!remainder.isZero()) {
        // Round half up at the last digit; an exact tie is all but impossible here and the
        // text is either read back as a float or printed.
        if ((remainder * BigInt(2)).compare(scaledDen) >= 0) {
            quotient = quotient + BigInt(1);
        }
    }
    std::string digits       = quotient.toDecimal();
    const std::int64_t point = static_cast<std::int64_t>(digits.size()) - scale;
    std::string text         = value.num.isNegative() ? "-" : "";
    if (point <= 0) {
        text += "0.";
        text.append(static_cast<std::size_t>(-point), '0');
        text += digits;
    } else if (point >= static_cast<std::int64_t>(digits.size())) {
        text += digits;
        text.append(static_cast<std::size_t>(point) - digits.size(), '0');
    } else {
        text += digits.substr(0, static_cast<std::size_t>(point));
        text += ".";
        text += digits.substr(static_cast<std::size_t>(point));
    }
    return text;
}

/// Long decimal text shortened into scientific notation (diagnostics only).
std::string scientific(const std::string &decimal) {
    const bool negative     = !decimal.empty() && decimal.front() == '-';
    const std::size_t start = negative ? 1 : 0;
    std::string digits;
    std::int64_t point         = 0;
    bool seenPoint             = false;
    std::int64_t integerDigits = 0;
    for (std::size_t i = start; i < decimal.size(); ++i) {
        const char c = decimal[i];
        if (c == '.') {
            seenPoint = true;
            continue;
        }
        if (!seenPoint) {
            ++integerDigits;
        }
        digits.push_back(c);
    }
    std::size_t first = digits.find_first_not_of('0');
    if (first == std::string::npos) {
        return "0";
    }
    point = integerDigits - static_cast<std::int64_t>(first) - 1;
    digits.erase(0, first);
    while (digits.size() > 1 && digits.back() == '0') {
        digits.pop_back();
    }
    if (digits.size() > 17) {
        digits.resize(17);
    }
    std::string text = negative ? "-" : "";
    text += digits[0];
    if (digits.size() > 1) {
        text += ".";
        text += digits.substr(1);
    }
    text += "e";
    text += point < 0 ? "-" : "+";
    text += std::to_string(point < 0 ? -point : point);
    return text;
}

} // namespace

// =============================================================================
// Value
// =============================================================================

struct Value::Rep {
    Rational rational;
    /// A zero written with a minus (`-0.0`): the value is zero, and it stays a negative zero
    /// when it becomes a float, so that `1.0 / -0.0` is -infinity as it reads.
    bool negativeZero = false;
    /// The imaginary part (zero for an Integer or Float value).
    Rational imaginary{Rational{}};
    bool imaginaryNegativeZero = false;
};

Value::Value() : kind_(Kind::Integer), rep_(std::make_shared<const Rep>(Rep{Rational{}})) {}

Value::Value(Kind kind, std::shared_ptr<const Rep> rep) : kind_(kind), rep_(std::move(rep)) {}

Value::Value(const Value &)            = default;
Value &Value::operator=(const Value &) = default;
Value::~Value()                        = default;

Value Value::fromInt64(std::int64_t value) {
    return Value(
        Kind::Integer,
        std::make_shared<const Rep>(Rep{Rational{BigInt(value), BigInt(1)}}));
}

Value Value::fromDouble(double value) {
    // The exact value of the double: mantissa * 2^exponent.
    if (value == 0.0 || !std::isfinite(value)) {
        return fromInt64(0);
    }
    const bool negative     = std::signbit(value);
    int exponent            = 0;
    const double normalized = std::frexp(std::abs(value), &exponent); // [0.5, 1)
    // normalized * 2^53 is an integer for a normal double; for a subnormal, frexp still gives
    // an exact mantissa with a small exponent.
    const double mantissa = std::ldexp(normalized, 53);
    BigInt numerator(static_cast<std::int64_t>(mantissa));
    exponent -= 53;
    Rational rational;
    if (exponent >= 0) {
        rational =
            makeRational(numerator.shiftedLeft(static_cast<std::size_t>(exponent)), BigInt(1));
    } else {
        rational =
            makeRational(numerator, BigInt(1).shiftedLeft(static_cast<std::size_t>(-exponent)));
    }
    if (negative) {
        rational.num = rational.num.negated();
    }
    return Value(
        Kind::Float,
        std::make_shared<const Rep>(Rep{std::move(rational), negative && value == 0.0}));
}

std::optional<Value> Value::fromImaginaryText(std::string_view text) {
    bool negative = false;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    if (text.empty() || text.back() != 'i') {
        return std::nullopt;
    }
    text.remove_suffix(1);
    std::optional<Value> body = fromRealText(negative ? "-" + std::string(text) : text);
    if (!body.has_value()) {
        return std::nullopt;
    }
    Rep rep{
        Rational{},
        false,
        body->rep_->rational,
        body->rep_->negativeZero || (negative && body->rep_->rational.num.isZero())};
    return Value(Kind::Imaginary, std::make_shared<const Rep>(std::move(rep)));
}

bool Value::hasImaginaryPart() const { return kind_ == Kind::Imaginary || kind_ == Kind::Complex; }

std::optional<Value> Value::fromIntegerText(std::string_view text) {
    bool negative = false;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    int radix = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        radix = 16;
        text.remove_prefix(2);
    } else if (text.size() > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
        radix = 2;
        text.remove_prefix(2);
    } else if (text.size() > 2 && text[0] == '0' && (text[1] == 'o' || text[1] == 'O')) {
        radix = 8;
        text.remove_prefix(2);
    } else if (text.size() > 1 && text[0] == '0') {
        radix = 8;
        text.remove_prefix(1);
    }
    // Digit separators are not part of the language.
    if (text.empty()) {
        return std::nullopt;
    }
    auto magnitude = BigInt::fromDigits(text, radix);
    if (!magnitude.has_value()) {
        return std::nullopt;
    }
    Rational rational{negative ? magnitude->negated() : *magnitude, BigInt(1)};
    return Value(Kind::Integer, std::make_shared<const Rep>(Rep{std::move(rational), false}));
}

std::optional<Value> Value::fromRealText(std::string_view text) {
    bool negative = false;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    std::string digits;
    std::int64_t fractionDigits = 0;
    bool seenPoint              = false;
    std::size_t i               = 0;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c >= '0' && c <= '9') {
            digits.push_back(c);
            if (seenPoint) {
                ++fractionDigits;
            }
            continue;
        }
        if (c == '.' && !seenPoint) {
            seenPoint = true;
            continue;
        }
        break;
    }
    std::int64_t exponent = 0;
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        bool exponentNegative = false;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            exponentNegative = text[i] == '-';
            ++i;
        }
        std::int64_t value = 0;
        bool any           = false;
        for (; i < text.size(); ++i) {
            if (text[i] < '0' || text[i] > '9') {
                return std::nullopt;
            }
            any = true;
            if (value > 1000000) {
                return std::nullopt;
            }
            value = value * 10 + (text[i] - '0');
        }
        if (!any) {
            return std::nullopt;
        }
        exponent = exponentNegative ? -value : value;
    }
    if (i != text.size() || digits.empty()) {
        return std::nullopt;
    }
    auto mantissa = BigInt::fromDigits(digits, 10);
    if (!mantissa.has_value()) {
        return std::nullopt;
    }
    const std::int64_t scale = fractionDigits - exponent;
    Rational rational;
    if (scale > 0) {
        if (scale > 8192) {
            return std::nullopt;
        }
        rational = makeRational(*mantissa, powerOfLimb(10, static_cast<std::size_t>(scale)));
    } else {
        if (-scale > 8192) {
            return std::nullopt;
        }
        rational =
            makeRational(*mantissa * powerOfLimb(10, static_cast<std::size_t>(-scale)), BigInt(1));
    }
    if (negative) {
        rational.num = rational.num.negated();
    }
    const bool negativeZero = negative && rational.num.isZero();
    return Value(Kind::Float, std::make_shared<const Rep>(Rep{std::move(rational), negativeZero}));
}

bool Value::isZero() const { return rep_->rational.num.isZero(); }

bool Value::isNegative() const { return rep_->rational.num.isNegative(); }

std::string Value::toFloatText() const {
    if (hasImaginaryPart()) {
        return toString();
    }
    if (rep_->rational.num.isZero() && rep_->negativeZero) {
        return "-0";
    }
    if (const auto exact = exactDecimal(rep_->rational); exact.has_value()) {
        return *exact;
    }
    return approximateDecimal(rep_->rational, 50);
}

namespace {

/// The text of one part of an exact value (a complex number writes two).
std::string rationalText(const Rational &value, bool negativeZero) {
    if (value.num.isZero() && negativeZero) {
        return "-0";
    }
    std::string text;
    if (const auto exact = exactDecimal(value); exact.has_value()) {
        text = *exact;
    } else {
        text = approximateDecimal(value, 17);
    }
    if (text.size() > 40 && text.find('/') == std::string::npos) {
        return scientific(text);
    }
    return text;
}

} // namespace

std::string Value::toString() const {
    if (hasImaginaryPart()) {
        // Design principles 5.1 writes a complex literal as `3+4i`: both parts, the sign of
        // the imaginary one, its magnitude, then `i`.
        const bool imaginaryNegative =
            rep_->imaginary.num.isNegative() ||
            (rep_->imaginary.num.isZero() && rep_->imaginaryNegativeZero);
        Rational magnitude = rep_->imaginary;
        if (imaginaryNegative) {
            magnitude.num = magnitude.num.negated();
        }
        return rationalText(rep_->rational, rep_->negativeZero) + (imaginaryNegative ? "-" : "+") +
               rationalText(magnitude, false) + "i";
    }
    return rationalText(rep_->rational, rep_->negativeZero);
}

std::optional<std::int64_t> Value::asInt64() const {
    if (kind_ != Kind::Integer || !rationalIntegral(rep_->rational)) {
        return std::nullopt;
    }
    const BigInt &value = rep_->rational.num;
    if (!value.fitsInt64()) {
        return std::nullopt;
    }
    return value.toInt64();
}

namespace {

/// Whether the magnitude of `value` is at most `2^-exponent`.
bool atMostTiny(const Rational &value, int exponent) {
    BigInt scaled = value.num.abs().shiftedLeft(static_cast<std::size_t>(exponent));
    return scaled.compare(value.den) <= 0;
}

template <typename T> std::optional<T> toFloating(const Rational &value, bool negativeZero) {
    if (value.num.isZero()) {
        T zero{};
        return negativeZero ? -zero : zero;
    }
    const std::string text = exactDecimal(value).value_or(approximateDecimal(value, 50));
    T converted{};
    // parseWholeFloat refuses an overflow and an underflow alike: neither is a value the
    // type holds, so neither is rounded to zero or to an infinity here.
    const auto status = camel::utils::parseWholeFloat<T>(text, converted);
    return status == camel::utils::NumberTextStatus::Ok ? std::optional<T>(converted)
                                                        : std::nullopt;
}

/// The bf16 or f16 nearest the written value, which is the value a literal of either type is
/// (design principles 5.1, 7.3): one rounding of the exact rational, never two. The value is
/// brought to f32 first, which loses nothing that can change the answer: a rounding to 8
/// significand bits (bf16) or 11 (f16) through 24 bits is the same as rounding the exact value
/// directly. Nothing when the type has no value to give it: an f32 overflow or underflow (the
/// same refusal an f32 literal gets), or an infinite result for a finite written value.
template <typename Half>
std::optional<camel::core::data::data_ptr_t>
toHalfData(const Rational &value, bool negativeZero, camel::core::type::Type *type) {
    const auto asFloat = toFloating<float>(value, negativeZero);
    if (!asFloat.has_value()) {
        return std::nullopt;
    }
    const slot_t rounded = camel::core::rtdata::halfFromFloat<Half>(*asFloat);
    if (camel::core::rtdata::halfOverflows<Half>(*asFloat)) {
        return std::nullopt;
    }
    return std::make_shared<camel::core::data::HalfData>(
        camel::core::rtdata::halfBitsOfSlot(rounded),
        type);
}

/// Whether the bf16 or f16 nearest the written value is zero while the written value is not:
/// the underflow of the reduced type, asked by the boundary that rounds to zero, so that no
/// third rounding of the exact value is involved.
template <typename Half> bool halfUnderflows(const Rational &value) {
    const auto asFloat = toFloating<float>(value, false);
    if (!asFloat.has_value() || *asFloat == 0.0f) {
        return false;
    }
    return camel::core::rtdata::halfToFloat<Half>(
               camel::core::rtdata::halfFromFloat<Half>(*asFloat)) == 0.0f;
}

} // namespace

std::optional<camel::core::data::data_ptr_t> Value::toData(camel::core::type::Type *type) const {
    using camel::core::type::TypeCode;
    if (type == nullptr) {
        return std::nullopt;
    }
    if (camel::core::type::isComplex(type->code())) {
        const bool single = type->code() == TypeCode::Complex64;
        if (single) {
            const auto real      = toFloating<float>(rep_->rational, rep_->negativeZero);
            const auto imaginary = toFloating<float>(rep_->imaginary, rep_->imaginaryNegativeZero);
            if (!real.has_value() || !imaginary.has_value()) {
                return std::nullopt;
            }
            return std::make_shared<camel::core::data::ComplexData>(
                static_cast<double>(*real),
                static_cast<double>(*imaginary),
                type);
        }
        const auto real      = toFloating<double>(rep_->rational, rep_->negativeZero);
        const auto imaginary = toFloating<double>(rep_->imaginary, rep_->imaginaryNegativeZero);
        if (!real.has_value() || !imaginary.has_value()) {
            return std::nullopt;
        }
        return std::make_shared<camel::core::data::ComplexData>(*real, *imaginary, type);
    }
    if (hasImaginaryPart()) {
        // A complex value does not become a real one by itself.
        return std::nullopt;
    }
    switch (type->code()) {
    case TypeCode::Int32: {
        const auto value = asInt64();
        if (!value.has_value() || *value < std::numeric_limits<std::int32_t>::min() ||
            *value > std::numeric_limits<std::int32_t>::max()) {
            return std::nullopt;
        }
        return std::make_shared<camel::core::data::IntData>(static_cast<std::int32_t>(*value));
    }
    case TypeCode::Int64: {
        const auto value = asInt64();
        if (!value.has_value()) {
            return std::nullopt;
        }
        return std::make_shared<camel::core::data::LongData>(*value);
    }
    case TypeCode::Float32: {
        const auto value = toFloating<float>(rep_->rational, rep_->negativeZero);
        if (!value.has_value()) {
            return std::nullopt;
        }
        return std::make_shared<camel::core::data::FloatData>(*value);
    }
    case TypeCode::Float64: {
        const auto value = toFloating<double>(rep_->rational, rep_->negativeZero);
        if (!value.has_value()) {
            return std::nullopt;
        }
        return std::make_shared<camel::core::data::DoubleData>(*value);
    }
    case TypeCode::BFloat16:
        return toHalfData<camel::core::rtdata::BFloat16>(rep_->rational, rep_->negativeZero, type);
    case TypeCode::Float16:
        return toHalfData<camel::core::rtdata::Float16>(rep_->rational, rep_->negativeZero, type);
    default:
        return std::nullopt;
    }
}

namespace {

/// Whether one part of a value is nonzero and below half the smallest subnormal of T.
template <typename T> bool partUnderflows(const Rational &value) {
    if (value.num.isZero()) {
        return false;
    }
    // The same boundary toFloating used before: 2^-kHalfUlpExponent is half the smallest
    // subnormal of T, the largest magnitude that rounds to zero.
    constexpr int kHalfUlpExponent =
        std::numeric_limits<T>::digits - std::numeric_limits<T>::min_exponent + 1;
    return atMostTiny(value, kHalfUlpExponent);
}

} // namespace

bool Value::underflows(camel::core::type::Type *type) const {
    using camel::core::type::TypeCode;
    if (type == nullptr) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Float32:
        return !hasImaginaryPart() && partUnderflows<float>(rep_->rational);
    case TypeCode::Float64:
        return !hasImaginaryPart() && partUnderflows<double>(rep_->rational);
    case TypeCode::BFloat16:
        return !hasImaginaryPart() && halfUnderflows<camel::core::rtdata::BFloat16>(rep_->rational);
    case TypeCode::Float16:
        return !hasImaginaryPart() && halfUnderflows<camel::core::rtdata::Float16>(rep_->rational);
    case TypeCode::Complex64:
        return partUnderflows<float>(rep_->rational) || partUnderflows<float>(rep_->imaginary);
    case TypeCode::Complex128:
        return partUnderflows<double>(rep_->rational) || partUnderflows<double>(rep_->imaginary);
    default:
        return false;
    }
}

namespace {

bool hasImaginary(const Value &value) { return value.hasImaginaryPart(); }

/// The kind of a folded operation: complex when either operand has an imaginary part,
/// otherwise integer only when both operands are integers.
Kind foldedKind(const Value &left, const Value &right, bool complex) {
    if (complex) {
        return Kind::Complex;
    }
    return left.kind() == Kind::Integer && right.kind() == Kind::Integer ? Kind::Integer
                                                                         : Kind::Float;
}

std::shared_ptr<const Value::Rep> complexRep(Rational real, Rational imaginary) {
    return std::make_shared<const Value::Rep>(
        Value::Rep{std::move(real), false, std::move(imaginary), false});
}

} // namespace

std::optional<Value> Value::add(const Value &left, const Value &right) {
    const bool complex = hasImaginary(left) || hasImaginary(right);
    Rational real      = rationalAdd(left.rep_->rational, right.rep_->rational);
    Rational imaginary = rationalAdd(left.rep_->imaginary, right.rep_->imaginary);
    if (!complex) {
        return Value(foldedKind(left, right, false), std::make_shared<const Rep>(Rep{real}));
    }
    return Value(Kind::Complex, complexRep(std::move(real), std::move(imaginary)));
}

std::optional<Value> Value::sub(const Value &left, const Value &right) {
    const bool complex = hasImaginary(left) || hasImaginary(right);
    Rational real      = rationalSub(left.rep_->rational, right.rep_->rational);
    Rational imaginary = rationalSub(left.rep_->imaginary, right.rep_->imaginary);
    if (!complex) {
        return Value(foldedKind(left, right, false), std::make_shared<const Rep>(Rep{real}));
    }
    return Value(Kind::Complex, complexRep(std::move(real), std::move(imaginary)));
}

std::optional<Value> Value::mul(const Value &left, const Value &right) {
    const Rational &a = left.rep_->rational;
    const Rational &b = right.rep_->rational;
    const Rational &c = left.rep_->imaginary;
    const Rational &d = right.rep_->imaginary;
    if (a.num.bits() + b.num.bits() > kMaxBits || c.num.bits() + d.num.bits() > kMaxBits) {
        return std::nullopt;
    }
    const bool complex = hasImaginary(left) || hasImaginary(right);
    if (!complex) {
        return Value(
            foldedKind(left, right, false),
            std::make_shared<const Rep>(Rep{rationalMul(a, b)}));
    }
    // (a + ci)(b + di) = (ab - cd) + (ad + cb)i, where a, b are the real parts and c, d the
    // imaginary parts.
    Rational real      = rationalSub(rationalMul(a, b), rationalMul(c, d));
    Rational imaginary = rationalAdd(rationalMul(a, d), rationalMul(c, b));
    return Value(Kind::Complex, complexRep(std::move(real), std::move(imaginary)));
}

std::optional<Value> Value::div(const Value &left, const Value &right) {
    const Rational &a  = left.rep_->rational;
    const Rational &b  = right.rep_->rational;
    const Rational &c  = left.rep_->imaginary;
    const Rational &d  = right.rep_->imaginary;
    const bool complex = hasImaginary(left) || hasImaginary(right);
    if (!complex) {
        if (b.num.isZero()) {
            return std::nullopt;
        }
        if (left.kind_ == Kind::Integer && right.kind_ == Kind::Integer) {
            return Value(
                Kind::Integer,
                std::make_shared<const Rep>(Rep{a.num.divMod(b.num).first, BigInt(1)}));
        }
        const auto quotient = rationalDiv(a, b);
        if (!quotient.has_value()) {
            return std::nullopt;
        }
        return Value(Kind::Float, std::make_shared<const Rep>(Rep{*quotient}));
    }
    // (a + ci) / (b + di) = ((ab + cd) + (cb - ad)i) / (b^2 + d^2)
    const Rational denominator = rationalAdd(rationalMul(b, b), rationalMul(d, d));
    if (denominator.num.isZero()) {
        return std::nullopt;
    }
    const auto real = rationalDiv(rationalAdd(rationalMul(a, b), rationalMul(c, d)), denominator);
    const auto imaginary =
        rationalDiv(rationalSub(rationalMul(c, b), rationalMul(a, d)), denominator);
    if (!real.has_value() || !imaginary.has_value()) {
        return std::nullopt;
    }
    return Value(Kind::Complex, complexRep(std::move(*real), std::move(*imaginary)));
}

std::optional<Value> Value::mod(const Value &left, const Value &right) {
    if (left.hasImaginaryPart() || right.hasImaginaryPart()) {
        return std::nullopt;
    }
    if (left.kind_ != Kind::Integer || right.kind_ != Kind::Integer) {
        return std::nullopt;
    }
    const Rational &a = left.rep_->rational;
    const Rational &b = right.rep_->rational;
    if (b.num.isZero()) {
        return std::nullopt;
    }
    return Value(
        Kind::Integer,
        std::make_shared<const Rep>(Rep{a.num.divMod(b.num).second, BigInt(1)}));
}

namespace {

std::optional<std::uint64_t> integerExponent(const Rational &value) {
    if (!value.den.isOne() || value.num.isNegative() || !value.num.fitsInt64()) {
        return std::nullopt;
    }
    const std::int64_t exponent = value.num.toInt64();
    if (exponent < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(exponent);
}

} // namespace

std::optional<Value> Value::pow(const Value &base, const Value &exponent) {
    if (base.hasImaginaryPart() || exponent.hasImaginaryPart()) {
        // A complex power is not folded here: it is a math-module operation, not a literal
        // operator (the language has no complex `**`).
        return std::nullopt;
    }
    const Rational &b = base.rep_->rational;
    const Rational &e = exponent.rep_->rational;
    const auto power  = integerExponent(e);
    if (!power.has_value() || *power > 4096) {
        return std::nullopt;
    }
    const Kind kind = base.kind_ == Kind::Integer && exponent.kind_ == Kind::Integer ? Kind::Integer
                                                                                     : Kind::Float;
    if (*power == 0) {
        Rational one{BigInt(1), BigInt(1)};
        return Value(kind, std::make_shared<const Rep>(Rep{std::move(one)}));
    }
    if (b.num.isZero()) {
        return fromInt64(0);
    }
    if (b.num.bits() * (*power) > kMaxBits) {
        return std::nullopt;
    }
    Rational result{powerOf(b.num, *power), powerOf(b.den, *power)};
    return Value(kind, std::make_shared<const Rep>(Rep{makeRational(result.num, result.den)}));
}

std::optional<Value> Value::negate(const Value &value) {
    if (value.hasImaginaryPart()) {
        Rational real              = value.rep_->rational;
        Rational imaginary         = value.rep_->imaginary;
        bool imaginaryNegativeZero = value.rep_->imaginaryNegativeZero;
        if (imaginary.num.isZero()) {
            imaginaryNegativeZero = !imaginaryNegativeZero;
        } else {
            imaginary.num = imaginary.num.negated();
        }
        bool realNegativeZero = value.rep_->negativeZero;
        if (real.num.isZero()) {
            realNegativeZero = !realNegativeZero;
        } else {
            real.num = real.num.negated();
        }
        return Value(
            value.kind_,
            std::make_shared<const Rep>(
                Rep{std::move(real),
                    realNegativeZero,
                    std::move(imaginary),
                    imaginaryNegativeZero}));
    }
    Rational result = value.rep_->rational;
    if (result.num.isZero()) {
        // `-0.0` keeps the sign it was written with (the parser folds the minus into the
        // literal), so that `1.0 / -0.0` is -infinity as written.
        return Value(value.kind_, std::make_shared<const Rep>(Rep{std::move(result)}));
    }
    result.num = result.num.negated();
    return Value(value.kind_, std::make_shared<const Rep>(Rep{std::move(result)}));
}

int Value::compare(const Value &other) const {
    if (hasImaginaryPart() || other.hasImaginaryPart()) {
        // Equality is the only comparison a complex number has; the lexicographic order here
        // exists so that `==` and `!=` fold. `foldedComparison` does not fold `<` and friends
        // on an imaginary value.
        const int real = rationalCompare(rep_->rational, other.rep_->rational);
        if (real != 0) {
            return real;
        }
        return rationalCompare(rep_->imaginary, other.rep_->imaginary);
    }
    return rationalCompare(rep_->rational, other.rep_->rational);
}

bool kindEnters(const Value &value, camel::core::type::Type *type) {
    using camel::core::type::TypeCode;
    if (type == nullptr) {
        return false;
    }
    if (camel::core::type::isComplex(type->code())) {
        return true;
    }
    if (value.hasImaginaryPart()) {
        return false;
    }
    if (!type->isPrimitive()) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Int32:
    case TypeCode::Int64:
        // An integer can be an integer of any range; a float never becomes an integer by
        // itself (`let x: int = 1.0` is an error, as `1.0 as int` is one written down).
        return value.kind() == Kind::Integer;
    case TypeCode::Float32:
    case TypeCode::Float64:
        return true;
    // A bf16 or f16 is a floating type with a range and a precision of its own: `let x: bf16 =
    // 0.1` is legal and takes the nearest bf16, and a value the type cannot hold is refused with
    // that type's range (design principles 5.1, 7.3).
    case TypeCode::BFloat16:
    case TypeCode::Float16:
        return true;
    default:
        return false;
    }
}

std::string rangeText(camel::core::type::Type *type) {
    using camel::core::type::TypeCode;
    if (type == nullptr) {
        return {};
    }
    switch (type->code()) {
    case TypeCode::Int32:
        return camel::utils::integerRangeText<int32_t>();
    case TypeCode::Int64:
        return camel::utils::integerRangeText<int64_t>();
    case TypeCode::Float32:
        return std::string(camel::utils::floatRangeText<float>());
    case TypeCode::Float64:
        return std::string(camel::utils::floatRangeText<double>());
    case TypeCode::BFloat16:
        return std::string(camel::utils::halfRangeText(true));
    case TypeCode::Float16:
        return std::string(camel::utils::halfRangeText(false));
    case TypeCode::Complex64:
        return camel::utils::complexRangeText<float>();
    case TypeCode::Complex128:
        return camel::utils::complexRangeText<double>();
    default:
        return {};
    }
}

} // namespace camel::compile::literal