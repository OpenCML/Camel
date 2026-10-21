/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The value types of the operator semantics record (see operator_semantics.h): the text forms
 * of keys, permissions, numeric classes and fault kinds. The declarations themselves live in
 * the operator table (operator_table.cpp).
 */

#include "camel/core/operator_semantics.h"

#include <array>
#include <charconv>
#include <format>
#include <system_error>
#include <utility>

namespace camel::core::sem {

namespace {

/// The source names of the nine permission bits, in bit order.
constexpr std::array<std::pair<Permission, std::string_view>, 9> kPermissionNames = {{
    {kReassociate, "reassociate"},
    {kContract, "contract"},
    {kApproxMath, "approx_math"},
    {kChangePrecision, "change_precision"},
    {kNoNans, "no_nans"},
    {kNoInfs, "no_infs"},
    {kNoSignedZeros, "no_signed_zeros"},
    {kFlushSubnormals, "flush_subnormals"},
    {kNonDeterministic, "nondeterministic"},
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
