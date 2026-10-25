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
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace camel::core::type {

/// The values bound to dimension names at one call: name -> extent.
using ExtentValues = std::map<std::string, int64_t>;

/// One extent of a type: unknown, a constant, or a linear form over dimension names.
class ExtentForm {
  public:
    enum class Kind : uint8_t { Unknown, Constant, Linear };

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

    Kind kind() const { return kind_; }
    bool isUnknown() const { return kind_ == Kind::Unknown; }
    bool isConstant() const { return kind_ == Kind::Constant; }
    bool hasNames() const { return !terms_.empty(); }
    int64_t constant() const { return constant_; }
    const std::vector<std::pair<std::string, int64_t>> &terms() const { return terms_; }

    /// The value of the extent when every name it uses is bound; nullopt otherwise.
    std::optional<int64_t> evaluate(const ExtentValues &values) const {
        if (kind_ == Kind::Unknown) {
            return std::nullopt;
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
        if (kind_ == Kind::Unknown) {
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
        return kind_ == other.kind_ && constant_ == other.constant_ && terms_ == other.terms_;
    }
    bool operator!=(const ExtentForm &other) const { return !(*this == other); }

    /// How the extent is written in a type: `3`, `?`, `n`, `2 * n - 1`.
    std::string toString() const {
        if (kind_ == Kind::Unknown) {
            return "?";
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
};

/// The dimension sequences bound to pack names (`T[..rest]`) at one call: name -> extents. The
/// pack of a fixed-rank argument is its dimensions after the parameter's prefix; an empty
/// sequence means the pack stood for no dimensions at all (design principles 5.4).
using ExtentPacks = std::map<std::string, std::vector<ExtentForm>>;

} // namespace camel::core::type