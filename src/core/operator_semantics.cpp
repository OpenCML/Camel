// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The value types of the operator semantics record (see operator_semantics.h): the text forms
 * of keys, permissions, numeric classes and fault kinds. The declarations themselves live in
 * the operator table (operator_table.cpp).
 */

#include "camel/core/operator_semantics.h"

#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <system_error>
#include <utility>

namespace camel::core::sem {

namespace {

/// The source names of the ten permission bits, in bit order.
constexpr std::array<std::pair<Permission, std::string_view>, 10> kPermissionNames = {{
    {kReassociate, "reassociate"},
    {kContract, "contract"},
    {kApproxMath, "approx_math"},
    {kChangePrecision, "change_precision"},
    {kNoNans, "no_nans"},
    {kNoInfs, "no_infs"},
    {kNoSignedZeros, "no_signed_zeros"},
    {kFlushSubnormals, "flush_subnormals"},
    {kNonDeterministic, "nondeterministic"},
    {kRefineAccuracy, "refine_accuracy"},
}};

/// Parses a decimal uint16_t that is the whole of `text`.
std::optional<uint16_t> parseVersionPart(std::string_view text) {
    uint16_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || ec != std::errc() || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

// ---- OperatorKey ---------------------------------------------------------------------------

std::string OperatorKey::uri() const {
    return std::format("{}:{}", symbolText(protocol), symbolText(name));
}

std::string OperatorKey::toString() const { return std::format("{}@{}.{}", uri(), major, minor); }

OperatorKey OperatorKey::of(std::string_view uri, uint16_t major, uint16_t minor) {
    // The protocol is everything before the first ':'; a URI without one has an empty
    // protocol and is all name.
    const size_t colon     = uri.find(':');
    const std::string_view protocol = colon == std::string_view::npos ? "" : uri.substr(0, colon);
    const std::string_view name     = colon == std::string_view::npos ? uri : uri.substr(colon + 1);
    return OperatorKey{internSymbol(protocol), internSymbol(name), major, minor};
}

std::optional<OperatorKey> OperatorKey::parse(std::string_view text) {
    const size_t at = text.rfind('@');
    if (at == std::string_view::npos || text.find(':') == std::string_view::npos ||
        text.find(':') > at) {
        return std::nullopt;
    }
    const std::string_view version = text.substr(at + 1);
    const size_t dot               = version.find('.');
    if (dot == std::string_view::npos) {
        return std::nullopt;
    }
    const auto major = parseVersionPart(version.substr(0, dot));
    const auto minor = parseVersionPart(version.substr(dot + 1));
    if (!major || !minor) {
        return std::nullopt;
    }
    return of(text.substr(0, at), *major, *minor);
}

// ---- Permissions ---------------------------------------------------------------------------

std::string_view permissionName(Permission permission) {
    for (const auto &[bit, name] : kPermissionNames) {
        if (bit == permission) {
            return name;
        }
    }
    return "";
}

std::optional<PermissionMask> parsePermission(std::string_view name) {
    if (name == "finite_math") {
        return kFiniteMath;
    }
    for (const auto &[bit, text] : kPermissionNames) {
        if (text == name) {
            return static_cast<PermissionMask>(bit);
        }
    }
    return std::nullopt;
}

std::string permissionsText(PermissionMask mask) {
    std::string out;
    auto add = [&](std::string_view name) {
        out += out.empty() ? "" : ", ";
        out += name;
    };
    const bool finite = (mask & kFiniteMath) == kFiniteMath;
    for (const auto &[bit, name] : kPermissionNames) {
        if ((mask & bit) == 0 || (finite && (bit & kFiniteMath) != 0)) {
            continue;
        }
        add(name);
    }
    if (finite) {
        add("finite_math");
    }
    return out;
}

// ---- The numeric contract of a rewrite -----------------------------------------------------

double stepUlps(const NumericSemantics &numeric) {
    switch (numeric.cls) {
    case NumericClass::Exact:
        return 0.0;
    case NumericClass::Bounded:
        // The declaration promises `ulpBound`; a bound below one ulp is a rounding step at least.
        return numeric.ulpBound >= 1.0f ? static_cast<double>(numeric.ulpBound) : 1.0;
    case NumericClass::CorrectlyRounded:
    case NumericClass::Opaque:
        break;
    }
    // An undeclared operator is read conservatively: one rounding step.
    return 1.0;
}

RewriteErrorBound rewriteErrorBound(PermissionMask needs, size_t steps, double declaredUlps) {
    // `refine_accuracy` is not a rounding bound: the check of such a rule compares the two
    // sides' errors against a higher-precision reference, sample by sample (rewrite 8.10,
    // modules/rewrite/module.cpp). Its `declaredUlps` is still reported below, so a caller that
    // only asks for a syntactic bound sees the operators' declarations.
    if (needs == kNoPermissions) {
        // Bit for bit: the check compares the bits (a NaN is a NaN).
        return {};
    }
    RewriteErrorBound bound;
    bound.ulps = declaredUlps;
    const double stepCount = static_cast<double>(steps) + 1.0;
    if ((needs & kReassociate) != 0) {
        // The regrouped roundings may each land one ulp away from the written ones.
        bound.ulps += stepCount;
    }
    if ((needs & kContract) != 0) {
        // A contraction removes a rounding; it never adds error beyond a step's declaration.
    }
    if ((needs & kChangePrecision) != 0) {
        // Design principles 7.3: bf16 is the least precision the permission admits (8 mantissa
        // bits), and each of the rule's steps may round there.
        bound.relative += std::ldexp(1.0, -8) * stepCount;
    }
    if ((needs & kNonDeterministic) != 0) {
        // Every schedule is allowed, so there is no number to compare against.
        bound.checkable = false;
    }
    return bound;
}

// ---- Numeric classes and faults ------------------------------------------------------------

std::string_view numericClassName(NumericClass cls) {
    switch (cls) {
    case NumericClass::Exact:
        return "exact";
    case NumericClass::CorrectlyRounded:
        return "correctly_rounded";
    case NumericClass::Bounded:
        return "bounded";
    case NumericClass::Opaque:
        break;
    }
    return "opaque";
}

std::string_view faultKindName(FaultKind kind) {
    switch (kind) {
    case FaultKind::DivisionByZero:
        return "division_by_zero";
    case FaultKind::IndexOutOfRange:
        return "index_out_of_range";
    case FaultKind::EmptyReduction:
        return "empty_reduction";
    case FaultKind::ConversionRange:
        return "conversion_range";
    case FaultKind::ShapeMismatch:
        return "shape_mismatch";
    case FaultKind::Other:
        break;
    }
    return "other";
}

} // namespace camel::core::sem
