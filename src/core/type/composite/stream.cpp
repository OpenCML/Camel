// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/composite/stream.h"

#include "camel/core/mm.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/storage.h"
#include "camel/core/type/text.h"
#include "camel/utils/assert.h"

#include <span>

namespace camel::core::type {

StreamType *StreamType::make(Type *elemType) {
    if (elemType == nullptr) {
        elemType = Type::Void();
    }
    // The element type is the whole key: `int..` written once and `int..` reached through a
    // `type` alias, a generic instantiation or a graph are one object, which is what makes
    // equality, overload selection and `#impl` signature agreement agree with each other.
    Type *plainElement       = TypeTable::instance().canonical(elemType);
    const uintptr_t words[1] = {typeKeyWord(elemType)};
    const uintptr_t plain[1] = {typeKeyWord(plainElement)};
    const auto createObject  = [&](Type *element) -> Type * {
        void *mem = allocTypeStorage<StreamType>(TypeStorageKind::Stream);
        return new (mem) StreamType(element);
    };
    Type *type = TypeTable::instance().intern(
        TypeTable::Shape::Stream, std::span<const uintptr_t>(words, 1),
        [&]() -> Type * { return createObject(elemType); },
        TypeTable::Canonical{
            std::span<const uintptr_t>(plain, 1),
            [&]() -> Type * { return createObject(plainElement); }});
    return static_cast<StreamType *>(type);
}

StreamType *StreamType::create(Type *elemType) { return make(elemType); }

// A stream is never written as a literal, so resolve() is reached only by a type that stands
// where an element was still unknown (`Ref`). There is no element list to join: the type is
// what it is.
Type *StreamType::resolve(const type_vec_t &typeList) const {
    (void)typeList;
    return const_cast<StreamType *>(this);
}

bool StreamType::resolved() const { return elemType_->code() != TypeCode::Ref; }

std::string StreamType::toString() const { return typeToString(this); }

std::string StreamType::mangle() const { return typeMangle(this); }

Type *StreamType::clone(bool deep /* = false */) const {
    // Interned: a shallow clone is this very object, which is what an immutable type allows.
    if (!deep) {
        return const_cast<StreamType *>(this);
    }
    return create(elemType_->clone(true));
}

bool StreamType::equals(Type *other) const { return typeEquals(this, other); }

// Least common type at a branch join: two streams of element types that have a common type are
// streams of it; anything else has no common type.
Type *StreamType::unify(Type *other) const {
    if (other == nullptr || other->code() != TypeCode::Stream) {
        return nullptr;
    }
    Type *joined = elemType_->unify(static_cast<const StreamType *>(other)->elemType_);
    return joined == nullptr ? nullptr : create(joined);
}

Type *StreamType::widened() const { return create(elemType_->widened()); }

// A stream converts to nothing but its own type: what turns some other value into a stream (a
// source, a generator, `tee`) is an operator of its own, not a conversion of a value that is
// already a stream.
CastSafety StreamType::castSafetyFrom(Type *sourceType) const {
    if (sourceType == nullptr) {
        return CastSafety::Forbidden;
    }
    return assignableFrom(sourceType) ? CastSafety::Safe : CastSafety::Forbidden;
}

bool StreamType::assignableFrom(Type *sourceType) const {
    if (sourceType == nullptr) {
        return false;
    }
    if (sourceType == this) {
        return true;
    }
    return sourceType->code() == TypeCode::Stream && typeEquals(this, sourceType);
}

} // namespace camel::core::type