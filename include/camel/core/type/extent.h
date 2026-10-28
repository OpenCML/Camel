// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Compile-time extents (design principles 5.4).
 *
 * A type may state its dimensions as expressions over names a signature binds:
 *
 *     func dense(x: f32[n, 3], w: f32[3, k])
 *         : f32[n, k]
 *
 * `constas n` binds the name where it first appears in a parameter type; later occurrences
 * write `n`. A dimension is a linear form: a constant plus integer multiples of names. That is
 * enough for `n`, `n + 1`, `2 * n` and `n - k`, whose equality can be decided; any other
 * expression (division, a product of two named dimensions, a call) is unknown.
 *
 * Extents are compile-time data: they are checked against the actual argument types at a call,
 * but never become part of a runtime value, and they never make the compiler build a second
 * instance of the function.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace camel::core::type {

/// The values bound to dimension names at one call: name -> extent.
using ExtentValues = std::map<std::string, int64_t>;

struct ExtentExpr;

/// What evaluates a call written in an extent (`conv_out(n, k, s, p)`): the compiler installs it
/// while it builds a program, because only the compiler can run a function at build time. `call`
/// is the compiler's own tree of the call, held by the expression and not read by the type system.
using ExtentCallEvaluator =
    std::function<std::optional<int64_t>(const std::shared_ptr<const void> &call, const ExtentValues &)>;

/// The evaluator in force on this thread; empty outside a build.
inline ExtentCallEvaluator &extentCallEvaluator() {
    static thread_local ExtentCallEvaluator evaluator;
    return evaluator;
}

/// An integer expression over names, for an extent that is not a linear form: `(n + 2 * p - k) /
/// s + 1`. It is evaluated when every name it uses is bound (to a dimension of an argument, or to
/// the value of a parameter the call gives as a constant), and stands for nothing before that.
struct ExtentExpr {
    enum class Op : uint8_t { Constant, Name, Add, Sub, Mul, Div, Mod, Neg, Call };

    Op op = Op::Constant;
    int64_t value = 0;
    std::string name;
    std::shared_ptr<const ExtentExpr> lhs;
    std::shared_ptr<const ExtentExpr> rhs;
    /// A call: the compiler's tree of it, the names it reads and how it is written.
    std::shared_ptr<const void> call;
    std::vector<std::string> callNames;

    static std::shared_ptr<const ExtentExpr> makeCall(
        std::shared_ptr<const void> call, std::vector<std::string> names, std::string text) {
        auto expr       = std::make_shared<ExtentExpr>();
        expr->op        = Op::Call;
        expr->call      = std::move(call);
        expr->callNames = std::move(names);
        expr->name      = std::move(text);
        return expr;
    }

    static std::shared_ptr<const ExtentExpr> makeConstant(int64_t value) {
        auto expr   = std::make_shared<ExtentExpr>();
        expr->op    = Op::Constant;
        expr->value = value;
        return expr;
    }
    static std::shared_ptr<const ExtentExpr> makeName(std::string name) {
        auto expr  = std::make_shared<ExtentExpr>();
        expr->op   = Op::Name;
        expr->name = std::move(name);
        return expr;
    }
    static std::shared_ptr<const ExtentExpr> make(
        Op op, std::shared_ptr<const ExtentExpr> lhs, std::shared_ptr<const ExtentExpr> rhs = {}) {
        auto expr = std::make_shared<ExtentExpr>();
        expr->op  = op;
        expr->lhs = std::move(lhs);
        expr->rhs = std::move(rhs);
        return expr;
    }

    /// The value, or nothing when a name is unbound, a division has a zero divisor or a value
    /// does not fit.
    std::optional<int64_t> evaluate(const ExtentValues &values) const {
        switch (op) {
        case Op::Constant:
            return value;
        case Op::Name: {
            auto it = values.find(name);
            if (it == values.end()) {
                return std::nullopt;
            }
            return it->second;
        }
        case Op::Neg: {
            auto inner = lhs->evaluate(values);
            return inner ? std::optional<int64_t>(-*inner) : std::nullopt;
        }
        case Op::Call: {
            for (const std::string &used : callNames) {
                if (!values.contains(used)) {
                    return std::nullopt;
                }
            }
            const ExtentCallEvaluator &evaluator = extentCallEvaluator();
            return evaluator ? evaluator(call, values) : std::nullopt;
        }
        default:
            break;
        }
        const auto a = lhs->evaluate(values);
        const auto b = rhs->evaluate(values);
        if (!a || !b) {
            return std::nullopt;
        }
        switch (op) {
        case Op::Add:
            return *a + *b;
        case Op::Sub:
            return *a - *b;
        case Op::Mul:
            return *a * *b;
        case Op::Div:
            return *b == 0 ? std::nullopt : std::optional<int64_t>(*a / *b);
        case Op::Mod:
            return *b == 0 ? std::nullopt : std::optional<int64_t>(*a % *b);
        default:
            return std::nullopt;
        }
    }

    void collectNames(std::set<std::string> &out) const {
        if (op == Op::Name) {
            out.insert(name);
        }
        if (op == Op::Call) {
            out.insert(callNames.begin(), callNames.end());
        }
        if (lhs) {
            lhs->collectNames(out);
        }
        if (rhs) {
            rhs->collectNames(out);
        }
    }

    std::string toString() const {
        switch (op) {
        case Op::Constant:
            return std::to_string(value);
        case Op::Name:
            return name;
        case Op::Neg:
            return "-" + lhs->toString();
        case Op::Call:
            return name;
        default:
            break;
        }
        const char *symbol = op == Op::Add   ? " + "
                             : op == Op::Sub ? " - "
                             : op == Op::Mul ? " * "
                             : op == Op::Div ? " / "
                                             : " % ";
        const auto wrap = [](const ExtentExpr &e) {
            const bool simple = e.op == Op::Constant || e.op == Op::Name;
            return simple ? e.toString() : "(" + e.toString() + ")";
        };
        return wrap(*lhs) + symbol + wrap(*rhs);
    }
};

/// One extent of a type: unknown, a constant, a linear form over dimension names, or an integer
/// expression over names that is not linear (`Computed`). A computed extent asks nothing of a
/// shape until the names it uses are bound, as an unknown one does not.
class ExtentForm {
  public:
    enum class Kind : uint8_t { Unknown, Constant, Linear, Computed };

    ExtentForm() = default;

    static ExtentForm unknown() { return ExtentForm(); }
    static ExtentForm constant(int64_t value) {
        ExtentForm form;
        form.kind_     = Kind::Constant;
        form.constant_ = value;
        return form;
    }
    /// `constant + sum(coefficient * name)`. Zero coefficients are dropped and the terms are
    /// kept sorted by name, so two forms over the same names compare equal.
    static ExtentForm linear(
        int64_t constant, std::vector<std::pair<std::string, int64_t>> terms) {
        ExtentForm form;
        form.constant_ = constant;
        // Terms over the same name are added up (`n + n` is `2 * n`); the map keeps them sorted
        // by name, which is the canonical form two extents are compared in.
        std::map<std::string, int64_t> merged;
        for (auto &[name, coefficient] : terms) {
            if (coefficient != 0) {
                merged[std::move(name)] += coefficient;
            }
        }
        for (auto &[name, coefficient] : merged) {
            if (coefficient != 0) {
                form.terms_.emplace_back(name, coefficient);
            }
        }
        form.kind_ = form.terms_.empty() ? Kind::Constant : Kind::Linear;
        return form;
    }
    static ExtentForm linear(std::vector<std::pair<std::string, int64_t>> terms) {
        return linear(0, std::move(terms));
    }

    /// An extent that is the integer expression `expr` over names.
    static ExtentForm computed(std::shared_ptr<const ExtentExpr> expr) {
        ExtentForm form;
        form.kind_ = Kind::Computed;
        form.expr_ = std::move(expr);
        std::set<std::string> names;
        form.expr_->collectNames(names);
        form.names_.assign(names.begin(), names.end());
        return form;
    }

    Kind kind() const { return kind_; }
    bool isUnknown() const { return kind_ == Kind::Unknown; }
    bool isConstant() const { return kind_ == Kind::Constant; }
    bool isComputed() const { return kind_ == Kind::Computed; }
    bool hasNames() const { return !terms_.empty() || !names_.empty(); }
    /// The expression of a computed extent.
    const std::shared_ptr<const ExtentExpr> &expression() const { return expr_; }
    /// Every name the extent uses, linear or computed.
    std::vector<std::string> usedNames() const {
        std::vector<std::string> out;
        for (const auto &[name, coefficient] : terms_) {
            out.push_back(name);
        }
        out.insert(out.end(), names_.begin(), names_.end());
        return out;
    }
    int64_t constant() const { return constant_; }
    const std::vector<std::pair<std::string, int64_t>> &terms() const { return terms_; }

    /// The value of the extent when every name it uses is bound; nullopt otherwise.
    std::optional<int64_t> evaluate(const ExtentValues &values) const {
        if (kind_ == Kind::Unknown) {
            return std::nullopt;
        }
        if (kind_ == Kind::Computed) {
            return expr_->evaluate(values);
        }
        int64_t result = constant_;
        for (const auto &[name, coefficient] : terms_) {
            auto it = values.find(name);
            if (it == values.end()) {
                return std::nullopt;
            }
            result += coefficient * it->second;
        }
        return result;
    }

    /**
     * Adds to `values` what this extent being equal to `value` says about the names it uses.
     * Returns false when it contradicts a value already bound. When more than one name is
     * still unbound the equation cannot be solved here and is left alone (the extent stays
     * undecided rather than guessed).
     */
    bool bind(int64_t value, ExtentValues &values) const {
        if (kind_ == Kind::Unknown || kind_ == Kind::Computed) {
            // A computed extent is not solved for its names; it is checked once they are bound.
            return true;
        }
        if (kind_ == Kind::Constant) {
            return constant_ == value;
        }
        int64_t rest = value - constant_;
        const std::pair<std::string, int64_t> *unbound = nullptr;
        size_t unboundCount                            = 0;
        for (const auto &term : terms_) {
            auto it = values.find(term.first);
            if (it == values.end()) {
                ++unboundCount;
                unbound = &term;
            } else {
                rest -= term.second * it->second;
            }
        }
        if (unboundCount == 0) {
            return rest == 0;
        }
        if (unboundCount == 1) {
            if (rest % unbound->second != 0) {
                return false;
            }
            values[unbound->first] = rest / unbound->second;
        }
        return true;
    }

    bool operator==(const ExtentForm &other) const {
        if (kind_ == Kind::Computed || other.kind_ == Kind::Computed) {
            return kind_ == other.kind_ && expr_->toString() == other.expr_->toString();
        }
        return kind_ == other.kind_ && constant_ == other.constant_ && terms_ == other.terms_;
    }
    bool operator!=(const ExtentForm &other) const { return !(*this == other); }

    /// How the extent is written in a type: `3`, `?`, `n`, `2 * n - 1`.
    std::string toString() const {
        if (kind_ == Kind::Unknown) {
            return "?";
        }
        if (kind_ == Kind::Computed) {
            return expr_->toString();
        }
        if (terms_.empty()) {
            return std::to_string(constant_);
        }
        std::string result;
        for (const auto &[name, coefficient] : terms_) {
            const bool negative = coefficient < 0;
            const int64_t magnitude = negative ? -coefficient : coefficient;
            if (result.empty()) {
                result += negative ? "-" : "";
            } else {
                result += negative ? " - " : " + ";
            }
            if (magnitude != 1) {
                result += std::to_string(magnitude) + " * ";
            }
            result += name;
        }
        if (constant_ > 0) {
            result += " + " + std::to_string(constant_);
        } else if (constant_ < 0) {
            result += " - " + std::to_string(-constant_);
        }
        return result;
    }

  private:
    Kind kind_     = Kind::Unknown;
    int64_t constant_ = 0;
    std::vector<std::pair<std::string, int64_t>> terms_;
    std::shared_ptr<const ExtentExpr> expr_;
    std::vector<std::string> names_;
};

/// The dimension sequences bound to pack names (`T[..rest]`) at one call: name -> extents. The
/// pack of a fixed-rank argument is its dimensions after the parameter's prefix; an empty
/// sequence means the pack stood for no dimensions at all (design principles 5.4).
using ExtentPacks = std::map<std::string, std::vector<ExtentForm>>;

} // namespace camel::core::type