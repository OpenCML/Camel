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
 * Author: Zhenjie Wei
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * How the values of a Camel program are written in the C++ that std::cpp generates: the C++
 * type of each Camel type, the C++ text of a constant, and the identifiers the generated code
 * uses (docs/design/cpp-standalone/README.cn.md, section 4).
 *
 * Within this lane the translated types grow stage by stage; a type without a translation here
 * makes the pass refuse the program at the construct that has it.
 */

#pragma once

#include "camel/core/slot.h"
#include "camel/core/type/base.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace camel::passes::cpp {

using camel::core::type::Type;

/// The C++ type of a value of `type`, or nothing when std::cpp does not translate it yet.
/// `void` is "void": the value of a node of that type is not kept.
std::optional<std::string> cppTypeOf(const Type *type);

bool isVoid(const Type *type);

/// What the refusals say std::cpp translates.
inline constexpr const char *kTranslatedTypes =
    "std::cpp translates values of the types int32, int64, float32, float64, bool, string and "
    "void only";

/// A C++ string literal of `bytes`: printable ASCII as it is, everything else as an octal
/// escape (an octal escape takes at most three digits, so a digit after it stays a digit).
std::string cppStringLiteral(std::string_view bytes);

/// A C++ literal of a float that reads back as the same number: the shortest round-trip digits
/// (the compiler's conversion of a decimal literal is correctly rounded). Infinities and NaN are
/// written with std::numeric_limits.
std::string cppFloatLiteral(double value);
std::string cppFloatLiteral(float value);
std::string cppIntLiteral(int64_t value);
std::string cppIntLiteral(int32_t value);

/// An identifier for the C++ text from a Camel name or a hint: letters, digits and '_' only,
/// not starting with a digit, not a C++ keyword. Never empty.
std::string cppIdentifier(std::string_view name);

/// Names that are unique in one scope of the generated file.
class NameScope {
  public:
    /// `base`, or `base_2`, `base_3`, ... if it is taken.
    std::string unique(const std::string &base);
    void reserve(const std::string &name) { used_.insert(name); }

  private:
    std::set<std::string> used_;
};

/// The string constants of a program: each distinct text becomes one namespace-scope
/// `const rt::Str`, built once when the program starts.
class StringPool {
  public:
    /// The name of the constant holding `text`.
    std::string nameFor(std::string_view text);
    /// The definitions, in the order the texts were first asked for.
    std::string definitions() const;
    bool empty() const { return order_.empty(); }

  private:
    std::map<std::string, std::string, std::less<>> names_;
    std::vector<std::pair<std::string, std::string>> order_; // name, text
    NameScope scope_;
};

/// The C++ text of the constant in `slot` of type `type`, or nothing when it has none (yet).
std::optional<std::string> cppConstant(slot_t slot, const Type *type, StringPool &strings);

} // namespace camel::passes::cpp
