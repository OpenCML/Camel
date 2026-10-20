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

#include "cpp_types.h"

#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/rtdata/variant.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/enum.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <system_error>

namespace camel::passes::cpp {

using camel::core::type::TypeCode;

bool isVoid(const Type *type) { return type != nullptr && type->code() == TypeCode::Void; }

std::string cppStringLiteral(std::string_view bytes) {
    std::string out = "\"";
    for (unsigned char c : bytes) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c >= 0x20 && c < 0x7F) {
                // '?' twice could start a trigraph before C++17; spelled out anyway.
                if (c == '?') {
                    out += "\\?";
                } else {
                    out.push_back(static_cast<char>(c));
                }
            } else {
                out += std::format("\\{:03o}", c);
            }
            break;
        }
    }
    out += '"';
    return out;
}

namespace {

template <typename T> std::string floatLiteral(T value, const char *typeName, const char *suffix) {
    if (std::isnan(value)) {
        return std::format("std::numeric_limits<{}>::quiet_NaN()", typeName);
    }
    if (std::isinf(value)) {
        return std::format("{}std::numeric_limits<{}>::infinity()", value < 0 ? "-" : "", typeName);
    }
    std::array<char, 64> buffer{};
    const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    std::string text(buffer.data(), ec == std::errc() ? end : buffer.data());
    // Shortest digits may have neither a point nor an exponent ("3"): make it a float literal.
    if (text.find_first_of(".eEn") == std::string::npos) {
        text += ".0";
    }
    return text + suffix;
}

} // namespace

std::string cppFloatLiteral(double value) { return floatLiteral(value, "double", ""); }
std::string cppFloatLiteral(float value) { return floatLiteral(value, "float", "f"); }

std::string cppIntLiteral(int64_t value) {
    if (value == std::numeric_limits<int64_t>::min()) {
        // -9223372036854775808 is a negation of a literal that does not fit.
        return "std::numeric_limits<int64_t>::min()";
    }
    // A bare literal: the helpers of the standalone runtime are overloaded so that an int64
    // operand next to a literal selects the int64 form (camel/standalone/num.h).
    return std::to_string(value);
}

std::string cppIntLiteral(int32_t value) {
    if (value == std::numeric_limits<int32_t>::min()) {
        return "std::numeric_limits<int32_t>::min()";
    }
    return std::to_string(value);
}

std::string cppIdentifier(std::string_view name) {
    static const std::set<std::string, std::less<>> kKeywords = {
        "alignas",
        "alignof",
        "and",
        "and_eq",
        "asm",
        "auto",
        "bitand",
        "bitor",
        "bool",
        "break",
        "case",
        "catch",
        "char",
        "char8_t",
        "char16_t",
        "char32_t",
        "class",
        "compl",
        "concept",
        "const",
        "consteval",
        "constexpr",
        "constinit",
        "const_cast",
        "continue",
        "co_await",
        "co_return",
        "co_yield",
        "decltype",
        "default",
        "delete",
        "do",
        "double",
        "dynamic_cast",
        "else",
        "enum",
        "explicit",
        "export",
        "extern",
        "false",
        "float",
        "for",
        "friend",
        "goto",
        "if",
        "inline",
        "int",
        "long",
        "mutable",
        "namespace",
        "new",
        "noexcept",
        "not",
        "not_eq",
        "nullptr",
        "operator",
        "or",
        "or_eq",
        "private",
        "protected",
        "public",
        "register",
        "reinterpret_cast",
        "requires",
        "return",
        "short",
        "signed",
        "sizeof",
        "static",
        "static_assert",
        "static_cast",
        "struct",
        "switch",
        "template",
        "this",
        "thread_local",
        "throw",
        "true",
        "try",
        "typedef",
        "typeid",
        "typename",
        "union",
        "unsigned",
        "using",
        "virtual",
        "void",
        "volatile",
        "wchar_t",
        "while",
        "xor",
        "xor_eq",
        "main",
        "rt",
        "std",
        "depth",
    };
    std::string out;
    out.reserve(name.size());
    bool lastUnderscore = false;
    for (unsigned char c : name) {
        const bool keep = std::isalnum(c) != 0;
        if (keep) {
            out.push_back(static_cast<char>(c));
            lastUnderscore = false;
        } else if (!lastUnderscore && !out.empty()) {
            out.push_back('_');
            lastUnderscore = true;
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty()) {
        out = "value";
    }
    if (std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    // A double underscore anywhere, or an underscore and a capital at the start, is reserved.
    if (kKeywords.contains(out) || (out.size() > 1 && out[0] == '_' && std::isupper(out[1]))) {
        out += '_';
    }
    return out;
}

std::string NameScope::unique(const std::string &base) {
    if (used_.insert(base).second) {
        return base;
    }
    for (size_t n = 2;; ++n) {
        std::string candidate = std::format("{}_{}", base, n);
        if (used_.insert(candidate).second) {
            return candidate;
        }
    }
}

// ---- Types ----

namespace {

using camel::core::type::ArrayType;
using camel::core::type::FunctionType;
using camel::core::type::StructType;
using camel::core::type::TupleType;

const TupleType *asTuple(const Type *type) {
    return type != nullptr && type->code() == TypeCode::Tuple ? static_cast<const TupleType *>(type)
                                                              : nullptr;
}

} // namespace

std::optional<std::string> ProgramValues::type(const Type *t) {
    if (t == nullptr) {
        return std::nullopt;
    }
    switch (t->code()) {
    case TypeCode::Int32:
        return "int32_t";
    case TypeCode::Int64:
        return "int64_t";
    case TypeCode::Float32:
        return "float";
    case TypeCode::Float64:
        return "double";
    case TypeCode::Bool:
        return "bool";
    case TypeCode::String:
        return "rt::Str";
    case TypeCode::Void:
        return "void";
    default:
        break;
    }
    const std::string key = t->mangle();
    if (auto it = types_.find(key); it != types_.end()) {
        return it->second;
    }
    // A payload of `void` (a `void!`, the value of a branch without else) is kept as rt::Unit.
    auto payload = [&](const Type *p) -> std::optional<std::string> {
        return isVoid(p) ? std::optional<std::string>("rt::Unit") : type(p);
    };

    std::optional<std::string> out;
    if (t->code() == TypeCode::Array) {
        const Type *elem = static_cast<const ArrayType *>(t)->elemType();
        if (auto e = payload(elem)) {
            out = std::format("rt::Array<{}>", *e);
        }
    } else if (const TupleType *tuple = asTuple(t); tuple != nullptr && tuple->isCell()) {
        if (auto v = type(tuple->typeAt(0)); v && !isVoid(tuple->typeAt(0))) {
            out = std::format("rt::Cell<{}>", *v);
        }
    } else if (tuple != nullptr && tuple->isNullable()) {
        if (auto p = payload(tuple->payloadType())) {
            out = std::format("rt::Opt<{}>", *p);
        }
    } else if (tuple != nullptr && tuple->isFallible()) {
        if (auto p = payload(tuple->payloadType())) {
            out = std::format("rt::Fallible<{}>", *p);
        }
    } else if (t->code() == TypeCode::Struct && t == camel::core::type::errorType()) {
        out = "rt::Error";
    } else if (tuple != nullptr || t->code() == TypeCode::Struct) {
        // A struct of the generated file, declared after the types of its members.
        const bool isTuple = tuple != nullptr;
        const auto *record = static_cast<const StructType *>(t);
        const size_t n     = isTuple ? tuple->size() : record->size();
        std::vector<std::pair<std::string, std::string>> members; // type, name
        for (size_t i = 0; i < n; ++i) {
            const Type *m = isTuple ? tuple->typeAt(i) : record->typeAt(i);
            auto mt       = payload(m);
            if (!mt) {
                return std::nullopt;
            }
            members.emplace_back(*mt, fieldName(t, i));
        }
        const std::string name = names_.unique(isTuple ? "Tuple" : "Record");
        std::string decl       = std::format("/// {}\nstruct {} {{\n", t->toString(), name);
        for (const auto &[mt, mn] : members) {
            decl += std::format("    {} {};\n", mt, mn);
        }
        decl += std::format("    bool operator==(const {} &) const = default;\n}};\n", name);
        // Its text, as the runtime prints it: `(1, a)` and `{ x: 1, y: 2.5 }`.
        decl += std::format("inline void appendText(std::string &out, const {} &v) {{\n", name);
        decl += std::format("    out += \"{}\";\n", isTuple ? "(" : "{ ");
        for (size_t i = 0; i < members.size(); ++i) {
            if (i > 0) {
                decl += "    out += \", \";\n";
            }
            if (!isTuple) {
                decl += std::format("    out += \"{}: \";\n", record->fieldName(i));
            }
            decl += std::format("    appendText(out, v.{});\n", members[i].second);
        }
        decl += std::format("    out += \"{}\";\n}}\n\n", isTuple ? ")" : " }");
        declarations_ += decl;
        out = name;
    } else if (t->code() == camel::core::type::EnumType::typeCode()) {
        const auto *enumType = static_cast<const camel::core::type::EnumType *>(t);
        if (enumType->pending() || !enumType->complete()) {
            return std::nullopt; // a prototype, or an instance whose argument is a hole
        }
        const auto &variants         = enumType->variants();
        const std::string &shortName = enumType->decl()->shortName();
        // A name of its own per instance: `Option_int` for `Option<int>`.
        std::string base          = cppIdentifier(shortName);
        const std::string spelled = t->toString();
        if (spelled != shortName) {
            base += "_";
            for (char c : spelled.substr(shortName.size())) {
                base += (std::isalnum(static_cast<unsigned char>(c)) != 0) ? c : '_';
            }
        }
        const std::string name = names_.unique(cppIdentifier(base));
        // The payload of each variant is a struct of its own, nested in the enum's: a
        // tuple variant's fields are `_0`, `_1`, a struct variant's keep their names.
        std::vector<std::string> payloadNames;
        std::string decl = std::format("/// {}\nstruct {} {{\n", spelled, name);
        for (size_t tag = 0; tag < variants.size(); ++tag) {
            const auto &variant       = variants[tag];
            const std::string payload = cppIdentifier(variant.name);
            payloadNames.push_back(payload);
            decl += std::format("    struct {} {{\n", payload);
            for (size_t i = 0; i < variant.fieldCount(); ++i) {
                auto ft = type(variant.fieldTypes[i]);
                if (!ft) {
                    return std::nullopt;
                }
                const std::string field = variant.shape == camel::core::type::VariantShape::Struct
                                              ? cppIdentifier(variant.fieldNames[i])
                                              : std::format("_{}", i);
                decl += std::format("        {} {};\n", *ft, field);
            }
            decl += std::format(
                "        bool operator==(const {} &) const = default;\n    }};\n",
                payload);
        }
        decl += "    int32_t tag = 0;\n    std::variant<std::monostate";
        for (const std::string &payload : payloadNames) {
            decl += ", " + payload;
        }
        decl += "> value;\n";
        decl += std::format("    bool operator==(const {} &) const = default;\n\n", name);
        // Construction: `Shape::make<1>(2.0, 3.0)`. The tag is a constant of the program.
        decl += "    template <std::size_t Tag, typename... Fs>\n    static ";
        decl += std::format("{} make(Fs &&...fs) {{\n        {} out;\n", name, name);
        decl += "        out.tag = static_cast<int32_t>(Tag);\n";
        for (size_t tag = 0; tag < variants.size(); ++tag) {
            decl += std::format(
                "        {}if constexpr (Tag == {}) {{\n            out.value = "
                "{}{{std::forward<Fs>(fs)...}};\n        }}\n",
                tag == 0 ? "" : "else ",
                tag,
                payloadNames[tag]);
        }
        decl += "        return out;\n    }\n";
        // Reading a field: `v.get<1, 0>()`. The tag and the index are constants as well.
        decl += "    template <std::size_t Tag, std::size_t Index>\n    auto get() const {\n";
        size_t branch = 0; // the first field read is the `if`, the others `else if`
        for (size_t tag = 0; tag < variants.size(); ++tag) {
            const auto &variant = variants[tag];
            for (size_t i = 0; i < variant.fieldCount(); ++i) {
                const std::string field = variant.shape == camel::core::type::VariantShape::Struct
                                              ? cppIdentifier(variant.fieldNames[i])
                                              : std::format("_{}", i);
                decl += std::format(
                    "        {}if constexpr (Tag == {} && Index == {}) {{\n            return "
                    "std::get<{}>(value).{};\n        }}\n",
                    branch++ == 0 ? "" : "else ",
                    tag,
                    i,
                    tag + 1,
                    field);
            }
        }
        decl += "    }\n};\n";
        // Its text, as the interpreter prints it: `Shape::Circle(1.0)`,
        // `Shape::Rect { w: 2.0, h: 3.0 }`, `Shape::Empty`.
        decl += std::format("inline void appendText(std::string &out, const {} &v) {{\n", name);
        decl += "    switch (v.tag) {\n";
        for (size_t tag = 0; tag < variants.size(); ++tag) {
            const auto &variant = variants[tag];
            decl += std::format("    case {}:\n", tag);
            decl += std::format("        out += \"{}::{}\";\n", shortName, variant.name);
            if (variant.shape == camel::core::type::VariantShape::Tuple) {
                decl += "        out += \"(\";\n";
            } else if (variant.shape == camel::core::type::VariantShape::Struct) {
                decl += variant.fieldCount() == 0 ? "        out += \" {}\";\n"
                                                  : "        out += \" { \";\n";
            }
            for (size_t i = 0; i < variant.fieldCount(); ++i) {
                if (i > 0) {
                    decl += "        out += \", \";\n";
                }
                const std::string field = variant.shape == camel::core::type::VariantShape::Struct
                                              ? cppIdentifier(variant.fieldNames[i])
                                              : std::format("_{}", i);
                if (variant.shape == camel::core::type::VariantShape::Struct) {
                    decl += std::format("        out += \"{}: \";\n", variant.fieldNames[i]);
                }
                decl += std::format(
                    "        appendText(out, std::get<{}>(v.value).{});\n",
                    tag + 1,
                    field);
            }
            if (variant.shape == camel::core::type::VariantShape::Tuple) {
                decl += "        out += \")\";\n";
            } else if (
                variant.shape == camel::core::type::VariantShape::Struct &&
                variant.fieldCount() > 0) {
                decl += "        out += \" }\";\n";
            }
            decl += "        break;\n";
        }
        decl += "    }\n}\n\n";
        declarations_ += decl;
        out = name;
    } else if (t->code() == TypeCode::Function) {
        const auto *fn = static_cast<const FunctionType *>(t);
        auto result    = type(fn->exitType());
        std::string args;
        bool ok = result.has_value();
        // A `var` parameter takes the variable's cell (design note 5.3).
        auto add = [&](const Type *p, bool isVar) {
            auto pt = type(p);
            ok      = ok && pt.has_value() && !isVoid(p);
            if (ok) {
                const bool cell = isVar && !pt->starts_with("rt::Cell<");
                args += (args.empty() ? "" : ", ") + (cell ? "rt::Cell<" + *pt + ">" : *pt);
            }
        };
        for (size_t i = 0; i < fn->normTypesSpan().size(); ++i) {
            add(fn->normTypesSpan()[i], fn->normIsVarAt(i));
        }
        for (size_t i = 0; i < fn->withTypesSpan().size(); ++i) {
            add(fn->withTypesSpan()[i], fn->withIsVarAt(i));
        }
        if (ok) {
            out = std::format("rt::Fn<{}({})>", *result, args);
        }
    }
    if (out) {
        types_.emplace(key, *out);
    }
    return out;
}

std::string ProgramValues::fieldName(const Type *t, size_t i) const {
    if (t->code() == TypeCode::Struct) {
        return cppIdentifier(static_cast<const StructType *>(t)->fieldName(i));
    }
    return std::format("_{}", i);
}

// ---- Constants ----

std::string ProgramValues::string(std::string_view text) {
    const std::string expr =
        std::format("std::string_view({}, {})", cppStringLiteral(text), text.size());
    // A name that says what the text is, when the text is a short word or two.
    std::string hint;
    for (unsigned char c : text.substr(0, 24)) {
        hint.push_back(std::isalnum(c) != 0 ? static_cast<char>(std::tolower(c)) : '_');
    }
    std::string base = "str";
    if (const std::string word = cppIdentifier(hint);
        hint.find_first_not_of('_') != std::string::npos && word != "value") {
        base += "_" + word;
    }
    return hoist("rt::Str", expr, base);
}

std::string
ProgramValues::hoist(const std::string &type, const std::string &expr, const std::string &hint) {
    const std::string key = type + "\n" + expr;
    if (auto it = constants.find(key); it != constants.end()) {
        return it->second;
    }
    // In the namespace `lit`: a name there cannot be hidden by a parameter or a local.
    const std::string name = constantNames_.unique(hint);
    constants.emplace(key, "lit::" + name);
    constants_ += std::format("const {} {}{{{}}};\n", type, name, expr);
    return "lit::" + name;
}

std::optional<std::string> ProgramValues::constant(slot_t slot, const Type *t) {
    if (t == nullptr) {
        return std::nullopt;
    }
    using camel::core::rtdata::fromSlot;
    switch (t->code()) {
    case TypeCode::Int32:
        return cppIntLiteral(fromSlot<int32_t>(slot));
    case TypeCode::Int64:
        return cppIntLiteral(fromSlot<int64_t>(slot));
    case TypeCode::Float32:
        return cppFloatLiteral(fromSlot<float>(slot));
    case TypeCode::Float64:
        return cppFloatLiteral(fromSlot<double>(slot));
    case TypeCode::Bool:
        return fromSlot<bool>(slot) ? "true" : "false";
    case TypeCode::Void:
        return "rt::Unit{}";
    case TypeCode::String: {
        const auto *str = fromSlot<String *>(slot);
        return string(str != nullptr ? str->view() : std::string_view());
    }
    default:
        break;
    }
    const auto cpp = type(t);
    if (!cpp) {
        return std::nullopt;
    }
    // The elements of a composite constant, each a constant of its own type.
    auto elements = [&](size_t n, auto slotAt, auto typeAt) -> std::optional<std::string> {
        std::string out;
        for (size_t i = 0; i < n; ++i) {
            auto e = constant(slotAt(i), typeAt(i));
            if (!e) {
                return std::nullopt;
            }
            out += (i ? ", " : "") + *e;
        }
        return out;
    };
    if (t->code() == TypeCode::Array) {
        const auto *array = fromSlot<::Array *>(slot);
        const Type *elem  = static_cast<const ArrayType *>(t)->elemType();
        auto items        = elements(
            array != nullptr ? array->size() : 0,
            [&](size_t i) { return array->get<slot_t>(i); },
            [&](size_t) { return elem; });
        return items ? std::optional(hoist(*cpp, *items, "array")) : std::nullopt;
    }
    if (const TupleType *tuple = asTuple(t); tuple != nullptr && tuple->isNullable()) {
        if (camel::core::rtdata::isNullValue(slot)) {
            return std::format("{}{{}}", *cpp);
        }
        auto payload = constant(camel::core::rtdata::nullablePayload(slot), tuple->payloadType());
        return payload ? std::optional(std::format("{}({})", *cpp, *payload)) : std::nullopt;
    } else if (tuple != nullptr && !tuple->isTagged() && !tuple->isCell()) {
        const auto *value = fromSlot<::Tuple *>(slot);
        if (value == nullptr) {
            return std::nullopt; // a place a construction fills in (FILL)
        }
        auto items = elements(
            tuple->size(),
            [&](size_t i) { return value->get<slot_t>(i); },
            [&](size_t i) { return tuple->typeAt(i); });
        return items ? std::optional(hoist(*cpp, *items, "tuple")) : std::nullopt;
    }
    if (t->code() == TypeCode::Struct) {
        const auto *record = static_cast<const StructType *>(t);
        const auto *value  = fromSlot<::Struct *>(slot);
        if (value == nullptr) {
            return std::nullopt;
        }
        auto items = elements(
            record->size(),
            [&](size_t i) { return value->get<slot_t>(i); },
            [&](size_t i) { return record->typeAt(i); });
        return items ? std::optional(hoist(*cpp, *items, "record")) : std::nullopt;
    }
    if (t->code() == camel::core::type::EnumType::typeCode()) {
        const auto *enumType = static_cast<const camel::core::type::EnumType *>(t);
        const auto *value    = fromSlot<camel::core::rtdata::Variant *>(slot);
        if (value == nullptr || enumType->pending() || value->tag() >= enumType->variantCount()) {
            return std::nullopt;
        }
        std::string fields;
        for (size_t i = 0; i < value->fieldCount(); ++i) {
            auto f = constant(value->get(i), enumType->fieldType(value->tag(), i));
            if (!f) {
                return std::nullopt;
            }
            fields += (i ? ", " : "") + *f;
        }
        return hoist(*cpp, std::format("{}::make<{}>({})", *cpp, value->tag(), fields), "enum");
    }
    return std::nullopt;
}

} // namespace camel::passes::cpp
