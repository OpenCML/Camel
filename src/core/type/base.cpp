// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/base.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/composite/composite.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/other.h"
#include "camel/core/type/storage.h"
#include "camel/utils/log.h"
#include "camel/utils/type.h"

#include <string_view>

using namespace std;
using namespace camel::core::type;
using namespace camel::core::rtdata;

namespace camel::core::type {

namespace {

// Internal name -> the name a program writes. The only copy of this table.
constexpr std::pair<std::string_view, std::string_view> kSourceTypeNames[] = {
    {"i64", "int"},
    {"f64", "float"},
};

} // namespace

std::string sourceTypeSpelling(std::string_view text) {
    const auto isWord = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_';
    };
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (isWord(text[i]) && (i == 0 || !isWord(text[i - 1]))) {
            size_t end = i;
            while (end < text.size() && isWord(text[end])) {
                ++end;
            }
            std::string_view word = text.substr(i, end - i);
            for (const auto &[internal, source] : kSourceTypeNames) {
                if (word == internal) {
                    word = source;
                    break;
                }
            }
            out += word;
            i = end;
            continue;
        }
        out += text[i++];
    }
    return out;
}

string typeCodeToString(TypeCode code) {
    switch (code) {
    case TypeCode::Int32:
        return "i32";
    case TypeCode::Int64:
        return std::string(kSourceTypeNames[0].second);
    case TypeCode::Float32:
        return "f32";
    case TypeCode::Float64:
        return std::string(kSourceTypeNames[1].second);
    case TypeCode::Bool:
        return "bool";
    case TypeCode::Void:
        return "void";
    case TypeCode::Never:
        return "never";
    case TypeCode::String:
        return "string";
    case TypeCode::Complex64:
        return "c64";
    case TypeCode::Complex128:
        return "c128";
    case TypeCode::BFloat16:
        return "bf16";
    case TypeCode::Float16:
        return "f16";
    case TypeCode::Array:
        return "array";
    case TypeCode::Tuple:
        return "tuple";
    case TypeCode::Struct:
        return "struct";
    case TypeCode::Function:
        return "function";
    case TypeCode::Stream:
        return "stream";
    case TypeCode::Ref:
        return "ref";
    case TypeCode::TypeVar:
        return "typevar";
    case TypeCode::TypeFunc:
        return "typefunc";
    case TypeCode::EnumHole:
        return "_";
    case TypeCode::TypeOf:
        return "typeof";
    default:
        return isOtherType(code) ? OtherTypeRegistry::getTypeName(code) : "unknown";
    }
}

Type::Type(TypeCode type) : code_(type) {}

const TypeCode &Type::code() const { return code_; }

std::string Type::toString() const { return typeCodeToString(code_); }

std::string Type::mangle() const {
    GUARD(!camel::core::type::isComposite(code_), "Composite type cannot call Type::mangle");
    GUARD(!camel::core::type::isOtherType(code_), "Other type cannot call Type::mangle");

    switch (code_) {
    case TypeCode::Int32:
        return "i32";
    case TypeCode::Int64:
        return "i";
    case TypeCode::Float32:
        return "f32";
    case TypeCode::Float64:
        return "f";
    case TypeCode::Bool:
        return "b";
    case TypeCode::Void:
        return "v";
    case TypeCode::String:
        return "s";
    case TypeCode::Complex64:
        return "c64";
    case TypeCode::Complex128:
        return "c128";
    case TypeCode::BFloat16:
        return "bf16";
    case TypeCode::Float16:
        return "f16";
    case TypeCode::Ref:
        return "r";
    case TypeCode::EnumHole:
        return "H";
    default:
        ASSERT(false, "Unknown TypeCode");
        return "";
    }

    return ""; // unknown
}

Type *Type::clone(bool deep) const {
    GUARD(!camel::core::type::isComposite(code_), "Composite type cannot call Type::clone");
    GUARD(!camel::core::type::isOtherType(code_), "Other type cannot call Type::clone");
    return Type::create(code_);
}

bool Type::equals(Type *type) const {
    GUARD(!camel::core::type::isComposite(code_), "Composite type cannot call Type::equals");
    GUARD(!camel::core::type::isOtherType(code_), "Other type cannot call Type::equals");
    if (!type)
        return false;
    return code_ == type->code_;
}

Type *Type::unify(Type *other) const { return equals(other) ? const_cast<Type *>(this) : nullptr; }

Type *Type::widened() const { return const_cast<Type *>(this); }

Type *Type::literalOperandType() const {
    const bool numeric = isNumericScalar(code_) || camel::core::type::isComplex(code_);
    return numeric ? const_cast<Type *>(this) : nullptr;
}

bool Type::extentForms(std::vector<ExtentForm> &out) const {
    (void)out;
    return false;
}

std::string_view Type::packName() const { return {}; }

bool Type::openRank() const { return false; }

const Type *Type::withExtentValues(const ExtentValues &values, const ExtentPacks &packs) const {
    (void)values;
    (void)packs;
    return this;
}

const Type *
Type::applyArguments(std::span<const TypeArgument> arguments, std::string &error) const {
    (void)arguments;
    error = "this type takes no arguments";
    return nullptr;
}

ExtentBinding bindExtents(
    const Type *parameter, const Type *actual, ExtentValues &values, ExtentPacks &packs,
    std::string &mismatch) {
    if (parameter == nullptr || actual == nullptr) {
        return ExtentBinding::NotDimensioned;
    }
    std::vector<ExtentForm> forms;
    if (!parameter->extentForms(forms)) {
        return ExtentBinding::NotDimensioned;
    }
    const std::string_view pack = parameter->packName();
    bool hasNames               = !pack.empty();
    for (const ExtentForm &form : forms) {
        hasNames = hasNames || form.hasNames();
    }
    if (!hasNames) {
        return ExtentBinding::NotDimensioned;
    }
    // The type object decides what its extents mean; another kind of type at the argument is
    // left to the ordinary argument check, which reports the mismatch it always did.
    if (actual->code() != parameter->code()) {
        return ExtentBinding::NotDimensioned;
    }
    std::vector<ExtentForm> actualForms;
    if (!actual->extentForms(actualForms)) {
        return ExtentBinding::NotDimensioned;
    }
    for (size_t i = 0; i < forms.size() && i < actualForms.size(); ++i) {
        // Only a dimension the argument fixes statically binds a name; an unknown one is
        // checked by the conversion at the call when it runs (design principles 5.4).
        if (!actualForms[i].isConstant()) {
            continue;
        }
        if (!forms[i].bind(actualForms[i].constant(), values)) {
            mismatch = actualForms[i].toString();
            return ExtentBinding::Mismatch;
        }
    }
    if (!pack.empty()) {
        // The pack stands for the argument's dimensions after the parameter's prefix. When the
        // argument's own rank is open (`T[3, ..]`, `T[..s]`), those dimensions are not known
        // here; the declaration stays and the conversion checks it at the call.
        const bool actualOpen =
            actual->openRank() || !actual->packName().empty() || actualForms.empty();
        // A pack bound to no dimensions is recorded as an empty list, not left absent: that is
        // what lets a return type consisting of the pack alone fold to the element type (an
        // argument of exactly the prefix's rank). An absent pack means "no call bound it".
        if (!actualOpen && actualForms.size() >= forms.size()) {
            packs[std::string(pack)] =
                std::vector<ExtentForm>(actualForms.begin() + forms.size(), actualForms.end());
        }
    }
    return ExtentBinding::Bound;
}

CastSafety Type::castSafetyFrom(Type *sourceType) const {
    GUARD(
        !camel::core::type::isComposite(code_),
        "Composite type cannot call Type::castSafetyFrom");
    GUARD(!camel::core::type::isOtherType(code_), "Other type cannot call Type::castSafetyFrom");

    if (!sourceType)
        return CastSafety::Forbidden;

    // Converting a value to its own type is legal and does nothing (design principles 5.1).
    // The composite types decide this themselves; this covers the ones that live in a slot
    // (the primitives, a text, the complex numbers) and any other type without fields.
    if (equals(sourceType))
        return CastSafety::Safe;

    // A value of the bottom type `never` never exists (design principles 3.9), and it is
    // assignable to every type: entering a position of any type is nothing to convert, so the
    // conversion is safe wherever the value is not (there is no value to lose).
    if (sourceType->code() == TypeCode::Never) {
        return CastSafety::Safe;
    }

    // Every other conversion between these types is refused (design principles 5.1): `as` is the
    // checked conversion of module types, shapes and the forms `T?` / `T!`, and never a
    // conversion between numbers, between a number and a bool or a text, or from a text. A number
    // of another type is made by the conversion function of the target (`float(n)`, `int(x)`,
    // `f32(x)`), whose loss rule is the function's own. A literal is not converted either: it
    // takes the type of its position (`let x: float = 5`).
    return CastSafety::Forbidden;
}

slot_t Type::castSlotFrom(slot_t value, Type *sourceType) const {
    ASSERT(sourceType != nullptr, "castSlotFrom source type is null");
    ASSERT(
        castSafetyFrom(sourceType) == CastSafety::Safe,
        "castSlotFrom only allowed when castSafetyFrom returns Safe");
    // The table (castSafetyFrom) holds one entry for a type that lives in a slot: itself, and
    // converting a value to its own type does nothing.
    return value;
}

bool Type::assignableFrom(Type *from) const {
    const Type *to = this;

    // An unknown source is accepted by no type: the answer does not depend on the target.
    if (!from)
        return false;
    if (to == from)
        return true;

    // Built-in types: composite types containing Ref must be resolved before assignment.
    ASSERT(
        to->code_ != TypeCode::Ref && from->code_ != TypeCode::Ref,
        "Ref type cannot be assigned");

    // A type variable stands for a type that is not known yet: it only matches itself.
    if (to->code_ == TypeCode::TypeVar || from->code_ == TypeCode::TypeVar)
        return to->equals(from);

    // The bottom type `never` (design principles 3.9): the expression it is the type of gives
    // the function no value, so there is nothing that could be of a type the target does not
    // take, and it is assignable to every type.
    if (from->code_ == TypeCode::Never)
        return true;

    // `void` is a type of its own: it takes `void` and nothing else, and is taken by nothing but
    // itself. A function whose result is discarded (`=> void`) is the one place that takes any
    // result, and it says so itself (FunctionType::assignableFrom).
    if (to->code_ == TypeCode::Void || from->code_ == TypeCode::Void)
        return to->code_ == from->code_;

    // Third-party types rely on their own overridden assignableFrom. This must be
    // checked before the composite flag: an Other type may also be flagged
    // Composite (e.g. Tensor) but is never a CompositeType object.
    if (camel::core::type::isOtherType(to->code_)) {
        const auto &self = static_cast<const OtherType &>(*this);
        return self.assignableFrom(from);
    }

    // Built-in composite types must override assignableFrom.
    if (camel::core::type::isComposite(to->code_)) {
        const auto &self = static_cast<const CompositeType &>(*this);
        return self.assignableFrom(from);
    }

    // Remaining cases are assignments between primitive types, which must match exactly.
    return to->code_ == from->code_;
}

Type *Type::create(TypeCode code) {
    // One object per code, held by the type table (see the header): a dtype a module states, a
    // primitive singleton and a clone of one are all the same type, with one id. The code is a
    // 32-bit enum and the key's word is a `uintptr_t`, so it enters as the number it is and not as
    // the bytes of the object: those are four bytes, and reading a word from them drags whatever
    // follows them into the key, which would make two spellings of one code two types.
    const uintptr_t word = static_cast<uintptr_t>(code);
    return TypeTable::instance().intern(
        TypeTable::Shape::Primitive, std::span<const uintptr_t>(&word, 1),
        [&]() -> Type * {
            EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                "Type",
                "Allocating Type: {}, size: {} bytes",
                typeCodeToString(code),
                sizeof(Type)));
            void *mem = allocTypeStorage<Type>(TypeStorageKind::Primitive);
            return new (mem) Type(code);
        });
}

Type *Type::Int32() { return Type::create(TypeCode::Int32); }

Type *Type::Int64() { return Type::create(TypeCode::Int64); }

Type *Type::Int() { return Type::Int64(); }

Type *Type::Float32() { return Type::create(TypeCode::Float32); }

Type *Type::Float64() { return Type::create(TypeCode::Float64); }

Type *Type::Float() { return Type::Float64(); }

Type *Type::Bool() { return Type::create(TypeCode::Bool); }

Type *Type::Void() { return Type::create(TypeCode::Void); }
Type *Type::Never() { return Type::create(TypeCode::Never); }

Type *Type::String() { return Type::create(TypeCode::String); }

Type *Type::Complex64() { return Type::create(TypeCode::Complex64); }

Type *Type::Complex128() { return Type::create(TypeCode::Complex128); }

Type *Type::BFloat16() { return Type::create(TypeCode::BFloat16); }

Type *Type::Float16() { return Type::create(TypeCode::Float16); }

Type *Type::Ref() { return Type::create(TypeCode::Ref); }

} // namespace camel::core::type