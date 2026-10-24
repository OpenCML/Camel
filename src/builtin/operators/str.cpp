// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "str.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/complex.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/type.h"

#include "complex_format.h"
#include "float_format.h"
#include "fmt/args.h"
#include "fmt/core.h"
#include "text_format.h"

#include <sstream>
#include <string>
#include <vector>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;

/// The type whose own text a format specification formats when it is used on a numeric array:
/// the array's element type (the element type follows the value: a declaration that fixes none,
/// the numeric-unknown array, leaves the elements' type to the value the call holds). `nullptr`
/// when there is none to format with, so the caller keeps its own refusal.
Type *formatElementTypeOf(Type *type, const Array *value) {
    if (type->code() != TypeCode::Array) {
        return nullptr;
    }
    Type *declared = tt::as_ptr<ArrayType>(type)->elemType();
    if (declared != nullptr && declared->code() != TypeCode::Void) {
        return declared;
    }
    if (value == nullptr || value->elemType() == nullptr) {
        return nullptr;
    }
    return const_cast<Type *>(value->elemType());
}

/// Whether an element type is one a numeric specification applies to on its own.
bool isNumericElementType(const Type *elemType) {
    if (elemType == nullptr) {
        return false;
    }
    switch (elemType->code()) {
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::BFloat16:
    case TypeCode::Float16:
        return true;
    default:
        return false;
    }
}

/// The specification of each `{}` of `pattern`, in the order the placeholders are written; a
/// placeholder without a specification contributes the empty string. A `{{` is a literal brace
/// and takes no argument, and a nested field name (`{0.x}`) keeps the text before the colon.
std::vector<std::string> placeholderSpecs(std::string_view pattern) {
    std::vector<std::string> specs;
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{') {
            if (i + 1 < pattern.size() && pattern[i + 1] == '{') {
                ++i;
                continue;
            }
            size_t j      = i + 1;
            size_t colon  = std::string_view::npos;
            while (j < pattern.size() && pattern[j] != '}') {
                if (pattern[j] == ':' && colon == std::string_view::npos) {
                    colon = j;
                }
                ++j;
            }
            specs.emplace_back(
                colon == std::string_view::npos ? std::string_view{}
                                                : pattern.substr(colon + 1, j - colon - 1));
            i = j;
        } else if (pattern[i] == '}' && i + 1 < pattern.size() && pattern[i + 1] == '}') {
            ++i;
        }
    }
    return specs;
}

/// One element under `spec`, as the language writes a number with a specification: the element's
/// own type formats it (f32, f64, bf16 and f16 each have the width their text is rounded at), and
/// a type with no number text of its own keeps the text it always prints.
std::string formatElementText(Type *elemType, slot_t slot, std::string_view spec) {
    // The pattern is built at run time, so it is a format string the checker cannot read
    // (`fmt::runtime`), exactly as the argument of `format` itself is.
    std::string pattern = "{:";
    pattern += spec;
    pattern += "}";
    const auto format = fmt::runtime(fmt::string_view(pattern.data(), pattern.size()));
    try {
        switch (elemType->code()) {
        case TypeCode::Int32:
            return fmt::format(format, fromSlot<Int32>(slot));
        case TypeCode::Int64:
            return fmt::format(format, fromSlot<Int64>(slot));
        case TypeCode::Float32:
            return fmt::format(
                format, camel::builtin::LanguageFloat<Float32>{fromSlot<Float32>(slot)});
        case TypeCode::Float64:
            return fmt::format(
                format,
                camel::builtin::LanguageFloat<Float64>{
                    fromSlot<Float64>(slot), camel::builtin::FloatTextFormat::Float64});
        case TypeCode::BFloat16:
            return fmt::format(
                format,
                camel::builtin::LanguageFloat<Float32>{
                    halfValueFloat(slot, elemType->code()),
                    camel::builtin::FloatTextFormat::BFloat16});
        case TypeCode::Float16:
            return fmt::format(
                format,
                camel::builtin::LanguageFloat<Float32>{
                    halfValueFloat(slot, elemType->code()),
                    camel::builtin::FloatTextFormat::Float16});
        default:
            break;
        }
    } catch (const fmt::format_error &) {
        // The base text cannot take this specification (a bool, a string): write it as it always
        // prints. A specification the formatter really refuses is reported by the caller's own
        // check of the pattern against the element type.
    }
    std::ostringstream plain;
    printSlot(plain, slot, elemType);
    return plain.str();
}

/// The text of a numeric array with `spec` applied to each element -- `'{:.4f}'->format<xs>` is
/// `[0.7692, 0.7808]`: the specification acts on every element on its own (the language
/// reference's `format` section), while the brackets and the `, ` between elements are the
/// array's own form. Nested arrays keep one bracket level per dimension.
void formatArrayWithSpec(
    std::ostream &os, const Array *array, Type *elemType, std::string_view spec, size_t dim,
    uint64_t base) {
    const size_t rank    = array->rank();
    const int64_t extent = array->dim(dim);
    uint64_t step        = 1;
    for (size_t d = dim + 1; d < rank; ++d) {
        step *= static_cast<uint64_t>(array->dim(d));
    }
    os << "[";
    for (int64_t i = 0; i < extent; ++i) {
        if (i > 0) {
            os << ", ";
        }
        const uint64_t at = base + static_cast<uint64_t>(i) * step;
        if (dim + 1 == rank) {
            os << formatElementText(elemType, array->getSlot(at), spec);
        } else {
            formatArrayWithSpec(os, array, elemType, spec, dim + 1, at);
        }
    }
    os << "]";
}

/// `pattern` with the specification dropped from the fields whose argument is a numeric array and
/// whose text is therefore already written: the store hands fmt a string for such an argument,
/// and a string takes no numeric specification, so `{:.4f}` becomes `{}` while every other field
/// stays as written. `argumentIsArray` says which argument of the call was written element by
/// element.
std::string dropSpecsOfArrayFields(
    std::string_view pattern, const std::vector<bool> &argumentIsArray) {
    std::string out;
    size_t automaticIndex = 0;
    for (size_t i = 0; i < pattern.size();) {
        const char c = pattern[i];
        if (c == '{' && i + 1 < pattern.size() && pattern[i + 1] == '{') {
            out += "{{";
            i += 2;
            continue;
        }
        if (c == '}' && i + 1 < pattern.size() && pattern[i + 1] == '}') {
            out += "}}";
            i += 2;
            continue;
        }
        if (c != '{') {
            out += c;
            ++i;
            continue;
        }
        size_t j     = i + 1;
        size_t colon = std::string_view::npos;
        while (j < pattern.size() && pattern[j] != '}') {
            if (pattern[j] == ':' && colon == std::string_view::npos) {
                colon = j;
            }
            ++j;
        }
        if (j >= pattern.size()) {
            out.append(pattern.substr(i));
            break;
        }
        const std::string_view field = pattern.substr(i + 1, j - i - 1);
        const std::string_view name =
            colon == std::string_view::npos ? field : field.substr(0, colon - i - 1);
        size_t argument = 0;
        bool automatic  = name.empty() || !(name[0] >= '0' && name[0] <= '9');
        if (automatic) {
            argument = automaticIndex++;
        } else {
            size_t digits = 0;
            while (digits < name.size() && name[digits] >= '0' && name[digits] <= '9') {
                argument = argument * 10 + static_cast<size_t>(name[digits] - '0');
                ++digits;
            }
        }
        if (argument < argumentIsArray.size() && argumentIsArray[argument]) {
            out += "{}";
        } else {
            out.append(pattern.substr(i, j - i + 1));
        }
        i = j + 1;
    }
    return out;
}

std::optional<std::string> checkFormatPattern(
    std::string_view pattern, std::span<camel::core::type::Type *const> argumentTypes) {
    fmt::dynamic_format_arg_store<fmt::format_context> store;
    for (const camel::core::type::Type *type : argumentTypes) {
        switch (type->code()) {
        case TypeCode::Int32:
            store.push_back(Int32{0});
            break;
        case TypeCode::Int64:
            store.push_back(Int64{0});
            break;
        case TypeCode::Float32:
            store.push_back(camel::builtin::LanguageFloat<Float32>{0});
            break;
        case TypeCode::Float64:
            store.push_back(camel::builtin::LanguageFloat<Float64>{
                0, camel::builtin::FloatTextFormat::Float64});
            break;
        case TypeCode::BFloat16:
            // The two reduced-precision types take a plain `{}` and a specification alike, as
            // f32 does: the pattern check must accept what the run-time `__format__` accepts.
            store.push_back(camel::builtin::LanguageFloat<Float32>{
                0, camel::builtin::FloatTextFormat::BFloat16});
            break;
        case TypeCode::Float16:
            store.push_back(camel::builtin::LanguageFloat<Float32>{
                0, camel::builtin::FloatTextFormat::Float16});
            break;
        case TypeCode::Complex64:
            store.push_back(camel::builtin::LanguageComplex<Float32>{0, 0});
            break;
        case TypeCode::Complex128:
            store.push_back(camel::builtin::LanguageComplex<Float64>{0, 0});
            break;
        case TypeCode::Array: {
            // A specification acts on each element of a numeric array, so the check is the
            // element's: `'{:.4f}'->format<xs>` is checked as `'{:.4f}'` against the element type.
            // A non-numeric array stands for text, and `{}` is all a text takes.
            Type *elemType = formatElementTypeOf(const_cast<Type *>(type), nullptr);
            if (elemType == nullptr) {
                store.push_back(camel::builtin::CodePointText{""});
                break;
            }
            switch (elemType->code()) {
            case TypeCode::Int32:
                store.push_back(Int32{0});
                break;
            case TypeCode::Int64:
                store.push_back(Int64{0});
                break;
            case TypeCode::Float32:
                store.push_back(camel::builtin::LanguageFloat<Float32>{0});
                break;
            case TypeCode::Float64:
                store.push_back(camel::builtin::LanguageFloat<Float64>{
                    0, camel::builtin::FloatTextFormat::Float64});
                break;
            case TypeCode::BFloat16:
                store.push_back(camel::builtin::LanguageFloat<Float32>{
                    0, camel::builtin::FloatTextFormat::BFloat16});
                break;
            case TypeCode::Float16:
                store.push_back(camel::builtin::LanguageFloat<Float32>{
                    0, camel::builtin::FloatTextFormat::Float16});
                break;
            default:
                store.push_back(camel::builtin::CodePointText{""});
                break;
            }
            break;
        }
        default:
            store.push_back(camel::builtin::CodePointText{""});
            break;
        }
    }
    try {
        (void)fmt::vformat(std::string(pattern), store);
    } catch (const fmt::format_error &e) {
        return std::string(e.what());
    }
    return std::nullopt;
}

slot_t __format__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *fmtStrObj = norm.get<String *>(0);

    std::string fmtStr = fmtStrObj->toString();

    fmt::dynamic_format_arg_store<fmt::format_context> store;

    // A numeric array takes its specification element by element: `'{:.4f}'->format<xs>` is
    // `[0.7692, 0.7808]`, each element written by the specification and the brackets and the `, `
    // between them by the array's own form (the language reference's `format` section). Such an
    // argument is written into the text as the array's own text, so the store holds a string for
    // it; an array without a specification keeps the array form it prints as everywhere else.
    const std::vector<std::string> specs = placeholderSpecs(fmtStr);
    std::vector<bool> arrayIsElementwise(with.size(), false);

    for (size_t i = 0; i < with.size(); ++i) {
        TypeCode type = with.code(i);

        if (type == TypeCode::Array) {
            const Array *value = with.get<Array *>(i);
            Type *elemType     = formatElementTypeOf(with.type(i), value);
            std::string spec   = i < specs.size() ? specs[i] : std::string{};
            if (isNumericElementType(elemType) && !spec.empty() && value != nullptr) {
                std::ostringstream elementwise;
                if (value->rank() == 0) {
                    elementwise << formatElementText(elemType, value->getSlot(0), spec);
                } else {
                    formatArrayWithSpec(elementwise, value, elemType, spec, 0, 0);
                }
                store.push_back(camel::builtin::CodePointText{elementwise.str()});
                arrayIsElementwise[i] = true;
                continue;
            }
        }

        switch (type) {
        case TypeCode::Int32:
            store.push_back(with.get<Int32>(i));
            break;
        case TypeCode::Int64:
            store.push_back(with.get<Int64>(i));
            break;
        case TypeCode::Float32:
            store.push_back(camel::builtin::LanguageFloat<Float32>{with.get<Float32>(i)});
            break;
        case TypeCode::Float64:
            store.push_back(camel::builtin::LanguageFloat<Float64>{
                with.get<Float64>(i), camel::builtin::FloatTextFormat::Float64});
            break;
        case TypeCode::BFloat16:
            // A bf16 or an f16 has one text of its own (7.7, ruling 2026-10-23), and this is the
            // path `format` and string interpolation take: a plain `{}` writes that text.
            store.push_back(camel::builtin::LanguageFloat<Float32>{
                rtdata::halfValueFloat(with.slot(i), type),
                camel::builtin::FloatTextFormat::BFloat16});
            break;
        case TypeCode::Float16:
            store.push_back(camel::builtin::LanguageFloat<Float32>{
                rtdata::halfValueFloat(with.slot(i), type),
                camel::builtin::FloatTextFormat::Float16});
            break;
        case TypeCode::Complex64: {
            const Complex *value = with.get<Complex *>(i);
            store.push_back(camel::builtin::LanguageComplex<Float32>{
                value != nullptr ? value->realF() : Float32{0},
                value != nullptr ? value->imaginaryF() : Float32{0}});
            break;
        }
        case TypeCode::Complex128: {
            const Complex *value = with.get<Complex *>(i);
            store.push_back(camel::builtin::LanguageComplex<Float64>{
                value != nullptr ? value->real() : Float64{0},
                value != nullptr ? value->imaginary() : Float64{0}});
            break;
        }
        default:
            // fallback to string
            std::ostringstream oss;
            printSlot(oss, with.slot(i), with.type(i));
            store.push_back(camel::builtin::CodePointText{oss.str()});
            break;
        }
    }

    try {
        const std::string withArrayText = dropSpecsOfArrayFields(fmtStr, arrayIsElementwise);
        std::string resultStr           = fmt::vformat(withArrayText, store);
        String *resultObj               = String::from(resultStr, mm::autoSpace());
        return rtdata::toSlot(resultObj);
    } catch (const fmt::format_error &e) {
        throwRuntimeFault(RuntimeDiag::FormatFailed, std::string(fmtStr), std::string(e.what()));
    }
}

slot_t __join__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *sepObj = with.get<String *>(0);
    if (!sepObj) {
        throwRuntimeFault(RuntimeDiag::InvalidArgument, "join", "the separator is null");
    }
    std::string separator = sepObj->toString();

    Array *arrObj = norm.get<Array *>(0);
    const camel::core::type::ArrayType *arrType =
        tt::as_ptr<camel::core::type::ArrayType>(norm.type(0));
    camel::core::type::Type *elemType = arrType->elemType();

    std::ostringstream joined;
    size_t len = arrObj->size();

    for (size_t i = 0; i < len; ++i) {
        if (i > 0)
            joined << separator;
        slot_t slot = arrObj->get<slot_t>(i);
        printSlot(joined, slot, elemType);
    }

    String *resultObj = String::from(joined.str(), mm::autoSpace());
    return rtdata::toSlot(resultObj);
}
