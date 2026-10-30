// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/composite/array.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/other.h"
#include "camel/core/type/pure.h"
#include "camel/core/type/storage.h"
#include "camel/core/type/text.h"
#include "camel/utils/assert.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <functional>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace camel::core::error;
using namespace camel::core::type;

namespace camel::core::type {

ArrayStorage arrayStorageFor(const Type *elementType) {
    if (!elementType) {
        return ArrayStorage::Slots;
    }
    // A compact array is a run of pure data: bytes the type system has proved contain no
    // reference, so they can live in a data block, be compared with memcmp and be handed out
    // without a copy. Numbers, booleans, complex numbers, and structs and tuples made only of
    // them qualify; a reference (a string, a nested array, a closure, a tagged form) does not.
    return isPureDataType(elementType) ? ArrayStorage::Compact : ArrayStorage::Slots;
}

bool isCompactElementType(TypeCode code) {
    // The scalar half of the pure-data predicate: the one table `isPureDataType` reads first
    // (pure.h), so a scalar with a layout is exactly a scalar an array stores as a run of bytes and
    // an interchange can name -- integers, floats of every width, bool and the two complex types.
    // A struct or tuple of pure data is stored the same way and has a layout too (arrayStorageFor
    // takes it), but an interchange has no dtype for an aggregate, so it is not a compact element.
    return scalarDataLayout(code) != nullptr;
}

bool isNumericScalarTypeCode(TypeCode code) { return isCompactElementType(code); }

std::vector<ExtentForm> canonicalShape(std::span<const ExtentForm> forms) {
    // The names in the order they first appear; a form lists its terms by name, which is not an
    // order two spellings share, so the numbering is the one that makes the whole shape smallest.
    std::vector<std::string> names;
    for (const ExtentForm &form : forms) {
        for (const auto &[name, coefficient] : form.terms()) {
            if (std::find(names.begin(), names.end(), name) == names.end()) {
                names.push_back(name);
            }
        }
    }
    if (names.empty()) {
        return std::vector<ExtentForm>(forms.begin(), forms.end());
    }
    const auto encode = [&](const std::vector<size_t> &ids) {
        std::vector<int64_t> key;
        for (const ExtentForm &form : forms) {
            key.push_back(static_cast<int64_t>(form.kind()));
            key.push_back(form.constant());
            if (form.isComputed()) {
                // An expression is part of the shape's identity as it is written.
                key.push_back(
                    static_cast<int64_t>(std::hash<std::string>{}(form.expression()->toString())));
            }
            std::vector<std::pair<size_t, int64_t>> terms;
            for (const auto &[name, coefficient] : form.terms()) {
                const auto at = std::find(names.begin(), names.end(), name) - names.begin();
                terms.emplace_back(ids[static_cast<size_t>(at)], coefficient);
            }
            std::sort(terms.begin(), terms.end());
            for (const auto &[id, coefficient] : terms) {
                key.push_back(static_cast<int64_t>(id));
                key.push_back(coefficient);
            }
            key.push_back(-1000000007);
        }
        return key;
    };
    std::vector<size_t> ids(names.size());
    std::iota(ids.begin(), ids.end(), size_t{0});
    std::vector<size_t> best = ids;
    if (names.size() <= 7) {
        std::vector<int64_t> bestKey = encode(ids);
        while (std::next_permutation(ids.begin(), ids.end())) {
            std::vector<int64_t> key = encode(ids);
            if (key < bestKey) {
                bestKey = std::move(key);
                best    = ids;
            }
        }
    }
    std::vector<ExtentForm> out;
    out.reserve(forms.size());
    for (const ExtentForm &form : forms) {
        if (form.kind() != ExtentForm::Kind::Linear) {
            out.push_back(form);
            continue;
        }
        std::vector<std::pair<std::string, int64_t>> terms;
        for (const auto &[name, coefficient] : form.terms()) {
            const auto at = std::find(names.begin(), names.end(), name) - names.begin();
            terms.emplace_back("#" + std::to_string(best[static_cast<size_t>(at)]), coefficient);
        }
        out.push_back(ExtentForm::linear(form.constant(), std::move(terms)));
    }
    return out;
}

bool ArrayType::sameShape(const ArrayType &other) const {
    if (anyRank_ != other.anyRank_ || extents_.size() != other.extents_.size()) {
        return false;
    }
    bool named = false;
    for (size_t i = 0; i < extents_.size(); ++i) {
        named = named || extents_[i].hasNames() || other.extents_[i].hasNames();
        if (extents_[i].isConstant() != other.extents_[i].isConstant() ||
            extents_[i].isUnknown() != other.extents_[i].isUnknown() ||
            (extents_[i].isConstant() && extents_[i].constant() != other.extents_[i].constant())) {
            return false;
        }
    }
    if (!named) {
        return true;
    }
    return canonicalShape(extents_) == canonicalShape(other.extents_);
}

bool sameExtentConstraint(const ExtentForm &a, const ExtentForm &b) {
    // A constant extent is a value the shape must equal; every other extent (unknown, or a
    // linear form over names a signature binds) asks for nothing at the call. This is the
    // distinction the tensor type's shape comparison made before dimensions lived here.
    if (a.isConstant() || b.isConstant()) {
        return a.isConstant() && b.isConstant() && a.constant() == b.constant();
    }
    return true;
}

size_t arrayElementSize(TypeCode code) {
    switch (code) {
    case TypeCode::Int32:
    case TypeCode::Float32:
        return 4;
    case TypeCode::Int64:
    case TypeCode::Float64:
        return 8;
    case TypeCode::Complex64:
        return 8;
    case TypeCode::Complex128:
        return 16;
    case TypeCode::Bool:
        return 1;
    case TypeCode::BFloat16:
    case TypeCode::Float16:
        return 2;
    default:
        ASSERT(false, "Not a compact element type");
        return 0;
    }
}

} // namespace camel::core::type

ArrayType *ArrayTypeFactory::build() {
    if (!elemType_) {
        elemType_ = Type::Void();
    }
    return ArrayType::fromFactory(*this);
}

ArrayType::ArrayType(
    Type *elemType, std::vector<ExtentForm> extents, bool anyRank, const std::string *packName)
    : CompositeType(TypeCode::Array), elemType_(elemType), elemTypeCode_(elemType->code()),
      extents_(std::move(extents)), anyRank_(anyRank), packName_(packName) {}

ArrayType *ArrayType::make(
    Type *elemType, std::vector<ExtentForm> extents, bool anyRank, std::string_view pack) {
    if (!elemType) {
        elemType = Type::Void();
    }
    // A shape cannot be left unnormalized: a negative extent is not a dimension (the unknown
    // extent is `?`, and the checked conversion never keeps `-1` as a size). The caller turns
    // this into an invalid-type-argument diagnostic; it is not silently kept.
    for (const ExtentForm &form : extents) {
        if (form.isConstant() && form.constant() < 0) {
            throw std::invalid_argument(
                "an array extent cannot be negative (" + form.toString() +
                "); write '?' for an unknown extent");
        }
    }
    // `T[]` and `T[?]` are the same type: one unknown extent. An empty list means open rank
    // only when a pack makes it so; a fixed rank is at least one.
    if (!pack.empty()) {
        anyRank = true;
    }
    if (extents.empty() && !anyRank) {
        extents.push_back(ExtentForm::unknown());
    }
    const std::vector<ExtentForm> *canonical =
        TypeTable::instance().internExtents(std::span<const ExtentForm>(extents));
    const std::string *packText =
        pack.empty() ? nullptr : TypeTable::instance().internText(pack);
    // A dimension name and a pack name are in the key because a type prints them; `equals`
    // ignores them (text.cpp: sameExtentConstraint), so the same shape with the names erased is
    // the canonical form this type's canonical id names (see intern.h).
    bool hasNames = packText != nullptr;
    for (const ExtentForm &form : extents) {
        hasNames = hasNames || form.hasNames();
    }
    const uintptr_t flags = static_cast<uintptr_t>(anyRank ? 1u : 0u);
    // The element type is a component like any other, so it enters the key by id; the object
    // holds the very object it was asked for, which is what a caller that prints the type sees.
    const uintptr_t words[4] = {
        typeKeyWord(elemType),
        flags,
        reinterpret_cast<uintptr_t>(canonical),
        reinterpret_cast<uintptr_t>(packText),
    };
    const auto createObject = [&](Type *element, const std::vector<ExtentForm> &forms,
                                  const std::string *packName) -> Type * {
        EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
            "ArrayType",
            "Allocating ArrayType: {} , size: {} bytes",
            typeCodeToString(element->code()),
            sizeof(ArrayType)));
        void *mem = allocTypeStorage<ArrayType>(TypeStorageKind::Array);
        return new (mem) ArrayType(element, forms, anyRank, packName);
    };
    if (!hasNames) {
        return static_cast<ArrayType *>(TypeTable::instance().intern(
            TypeTable::Shape::Array, std::span<const uintptr_t>(words, 4),
            [&]() -> Type * { return createObject(elemType, extents, packText); }));
    }
    // The canonical twin: the shape with its dimension names numbered in a canonical order (a
    // linear form is the same form whatever its names are called, `[n]` and `[n + 1]` are two
    // shapes), and the pack name dropped, which is exactly what the shape's mangled name erases.
    std::vector<ExtentForm> plainForms = canonicalShape(extents);
    const std::vector<ExtentForm> *plainShape =
        TypeTable::instance().internExtents(std::span<const ExtentForm>(plainForms));
    Type *plainElement        = TypeTable::instance().canonical(elemType);
    const uintptr_t plain[4]  = {
        typeKeyWord(plainElement),
        flags,
        reinterpret_cast<uintptr_t>(plainShape),
        reinterpret_cast<uintptr_t>(nullptr),
    };
    return static_cast<ArrayType *>(TypeTable::instance().intern(
        TypeTable::Shape::Array, std::span<const uintptr_t>(words, 4),
        [&]() -> Type * { return createObject(elemType, extents, packText); },
        TypeTable::Canonical{
            std::span<const uintptr_t>(plain, 4),
            [&]() -> Type * { return createObject(plainElement, plainForms, nullptr); }}));
}

ArrayType *ArrayType::create(Type *elemType) {
    if (!elemType) {
        elemType = Type::Void();
    }
    // One object per element type and shape (camel/core/type/intern.h): every `int[]` a program
    // writes, every array a rule infers, and every re-encoding of a graph share it.
    return make(elemType, {}, false, {});
}

ArrayType *ArrayType::create(Type *elemType, std::span<const int64_t> extents) {
    std::vector<ExtentForm> forms;
    forms.reserve(extents.size());
    for (int64_t extent : extents) {
        forms.push_back(ExtentForm::constant(extent));
    }
    return make(elemType, std::move(forms), false, {});
}

ArrayType *ArrayType::create(
    Type *elemType, std::span<const ExtentForm> extents, std::string_view pack) {
    return make(elemType, std::vector<ExtentForm>(extents.begin(), extents.end()), false, pack);
}

ArrayType *ArrayType::openRank(Type *elemType, std::span<const ExtentForm> prefix) {
    return make(elemType, std::vector<ExtentForm>(prefix.begin(), prefix.end()), true, {});
}

ArrayType *ArrayType::fromFactory(ArrayTypeFactory &factory) { return create(factory.elemType_); }

ArrayType *ArrayType::fromData(Type *elemType) { return create(elemType); }

std::string_view ArrayType::packName() const {
    return packName_ == nullptr ? std::string_view() : std::string_view(*packName_);
}

std::string ArrayType::shapeText() const {
    // `T[?]` is printed `T[]`: the unknown single dimension is spelled with empty brackets, so
    // every `int[]` program keeps the text it had before dimensions moved into this type.
    if (!anyRank_ && extents_.size() == 1 && extents_[0].isUnknown()) {
        return "[]";
    }
    std::string text = "[";
    for (size_t i = 0; i < extents_.size(); ++i) {
        text += (i ? ", " : "");
        text += extents_[i].toString();
    }
    if (anyRank_) {
        if (!extents_.empty()) {
            text += ", ";
        }
        text += "..";
        text += packName();
    }
    text += "]";
    return text;
}

std::string ArrayType::shapeMangle() const {
    std::string shape = anyRank_ ? "any_" : std::to_string(extents_.size()) + "_";
    const std::vector<ExtentForm> canonical = canonicalShape(extents_);
    for (size_t i = 0; i < extents_.size(); ++i) {
        shape += (i ? "," : "");
        // The spelling of a dimension name is not part of the type's identity, so it is not in the
        // mangled name; a linear form is, as the canonical shape writes it.
        const ExtentForm &form = canonical[i];
        if (form.isConstant()) {
            shape += std::to_string(form.constant());
        } else if (form.isUnknown()) {
            shape += "?";
        } else if (form.isComputed()) {
            shape += "E" + std::to_string(std::hash<std::string>{}(form.expression()->toString()));
        } else {
            shape += "L" + std::string(form.constant() < 0 ? "m" : "") +
                     std::to_string(form.constant() < 0 ? -form.constant() : form.constant());
            for (const auto &[name, coefficient] : form.terms()) {
                shape += "_" + name.substr(1) + "x" + (coefficient < 0 ? "m" : "") +
                         std::to_string(coefficient < 0 ? -coefficient : coefficient);
            }
        }
    }
    return shape;
}

ArrayType *ArrayType::withElementType(Type *elemType) const {
    if (!elemType || elemType == elemType_) {
        // Nothing states another element type: asking a type for its own element type leaves it
        // as it is.
        return const_cast<ArrayType *>(this);
    }
    return make(elemType, extents_, anyRank_, packName());
}

Type *ArrayType::firstDimensionElement() const {
    // The axis 0 case of the one rule below: what `t[i]` takes for every rank (design principles
    // 5.4), for every array: the rule lives here, in one place.
    return elementAlongAxis(0);
}

Type *ArrayType::elementAlongAxis(size_t axis) const {
    // One dimension fewer: every extent but the one the axis names, which is what a position
    // written on that dimension leaves of the value, for every rank and every dimension.
    std::vector<ExtentForm> rest(extents_.begin(), extents_.end());
    if (axis < rest.size()) {
        rest.erase(rest.begin() + static_cast<std::ptrdiff_t>(axis));
    }
    const std::span<const ExtentForm> kept(rest);
    if (anyRank_) {
        // Open rank: a dimension dropped from "at least n" leaves an open rank with the rest of
        // the prefix, which is the type `iterInfer` gave the same value. An axis inside the open
        // part leaves the prefix as it is.
        return openRank(elemType_, kept);
    }
    if (kept.empty()) {
        // Rank 1: the first dimension's elements are the elements themselves — a scalar is not a
        // rank-0 array, it is the element type. A
        // nested array's `T[][]` is one dimension of `T[]`.
        return elemType_;
    }
    return create(elemType_, kept, packName());
}

Type *ArrayType::resolve(const type_vec_t &typeList) const {
    ASSERT(typeList.size() > 0, "Type list is empty");
    ASSERT(!resolved(), "ArrayType is already resolved");

    Type *newElemType = elemType_;
    for (const auto &type : typeList) {
        // Void and Ref stand for "no element seen yet".
        if (newElemType->code() == TypeCode::Void || newElemType->code() == TypeCode::Ref) {
            newElemType = type;
        } else if (Type *joined = newElemType->unify(type)) {
            // Elements of refinement-carrying types (e.g. tensors of different shapes) share
            // their least common type.
            newElemType = joined;
        } else if (entersImplicitly(type, newElemType)) {
            // An element of the more specific type (an unnamed struct value beside a named
            // one, design principles 5.2): the array is of the type it enters, and the value
            // is converted where it is put into the array.
            newElemType = type;
        } else if (entersImplicitly(newElemType, type)) {
            // Already the type the new element enters.
        } else {
            throw DiagnosticBuilder::of(SemanticDiag::ElementTypeMismatch)
                .commit("Array", type->toString(), newElemType->toString());
        }
    }
    ArrayType *newArray = withElementType(newElemType);
    ASSERT(newArray->resolved(), "ArrayType is not fully resolved");
    return newArray;
}

bool ArrayType::resolved() const { return elemType_->code() != TypeCode::Ref; }

string ArrayType::toString() const { return typeToString(this); }

std::string ArrayType::mangle() const { return typeMangle(this); }

Type *ArrayType::clone(bool deep /* = false */) const {
    // Interned: a shallow clone is this very object, which is what an immutable type allows.
    if (!deep) {
        return const_cast<ArrayType *>(this);
    }
    return withElementType(elemType_->clone(true));
}

bool ArrayType::equals(Type *other) const { return typeEquals(this, other); }

// Least common type at a branch join. A rank or pack difference leaves the rank unknown; a
// common rank keeps the extents both sides state and makes the others unknown. The element type
// follows the same rule: two numeric element types that have no common type have no common array
// either, because the element type of a value is always in its type (design principles 5.1).
Type *ArrayType::unify(Type *other) const {
    if (other == nullptr || other->code() != TypeCode::Array) {
        return nullptr;
    }
    const auto *rhs = static_cast<const ArrayType *>(other);
    Type *elem     = elemType_->unify(rhs->elemType_);
    if (elem == nullptr) {
        return nullptr;
    }
    const bool sameSkeleton =
        anyRank_ == rhs->anyRank_ && extents_.size() == rhs->extents_.size();
    std::vector<ExtentForm> forms;
    bool anyRank = false;
    if (sameSkeleton) {
        forms.reserve(extents_.size());
        for (size_t i = 0; i < extents_.size(); ++i) {
            if (extents_[i] == rhs->extents_[i] && !extents_[i].isUnknown()) {
                // The same constant, or the same form over the same names.
                forms.push_back(extents_[i]);
            } else {
                forms.push_back(ExtentForm::unknown());
            }
        }
        anyRank = anyRank_;
    } else {
        // The ranks differ, so only "an array of this element type" is common; its rank is
        // unknown (`int[..]`).
        anyRank = true;
    }
    return make(
        elem, std::move(forms), anyRank,
        anyRank && sameSkeleton ? packName() : std::string_view());
}

// Converting a value to its own type is legal and does nothing (design principles 5.1). From a
// less precise array type, what the two static types already contradict is refused now; what
// the source leaves open is checked when the conversion runs (castSlotFrom): Unsafe.
CastSafety ArrayType::castSafetyFrom(Type *sourceType) const {
    if (sourceType == nullptr) {
        return CastSafety::Forbidden;
    }
    // A type that still holds the `Ref` placeholder is not one a value can have, so nothing is
    // converted to or from it: the conversion refuses it here for the reason
    // ArrayType::assignableFrom states below, instead of asking whether its element type fits
    // (which that comparison cannot answer). A type against itself is not such a question.
    if (this == sourceType) {
        return CastSafety::Safe;
    }
    if (!resolved() || (sourceType->code() == TypeCode::Array &&
                        !static_cast<const ArrayType *>(sourceType)->resolved())) {
        return CastSafety::Forbidden;
    }
    if (assignableFrom(sourceType)) {
        return CastSafety::Safe;
    }
    if (sourceType->code() != TypeCode::Array) {
        return CastSafety::Forbidden;
    }
    const auto *source = static_cast<const ArrayType *>(sourceType);
    // Element types: a target refuses a source it already knows cannot fit; an element type the
    // source states as a name (nothing does today) is left to the runtime check.
    if (elemTypeCode_ != TypeCode::Void && !elemType_->assignableFrom(source->elemType_)) {
        return CastSafety::Forbidden;
    }
    // Shapes: equal ranks are required when this type fixes one; a constant extent the source
    // states differently is a contradiction, an unknown one is checked at the call.
    if (!anyRank_) {
        if (!source->anyRank_ && extents_.size() != source->extents_.size()) {
            return CastSafety::Forbidden;
        }
        if (!source->anyRank_) {
            for (size_t i = 0; i < extents_.size() && i < source->extents_.size(); ++i) {
                if (extents_[i].isConstant() && source->extents_[i].isConstant() &&
                    extents_[i].constant() != source->extents_[i].constant()) {
                    return CastSafety::Forbidden;
                }
            }
        }
    } else {
        if (!source->anyRank_ && source->extents_.size() < extents_.size()) {
            return CastSafety::Forbidden;
        }
        for (size_t i = 0; i < extents_.size() && i < source->extents_.size(); ++i) {
            if (extents_[i].isConstant() && source->extents_[i].isConstant() &&
                extents_[i].constant() != source->extents_[i].constant()) {
                return CastSafety::Forbidden;
            }
        }
    }
    return CastSafety::Unsafe;
}

// The runtime half of the checked conversion: the shape is checked against the type the program
// declared. After the tensor alias the array is where a numeric array states its shape, and these
// diagnostics are its own: they say `the array has shape [2, 3]`, which is what the value is.
//
// A conversion that runs here can be Forbidden by the two types and still have to run. The types
// compared are the ones the value and the target have now, and a pass may sharpen the source: a
// call folded into the constant it returns, or a constant re-inferred at its exact type, turns the
// `f32[..]` the program wrote into `f32[2, 3]`, so the `as f32[3, 2]` beside it converts a shape
// that cannot fit. That conversion is the program's own and it fails when the program runs, with
// the diagnostic below, exactly as it does without any pass (design principles 3.9: a conversion
// that can only fail is neither folded away nor turned into a compile-time refusal; 5.4: the
// conversion is where a shape is checked). So the question here is not "would the builder have
// admitted these two types?" -- it answers that for the types the program wrote, and refuses them
// there -- but "what does the value hold?", which every answer but Safe asks below.
slot_t ArrayType::castSlotFrom(slot_t value, Type *sourceType) const {
    ASSERT(sourceType != nullptr, "castSlotFrom source type is null");
    const CastSafety safety = castSafetyFrom(sourceType);
    if (safety == CastSafety::Safe) {
        return value;
    }
    // What every other answer needs is a value of a type a value can have: `Unsafe` is answered
    // only for an array source, and no compiled value has a type that still holds the `Ref`
    // placeholder (design principles 5.1, 5.2). A source of another kind is one the builder
    // refused while reading the program, and its slot is not one this check can read.
    ASSERT(
        resolved() && sourceType->code() == TypeCode::Array &&
            static_cast<const ArrayType *>(sourceType)->resolved(),
        "an array converts from a resolved array type");
    auto *array      = camel::core::rtdata::fromSlot<Array *>(value);
    const auto refuse = [&](const std::string &what) {
        camel::core::error::throwRuntimeFault(
            camel::core::error::RuntimeDiag::RuntimeError,
            "conversion to '" + toString() + "' failed: " + what);
    };
    if (array == nullptr) {
        refuse("the value is null");
    }
    if (elemTypeCode_ != TypeCode::Void && array->dtype() != elemTypeCode_) {
        // The static element type is exact; the check costs one comparison and makes an unsafe
        // conversion honest.
        refuse("the array has element type " + typeCodeToString(array->dtype()));
    }
    std::string actual = "[";
    for (size_t i = 0; i < array->rank(); ++i) {
        actual += (i ? ", " : "") + std::to_string(array->dim(i));
    }
    actual += "]";
    if (!anyRank_) {
        if (array->rank() != extents_.size()) {
            refuse("the array has shape " + actual);
        }
        for (size_t i = 0; i < extents_.size(); ++i) {
            if (extents_[i].isConstant() &&
                (array->dim(i) < 0 || static_cast<int64_t>(array->dim(i)) != extents_[i].constant())) {
                refuse("the array has shape " + actual);
            }
        }
    } else {
        if (array->rank() < extents_.size()) {
            refuse("the array has shape " + actual);
        }
        for (size_t i = 0; i < extents_.size(); ++i) {
            if (extents_[i].isConstant() &&
                (array->dim(i) < 0 || static_cast<int64_t>(array->dim(i)) != extents_[i].constant())) {
                refuse("the array has shape " + actual);
            }
        }
    }
    return value;
}

// Arrays have no element mutation, so they accept arrays of assignable element types (a refined
// module type where its general form is expected; a number of one type is never assignable to
// another, design principles 5.1) whose shape this type permits.
bool ArrayType::assignableFrom(Type *sourceType) const {
    if (this == sourceType) {
        return true;
    }
    if (!sourceType) {
        return false;
    }
    if (sourceType->code() != TypeCode::Array) {
        // An open rank with no prefix takes rank zero too, and a rank-zero array is the element
        // itself (design principles 5.4): a value of the element type is a value of `T[..]` --
        // the same value, not an array holding it. `T[]` does not take it: it states one
        // dimension, and the element type is rank zero, not rank one. Neither does an open rank
        // with a prefix (`T[3, ..]`), whose prefix states dimensions the value must have.
        return rankZeroType() != nullptr && resolved() && elemType_->assignableFrom(sourceType);
    }
    const auto *source = static_cast<const ArrayType *>(sourceType);
    // A type that still holds the `Ref` placeholder is not one a value can have (design
    // principles 5.1: every value has a determined type, and no type is undetermined after
    // compilation). `resolved()` names that state, and `resolve` is what turns it into a type
    // from the computed elements of a literal. Nothing is assignable to it and it is assignable
    // to nothing, so the answer is `false` -- not a comparison of the placeholder with an element
    // type, which the primitive comparison cannot answer and asserts on.
    if (!resolved() || !source->resolved()) {
        return false;
    }
    if (elemTypeCode_ != TypeCode::Void && !elemType_->assignableFrom(source->elemType_)) {
        return false;
    }
    if (!anyRank_) {
        // A fixed rank accepts only that rank, with the extents this type states; an extent
        // that is not a constant (unknown, or a form a signature binds) accepts any size.
        if (source->anyRank_ || extents_.size() != source->extents_.size()) {
            return false;
        }
    } else {
        // An open rank with a prefix needs the argument to promise at least the prefix; an
        // argument whose own rank is open cannot promise it, so the conversion is checked at
        // the call instead (`as` from such an argument is Unsafe, not implicit).
        if (source->anyRank_) {
            if (!extents_.empty()) {
                return false;
            }
        } else if (source->extents_.size() < extents_.size()) {
            return false;
        }
    }
    for (size_t i = 0; i < extents_.size() && i < source->extents_.size(); ++i) {
        if (extents_[i].isConstant() && !sameExtentConstraint(extents_[i], source->extents_[i])) {
            return false;
        }
    }
    return true;
}

// Widening holds any array of the same rank: the element type is widened (so mutation through
// a `var` parameter cannot break assignability) and every dimension value is forgotten. A
// named pack is dropped, since the dimensions it stood for are no longer stated.
Type *ArrayType::widened() const {
    Type *elem  = elemType_->widened();
    bool changed = elem != elemType_;
    std::vector<ExtentForm> forms;
    forms.reserve(extents_.size());
    for (const ExtentForm &form : extents_) {
        // A constant or a named form is a fact widening removes; an unknown form stays.
        changed = changed || form.isConstant() || form.hasNames();
        forms.push_back(ExtentForm::unknown());
    }
    changed = changed || packName_ != nullptr;
    if (!changed) {
        return const_cast<ArrayType *>(this);
    }
    return make(elem, std::move(forms), anyRank_, {});
}

Type *ArrayType::literalOperandType() const {
    // A literal beside a numeric array is an element of it (`x * 2.0` with `x: f32[3]` is the f32
    // product), so it takes the element type (design principles 5.1). An array whose element type
    // nobody stated gives none, and a nested array is not one numeric array.
    const bool numeric = isNumericScalar(elemTypeCode_) || isComplex(elemTypeCode_);
    return numeric ? elemType_ : nullptr;
}

bool ArrayType::extentForms(std::vector<ExtentForm> &out) const {
    out = extents_;
    return true;
}

const Type *
ArrayType::withExtentValues(const ExtentValues &values, const ExtentPacks &packs) const {
    std::vector<ExtentForm> forms;
    forms.reserve(extents_.size());
    bool refined = false;
    for (const ExtentForm &form : extents_) {
        if (form.hasNames()) {
            if (auto value = form.evaluate(values)) {
                // A bound form that comes out negative does not describe a dimension (a
                // signature can state `2 * n - 10` and an argument can make it negative).
                // Keeping the form leaves the extent to the checked conversion at the call,
                // where the actual shape is reported, instead of failing while compiling.
                if (*value >= 0) {
                    forms.push_back(ExtentForm::constant(*value));
                    refined = true;
                    continue;
                }
            }
        }
        forms.push_back(form);
    }
    bool anyRank = anyRank_;
    if (anyRank_ && !packName().empty()) {
        auto it = packs.find(std::string(packName()));
        if (it != packs.end()) {
            // The pack is the dimensions the argument had after this type's prefix. An empty
            // pack leaves no dimension, so the type denotes its element type (a scalar): that
            // is what makes `first_row(xs: T[n, ..rest]): T[..rest]` return an element for a
            // rank-1 argument (plan Q1).
            forms.insert(forms.end(), it->second.begin(), it->second.end());
            anyRank = false;
            refined = true;
        }
    }
    if (!refined) {
        return this;
    }
    if (!anyRank && forms.empty()) {
        return elemType_;
    }
    return make(elemType_, std::move(forms), anyRank, anyRank ? packName() : std::string_view());
}

namespace camel::core::type {

const ArrayType *asArrayType(const Type *type) {
    if (type == nullptr || type->code() != TypeCode::Array) {
        return nullptr;
    }
    return static_cast<const ArrayType *>(type);
}

bool isNumericArrayType(const Type *type) {
    const ArrayType *array = asArrayType(type);
    return array != nullptr && isNumericScalarTypeCode(array->elemTypeCode());
}

bool isUntypedArrayType(const Type *type) {
    const ArrayType *array = asArrayType(type);
    if (array == nullptr || array->elemType() == nullptr) {
        return false;
    }
    // The element type is `Void`: nobody wrote one, so every position the type stands in fixes it.
    return array->elemType()->code() == TypeCode::Void;
}

bool isElementTypeUnstated(const Type *type) {
    if (type == nullptr) {
        return false;
    }
    // `Void` itself is the "no value" marker: `null`, an expression that gives nothing. An array
    // whose element type is `Void` is the empty literal in one or more dimensions.
    return type->code() == TypeCode::Void || isUntypedArrayType(type);
}

} // namespace camel::core::type
