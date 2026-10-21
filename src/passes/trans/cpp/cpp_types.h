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
 * Updated: Oct. 20, 2026
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

bool isVoid(const Type *type);

/// What the refusals say std::cpp translates.
inline constexpr const char *kTranslatedTypes =
    "std::cpp translates numbers (real and complex), bool, string, enums, arrays, tuples, structs, T?, T!, "
    "functions and var variables of these, and void";

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

/// The types and constants of a program as the generated file writes them: the C++ type of
/// each Camel type (declaring a struct for each tuple and struct type), and the C++ text of
/// each constant (a string or a composite constant is one namespace-scope `const`, built once
/// when the program starts; copying it counts a reference).
class ProgramValues {
  public:
    /// The C++ type of a value of `type`, or nothing when std::cpp does not translate it yet.
    /// `void` is "void": the value of a node of that type is not kept.
    std::optional<std::string> type(const Type *type);

    /// The C++ representation of values of `type`: for a struct type, the one struct that all
    /// types of its layout share, whichever declaration or literal wrote it (design principles
    /// 5.2: a named type is its declaration, but a value of the same layout enters it without
    /// moving, so a CAST between them is a no-op); otherwise the C++ type itself.
    std::optional<std::string> representation(const Type *type);

    /// Whether a CAST from one type to the other moves nothing: the same C++ representation.
    bool sameRepresentation(const Type *a, const Type *b);

    /// The C++ name of element `i` of a tuple (`_0`, `_1`, ...) or field `i` of a struct type.
    std::string fieldName(const Type *type, size_t i) const;

    /// The C++ text of the constant in `slot` of type `type`, or nothing when it has none.
    std::optional<std::string> constant(slot_t slot, const Type *type);

    /// The name of the constant holding the string `text`.
    std::string string(std::string_view text);

    /// Keeps `name` (a function of the program) from being the name of a declared type.
    void reserve(const std::string &name) { names_.reserve(name); }

    /// The struct declarations, each after the ones it uses.
    std::string typeDeclarations() const { return declarations_; }
    /// The constants, each after the ones it uses; they belong in a namespace `lit`.
    std::string constantDefinitions() const { return constants_; }

  private:
    std::string hoist(const std::string &type, const std::string &expr, const std::string &hint);

    std::map<std::string, std::string> types_;           // the type's mangled name -> C++ type
    std::map<std::string, std::string> layouts_;         // a struct layout -> its one C++ struct
    std::map<std::string, std::string> representations_; // a type -> the struct it denotes
    std::map<std::string, std::string> constants;        // C++ expression -> name of the constant
    std::string declarations_;
    std::string constants_;
    NameScope names_;         // the declared types
    NameScope constantNames_; // the constants, in namespace lit
};

} // namespace camel::passes::cpp
