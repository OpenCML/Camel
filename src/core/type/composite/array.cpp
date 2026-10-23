// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/composite/array.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/pure.h"
#include "camel/core/type/storage.h"
#include "camel/core/type/text.h"
#include "camel/utils/assert.h"
#include "camel/utils/log.h"

#include <algorithm>
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
    switch (code) {
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::Bool:
    case TypeCode::BFloat16:
    case TypeCode::Float16:
    case TypeCode::Complex64:
    case TypeCode::Complex128:
        return true;
    default:
        return false;
    }
}

bool isNumericScalarTypeCode(TypeCode code) { return isCompactElementType(code); }

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
    Type *elemType, std::vector<ExtentForm> extents, bool anyRank, const std::string *packName,
    bool numericUnknown)
    : CompositeType(TypeCode::Array), elemType_(elemType), elemTypeCode_(elemType->code()),
      extents_(std::move(extents)), anyRank_(anyRank), numericUnknown_(numericUnknown),
      packName_(packName) {}

ArrayType *ArrayType::make(
    Type *elemType, std::vector<ExtentForm> extents, bool anyRank, std::string_view pack,
    bool numericUnknown) {
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
    uintptr_t words[4] = {
        reinterpret_cast<uintptr_t>(elemType),
        static_cast<uintptr_t>((anyRank ? 1u : 0u) | (numericUnknown ? 2u : 0u)),
        reinterpret_cast<uintptr_t>(canonical),
        reinterpret_cast<uintptr_t>(packText),
    };
    const auto createObject = [&]() -> Type * {
        EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
            "ArrayType",
            "Allocating ArrayType: {} , size: {} bytes",
            typeCodeToString(elemType->code()),
            sizeof(ArrayType)));
        void *mem = allocTypeStorage<ArrayType>(TypeStorageKind::Array);
        return new (mem) ArrayType(elemType, std::move(extents), anyRank, packText, numericUnknown);
    };
    Type *type = TypeTable::instance().intern(
        TypeTable::Shape::Array, std::span<const uintptr_t>(words, 4), createObject);
    return static_cast<ArrayType *>(type);
}

ArrayType *ArrayType::create(Type *elemType) {
    if (!elemType) {
        elemType = Type::Void();
    }
    // One object per element type and shape (camel/core/type/intern.h): every `int[]` a program
    // writes, every array a rule infers, and every re-encoding of a graph share it.
    return make(elemType, {}, false, {}, false);
}

ArrayType *ArrayType::create(Type *elemType, std::span<const int64_t> extents) {
    std::vector<ExtentForm> forms;
    forms.reserve(extents.size());
    for (int64_t extent : extents) {
        forms.push_back(ExtentForm::constant(extent));
    }
    return make(elemType, std::move(forms), false, {}, false);
}

ArrayType *ArrayType::create(
    Type *elemType, std::span<const ExtentForm> extents, std::string_view pack) {
    return make(
        elemType, std::vector<ExtentForm>(extents.begin(), extents.end()), false, pack, false);
}

ArrayType *ArrayType::openRank(Type *elemType, std::span<const ExtentForm> prefix) {
    return make(
        elemType, std::vector<ExtentForm>(prefix.begin(), prefix.end()), true, {}, false);
}

ArrayType *ArrayType::numericUnknown(std::span<const int64_t> extents, std::string_view pack) {
    std::vector<ExtentForm> forms;
    forms.reserve(extents.size());
    for (int64_t extent : extents) {
        forms.push_back(ExtentForm::constant(extent));
    }
    // Bare `Tensor` is any rank; a shape argument fixes the rank.
    const bool anyRank = extents.empty() || !pack.empty();
    return make(Type::Void(), std::move(forms), anyRank, pack, true);
}

ArrayType *ArrayType::fromFactory(ArrayTypeFactory &factory) { return create(factory.elemType_); }

ArrayType *ArrayType::fromData(Type *elemType) { return create(elemType); }

std::string_view ArrayType::packName() const {
    return packName_ == nullptr ? std::string_view() : std::string_view(*packName_);
}

std::string ArrayType::shapeText() const { return shapeText(false); }

std::string ArrayType::tensorShapeText() const { return shapeText(true); }

std::string ArrayType::shapeText(bool tensorSpelling) const {
    // `T[?]` is printed `T[]`: the unknown single dimension is spelled with empty brackets, so
    // every `int[]` program keeps the text it had before dimensions moved into this type. The
    // tensor module spells it `[?]`.
    if (!tensorSpelling && !anyRank_ && extents_.size() == 1 && extents_[0].isUnknown()) {
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
    for (size_t i = 0; i < extents_.size(); ++i) {
        shape += (i ? "," : "");
        // Dimension names are signature metadata, not part of the type's identity: they are
        // not in the mangled name, as they are not in equality.
        shape += extents_[i].isConstant() ? std::to_string(extents_[i].constant()) : "?";
    }
    return shape;
}

ArrayType *ArrayType::withElementType(Type *elemType) const {
    if (!elemType || elemType == elemType_ || numericUnknown_) {
        // The element of the numeric-unknown array is not a type this array states; only its
        // shape is. Nothing to rewrite.
        return const_cast<ArrayType *>(this);
    }
    return make(elemType, extents_, anyRank_, packName(), numericUnknown_);
}

Type *ArrayType::resolve(const type_vec_t &typeList) const {
    ASSERT(typeList.size() > 0, "Type list is empty");
    ASSERT(!resolved(), "ArrayType is already resolved");
    if (numericUnknown_) {
        // The element of the numeric-unknown array is not fixed by construction; only its
        // shape is. An array literal never has this type.
        return const_cast<ArrayType *>(this);
    }

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
// common rank keeps the extents both sides state and makes the others unknown. The element
// type follows the same rule, and two different numeric element types become the numeric
// unknown array (`Tensor`), as the tensor module's unify did (plan section 3.3).
Type *ArrayType::unify(Type *other) const {
    if (other == nullptr || other->code() != TypeCode::Array) {
        return nullptr;
    }
    const auto *rhs = static_cast<const ArrayType *>(other);
    Type *elem     = nullptr;
    bool elemUnknown = false;
    if (numericUnknown_ || rhs->numericUnknown_) {
        const ArrayType *concrete = numericUnknown_ ? rhs : this;
        if (!concrete->numericUnknown_ &&
            !isNumericScalarTypeCode(concrete->elemTypeCode_)) {
            return nullptr;
        }
        elemUnknown = true;
    } else {
        elem = elemType_->unify(rhs->elemType_);
        if (elem == nullptr) {
            if (isNumericScalarTypeCode(elemTypeCode_) &&
                isNumericScalarTypeCode(rhs->elemTypeCode_)) {
                // Both are numbers but of different element types: no implicit conversion, so
                // the common type says only "numeric".
                elemUnknown = true;
            } else {
                return nullptr;
            }
        }
    }
    const bool sameSkeleton =
        anyRank_ == rhs->anyRank_ && extents_.size() == rhs->extents_.size();
    std::vector<ExtentForm> forms;
    bool anyRank = false;
    if (sameSkeleton) {
        forms.reserve(extents_.size());
        for (size_t i = 0; i < extents_.size(); ++i) {
            if (extents_[i].isConstant() && rhs->extents_[i].isConstant() &&
                extents_[i].constant() == rhs->extents_[i].constant()) {
                forms.push_back(extents_[i]);
            } else {
                forms.push_back(ExtentForm::unknown());
            }
        }
        anyRank = anyRank_;
    } else {
        // The ranks differ, so only "an array of this element type" is common; its rank is
        // unknown (`int[..]`), the counterpart of the tensor type's any-rank unify.
        anyRank = true;
    }
    const Type *elemType = elemUnknown ? Type::Void() : elem;
    return make(
        const_cast<Type *>(elemType), std::move(forms), anyRank,
        anyRank && sameSkeleton ? packName() : std::string_view(), elemUnknown);
}

// Converting a value to its own type is legal and does nothing (design principles 5.1). From a
// less precise array type, what the two static types already contradict is refused now; what
// the source leaves open is checked when the conversion runs (castSlotFrom): Unsafe.
CastSafety ArrayType::castSafetyFrom(Type *sourceType) const {
    if (sourceType == nullptr) {
        return CastSafety::Forbidden;
    }
    if (assignableFrom(sourceType)) {
        return CastSafety::Safe;
    }
    if (sourceType->code() != TypeCode::Array) {
        return CastSafety::Forbidden;
    }
    const auto *source = static_cast<const ArrayType *>(sourceType);
    // Element types: a numeric-unknown target accepts any numeric source; a concrete target
    // refuses a source it already knows cannot fit, and defers the numeric-unknown source to
    // the runtime check.
    if (numericUnknown_) {
        if (!source->numericUnknown_ && !isNumericScalarTypeCode(source->elemTypeCode_)) {
            return CastSafety::Forbidden;
        }
    } else if (elemTypeCode_ != TypeCode::Void && !source->numericUnknown_ &&
               !elemType_->assignableFrom(source->elemType_)) {
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

// The runtime half of the checked conversion: the shape (and, for the numeric-unknown array,
// the element type) is checked against the type the program declared. The diagnostic format is
// the one the tensor module used for the same check, so moving it here changes no message.
slot_t ArrayType::castSlotFrom(slot_t value, Type *sourceType) const {
    const CastSafety safety = castSafetyFrom(sourceType);
    ASSERT(safety != CastSafety::Forbidden, "castSlotFrom only allowed when the cast is not forbidden");
    if (safety == CastSafety::Safe) {
        return value;
    }
    auto *array      = camel::core::rtdata::fromSlot<Array *>(value);
    const auto refuse = [&](const std::string &what) {
        camel::core::error::throwRuntimeFault(
            camel::core::error::RuntimeDiag::RuntimeError,
            "conversion to '" + toString() + "' failed: " + what);
    };
    if (array == nullptr) {
        refuse("the value is null");
    }
    if (numericUnknown_) {
        if (!isNumericScalarTypeCode(array->dtype())) {
            refuse("the array has element type " + typeCodeToString(array->dtype()));
        }
    } else if (elemTypeCode_ != TypeCode::Void && array->dtype() != elemTypeCode_) {
        // The static element type is exact for every array but the numeric-unknown one; the
        // check costs one comparison and makes an unsafe conversion honest.
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

// Arrays have no element mutation, so they accept arrays of assignable element types
// (e.g. Tensor<float32>[] where Tensor[] is expected) whose shape this type permits.
bool ArrayType::assignableFrom(Type *sourceType) const {
    if (this == sourceType) {
        return true;
    }
    if (!sourceType || sourceType->code() != TypeCode::Array) {
        return false;
    }
    const auto *source = static_cast<const ArrayType *>(sourceType);
    if (numericUnknown_) {
        // Every numeric scalar is accepted; another numeric-unknown array is already numeric.
        if (!source->numericUnknown_ && !isNumericScalarTypeCode(source->elemTypeCode_)) {
            return false;
        }
    } else if (elemTypeCode_ != TypeCode::Void) {
        // A numeric-unknown source does not promise the exact element type this type states.
        if (source->numericUnknown_ || !elemType_->assignableFrom(source->elemType_)) {
            return false;
        }
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
    Type *elem = elemType_;
    bool changed = false;
    if (!numericUnknown_) {
        elem    = elemType_->widened();
        changed = elem != elemType_;
    }
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
    return make(elem, std::move(forms), anyRank_, {}, numericUnknown_);
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
    return make(elemType_, std::move(forms), anyRank, anyRank ? packName() : std::string_view(),
                numericUnknown_);
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
    if (array == nullptr) {
        return false;
    }
    // The numeric-unknown array has no element type to read; that it is numeric is the marker.
    return array->isNumericUnknown() || isNumericScalarTypeCode(array->elemTypeCode());
}

} // namespace camel::core::type