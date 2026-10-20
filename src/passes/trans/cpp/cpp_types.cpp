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
#include "camel/core/rtdata/complex.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/rtdata/variant.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/enum.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <system_error>
#include <unordered_set>
#include <vector>

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

/// Whether `root` is `owner` or contains it through the fields of a composite or the variants
/// of an enum. A recursive enum closes the graph, so the walk keeps a visited set; it is an
/// explicit stack because a type's nesting is not bounded by anything but the source.
bool containsEnumType(const Type *root, const Type *owner) {
    if (root == nullptr) {
        return false;
    }
    std::vector<const Type *> work{root};
    std::unordered_set<const Type *> seen;
    while (!work.empty()) {
        const Type *type = work.back();
        work.pop_back();
        if (type == nullptr) {
            continue;
        }
        if (type == owner) {
            return true;
        }
        if (!seen.insert(type).second) {
            continue;
        }
        switch (type->code()) {
        case TypeCode::Array:
            work.push_back(static_cast<const ArrayType *>(type)->elemType());
            break;
        case TypeCode::Tuple:
            for (const Type *member : static_cast<const TupleType *>(type)->types()) {
                work.push_back(member);
            }
            break;
        case TypeCode::Struct:
            for (const Type *member : static_cast<const StructType *>(type)->types()) {
                work.push_back(member);
            }
            break;
        case TypeCode::Function: {
            const auto *fn = static_cast<const FunctionType *>(type);
            for (const Type *member : fn->withTypesSpan()) {
                work.push_back(member);
            }
            for (const Type *member : fn->normTypesSpan()) {
                work.push_back(member);
            }
            if (fn->hasExitType()) {
                work.push_back(fn->exitType());
            }
            break;
        }
        default:
            break;
        }
        if (const auto *coll = camel::core::type::asCollectionTypeOrPrototype(type)) {
            for (const Type *member : coll->paramsSpan()) {
                work.push_back(member);
            }
        }
        if (const auto *enumType =
                camel::core::type::asEnumTypeOrPrototype(const_cast<Type *>(type))) {
            for (const auto &variant : enumType->variants()) {
                for (const Type *member : variant.fieldTypes) {
                    work.push_back(member);
                }
            }
        }
    }
    return false;
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
    // A complex number is std::complex of its parts' width (include/camel/standalone/complex.h).
    case TypeCode::Complex64:
        return "std::complex<float>";
    case TypeCode::Complex128:
        return "std::complex<double>";
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
        // The name is registered before the fields are walked: a recursive enum's field names
        // the enum itself, and that lookup must find the name being declared (see text.h).
        types_.emplace(key, name);
        // The payload of each variant is a struct of its own, nested in the enum's: a tuple
        // variant's fields are `_0`, `_1`, a struct variant's keep their names. A field whose
        // type contains this enum cannot be held by value (the type would be infinite); it is
        // held through `std::shared_ptr`, the one indirection C++ needs for a recursive type.
        struct FieldPlan {
            bool boxed = false;
            bool direct = false; // the field is the enum itself: print/compare iteratively
            std::string cppType;
            std::string field;
            const Type *type = nullptr;
        };
        std::vector<std::vector<FieldPlan>> plans;
        bool anyBoxed  = false;
        bool anyDirect = false;
        for (size_t tag = 0; tag < variants.size(); ++tag) {
            const auto &variant = variants[tag];
            plans.emplace_back();
            for (size_t i = 0; i < variant.fieldCount(); ++i) {
                const Type *fieldType = variant.fieldTypes[i];
                auto ft               = type(fieldType);
                if (!ft) {
                    return std::nullopt;
                }
                FieldPlan plan;
                plan.direct = fieldType == enumType;
                plan.boxed  = plan.direct || containsEnumType(fieldType, enumType);
                plan.cppType = plan.boxed ? std::format("std::shared_ptr<{}>", *ft) : *ft;
                plan.field   = variant.shape == camel::core::type::VariantShape::Struct
                                   ? cppIdentifier(variant.fieldNames[i])
                                   : std::format("_{}", i);
                plan.type    = fieldType;
                anyBoxed |= plan.boxed;
                anyDirect |= plan.direct;
                plans.back().push_back(std::move(plan));
            }
        }
        std::vector<std::string> payloadNames;
        std::string decl = std::format("/// {}\nstruct {} {{\n", spelled, name);
        for (size_t tag = 0; tag < variants.size(); ++tag) {
            const auto &variant       = variants[tag];
            const std::string payload = cppIdentifier(variant.name);
            payloadNames.push_back(payload);
            bool payloadBoxed = false;
            for (const FieldPlan &plan : plans[tag]) {
                payloadBoxed |= plan.boxed;
            }
            decl += std::format("    struct {} {{\n", payload);
            for (const FieldPlan &plan : plans[tag]) {
                decl += std::format("        {} {};\n", plan.cppType, plan.field);
            }
            // A boxed member may hold a type that is only complete once the enum is; its
            // defaulted `==` would instantiate that type where the payload is defined. The
            // enum's own operator== compares boxed members itself.
            if (!payloadBoxed) {
                decl += std::format(
                    "        bool operator==(const {} &) const = default;\n", payload);
            }
            decl += "    };\n";
        }
        decl += "    int32_t tag = 0;\n    std::variant<std::monostate";
        for (const std::string &payload : payloadNames) {
            decl += ", " + payload;
        }
        decl += "> value;\n";
        if (!anyBoxed) {
            decl += std::format("    bool operator==(const {} &) const = default;\n", name);
        }
        // Construction: `Shape::make<1>(2.0, 3.0)`. The tag is a constant of the program.
        decl += "    template <std::size_t Tag, typename... Fs>\n    static ";
        decl += std::format("{} make(Fs &&...fs) {{\n", name);
        decl += "        auto args = std::make_tuple(std::forward<Fs>(fs)...);\n";
        decl += std::format("        {} out;\n", name);
        decl += "        out.tag = static_cast<int32_t>(Tag);\n";
        for (size_t tag = 0; tag < variants.size(); ++tag) {
            const auto &variant = variants[tag];
            std::string init = payloadNames[tag] + "{";
            for (size_t i = 0; i < plans[tag].size(); ++i) {
                if (i > 0) {
                    init += ", ";
                }
                const FieldPlan &plan = plans[tag][i];
                if (plan.boxed) {
                    const std::string boxed = plan.cppType.substr(
                        std::string("std::shared_ptr<").size(),
                        plan.cppType.size() - std::string("std::shared_ptr<").size() - 1);
                    init += std::format("rt::box<{}>(std::get<{}>(args))", boxed, i);
                } else {
                    init += std::format("std::get<{}>(args)", i);
                }
            }
            init += "}";
            decl += std::format(
                "        {}if constexpr (Tag == {}) {{\n            out.value = "
                "{};\n        }}\n",
                tag == 0 ? "" : "else ",
                tag,
                init);
        }
        decl += "        return out;\n    }\n";
        // Reading a field: `v.get<1, 0>()`. The tag and the index are constants as well; a
        // boxed field is given back by value, as the graph's `:enum/field` gives it.
        decl += "    template <std::size_t Tag, std::size_t Index>\n    auto get() const {\n";
        size_t branch = 0; // the first field read is the `if`, the others `else if`
        for (size_t tag = 0; tag < variants.size(); ++tag) {
            for (size_t i = 0; i < variants[tag].fieldCount(); ++i) {
                const FieldPlan &plan = plans[tag][i];
                const std::string access =
                    std::format("std::get<{}>(value).{}", tag + 1, plan.field);
                const std::string result = plan.boxed ? "*" + access : access;
                decl += std::format(
                    "        {}if constexpr (Tag == {} && Index == {}) {{\n            return "
                    "{};\n        }}\n",
                    branch++ == 0 ? "" : "else ",
                    tag,
                    i,
                    result);
            }
        }
        decl += "    }\n";
        // A recursive value frees its children without native recursion: the destructor hands
        // each boxed child to the runtime's pending list and the outermost destructor drains it
        // (camel/standalone/recursive.h).
        if (anyBoxed) {
            decl += "    ~" + name + "() {\n        const bool outermost = rt::beginDestroy();\n";
            decl += "        switch (tag) {\n";
            for (size_t tag = 0; tag < variants.size(); ++tag) {
                const bool hasBoxed = std::any_of(
                    plans[tag].begin(), plans[tag].end(), [](const FieldPlan &plan) {
                        return plan.boxed;
                    });
                if (!hasBoxed) {
                    continue;
                }
                decl += std::format("        case {}:\n", tag);
                for (const FieldPlan &plan : plans[tag]) {
                    if (plan.boxed) {
                        decl += std::format(
                            "            rt::boxToStack(std::get<{}>(value).{});\n",
                            tag + 1,
                            plan.field);
                    }
                }
                decl += "            break;\n";
            }
            decl += "        }\n        if (outermost) {\n            rt::drainDestroyed();\n";
            decl += "        }\n        rt::endDestroy();\n    }\n";
        }
        // Equality by content: a boxed child is compared through the pointer, and a chain of
        // them is compared on an explicit stack, so a very long chain does not recurse.
        if (anyBoxed) {
            decl += std::format("    bool operator==(const {} &other) const {{\n", name);
            decl += "        std::vector<std::pair<const ";
            decl += name + " *, const " + name + " *>> pending{{this, &other}};\n";
            decl += "        while (!pending.empty()) {\n";
            decl += "            const auto [a, b] = pending.back();\n";
            decl += "            pending.pop_back();\n";
            decl += "            if (a == b) continue;\n";
            decl += "            if (a->tag != b->tag) return false;\n";
            decl += "            switch (a->tag) {\n";
            for (size_t tag = 0; tag < variants.size(); ++tag) {
                if (plans[tag].empty()) {
                    continue;
                }
                decl += std::format("            case {}: {{\n", tag);
                for (const FieldPlan &plan : plans[tag]) {
                    const std::string left =
                        std::format("std::get<{}>(a->value).{}", tag + 1, plan.field);
                    const std::string right =
                        std::format("std::get<{}>(b->value).{}", tag + 1, plan.field);
                    if (plan.direct) {
                        decl += "                {\n";
                        decl += std::format("                    auto &x = {};\n", left);
                        decl += std::format("                    auto &y = {};\n", right);
                        decl += "                    if (!x || !y) {\n";
                        decl += "                        if (x != y) return false;\n";
                        decl += "                    } else {\n";
                        decl += "                        pending.emplace_back(x.get(), y.get());\n";
                        decl += "                    }\n";
                        decl += "                }\n";
                    } else if (plan.boxed) {
                        decl += std::format(
                            "                if (!rt::boxedEqual({}, {})) return false;\n",
                            left,
                            right);
                    } else {
                        decl += std::format(
                            "                if (!({} == {})) return false;\n", left, right);
                    }
                }
                decl += "                break;\n            }\n";
            }
            decl += "        }\n        }\n        return true;\n    }\n";
        }
        decl += "};\n";
        // Its text, as the interpreter prints it: `Shape::Circle(1.0)`,
        // `Shape::Rect { w: 2.0, h: 3.0 }`, `Shape::Empty`. A direct recursive field is printed
        // from an explicit work list, so a long chain does not recurse.
        decl += std::format("inline void appendText(std::string &out, const {} &root) {{\n", name);
        if (anyDirect) {
            decl += "    std::vector<std::function<void()>> todo;\n";
            decl += std::format("    std::function<void(const {} &)> emit = [&](const {} &v) {{\n",
                                name, name);
            decl += "        switch (v.tag) {\n";
            for (size_t tag = 0; tag < variants.size(); ++tag) {
                const auto &variant = variants[tag];
                decl += std::format("        case {}: {{\n", tag);
                std::string prefix = std::format("{}::{}", shortName, variant.name);
                if (variant.shape == camel::core::type::VariantShape::Tuple) {
                    prefix += "(";
                } else if (variant.shape == camel::core::type::VariantShape::Struct) {
                    prefix += variant.fieldCount() == 0 ? " {}" : " { ";
                }
                decl += std::format("            out += \"{}\";\n", prefix);
                std::string suffix;
                if (variant.shape == camel::core::type::VariantShape::Tuple) {
                    suffix = ")";
                } else if (
                    variant.shape == camel::core::type::VariantShape::Struct &&
                    variant.fieldCount() > 0) {
                    suffix = " }";
                }
                if (!suffix.empty()) {
                    decl += std::format(
                        "            todo.push_back([&out] {{ out += \"{}\"; }});\n", suffix);
                }
                for (size_t i = variant.fieldCount(); i-- > 0;) {
                    const FieldPlan &plan = plans[tag][i];
                    std::string lead;
                    if (i > 0) {
                        lead = ", ";
                    }
                    if (variant.shape == camel::core::type::VariantShape::Struct) {
                        lead += std::string(variant.fieldNames[i]) + ": ";
                    }
                    const std::string access =
                        std::format("std::get<{}>(v.value).{}", tag + 1, plan.field);
                    if (plan.direct) {
                        decl += std::format(
                            "            todo.push_back([&out, &emit, p = {}.get()] "
                            "{{ out += \"{}\"; if (p) {{ emit(*p); }} else {{ out += \"null\"; }} "
                            "}});\n",
                            access,
                            lead);
                    } else if (plan.boxed) {
                        decl += std::format(
                            "            todo.push_back([&out, &v] {{ out += \"{}\"; appendText(out, "
                            "*{}); }});\n",
                            lead,
                            access);
                    } else {
                        decl += std::format(
                            "            todo.push_back([&out, &v] {{ out += \"{}\"; appendText(out, "
                            "{}); }});\n",
                            lead,
                            access);
                    }
                }
                decl += "            break;\n        }\n";
            }
            decl += "        }\n    };\n";
            decl += "    emit(root);\n";
            decl += "    while (!todo.empty()) {\n";
            decl += "        auto action = std::move(todo.back());\n";
            decl += "        todo.pop_back();\n";
            decl += "        action();\n";
            decl += "    }\n";
        } else {
            decl += "    switch (root.tag) {\n";
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
                    const FieldPlan &plan = plans[tag][i];
                    if (variant.shape == camel::core::type::VariantShape::Struct) {
                        decl += std::format("        out += \"{}: \";\n", variant.fieldNames[i]);
                    }
                    const std::string access =
                        std::format("std::get<{}>(root.value).{}", tag + 1, plan.field);
                    decl += std::format(
                        "        appendText(out, {});\n", plan.boxed ? "*" + access : access);
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
            decl += "    }\n";
        }
        decl += "}\n\n";
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
    // A complex constant is a std::complex of the two parts' literals (the shortest round-trip
    // digits; the c64 parts are exactly f32 values).
    case TypeCode::Complex64: {
        const auto *value = fromSlot<camel::core::rtdata::Complex *>(slot);
        return std::format(
            "std::complex<float>{{{}, {}}}",
            cppFloatLiteral(value != nullptr ? value->realF() : 0.0f),
            cppFloatLiteral(value != nullptr ? value->imaginaryF() : 0.0f));
    }
    case TypeCode::Complex128: {
        const auto *value = fromSlot<camel::core::rtdata::Complex *>(slot);
        return std::format(
            "std::complex<double>{{{}, {}}}",
            cppFloatLiteral(value != nullptr ? value->real() : 0.0),
            cppFloatLiteral(value != nullptr ? value->imaginary() : 0.0));
    }
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
