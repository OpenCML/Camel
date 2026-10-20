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
 * Created: Oct. 06, 2024
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/type/composite/array.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/pure.h"
#include "camel/core/type/storage.h"
#include "camel/core/type/text.h"
#include "camel/utils/assert.h"
#include "camel/utils/log.h"

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
    case TypeCode::Byte:
    case TypeCode::Complex64:
    case TypeCode::Complex128:
        return true;
    default:
        return false;
    }
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
    case TypeCode::Byte:
        return 1;
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

ArrayType::ArrayType(Type *elemType)
    : CompositeType(TypeCode::Array), elemType_(elemType), elemTypeCode_(elemType->code()) {}

ArrayType *ArrayType::create(Type *elemType) {
    if (!elemType) {
        elemType = Type::Void();
    }
    // One object per element type object (camel/core/type/intern.h): every `int[]` a program
    // writes, every array a rule infers, and every re-encoding of a graph share it.
    const auto make = [elemType]() -> Type * {
        EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
            "ArrayType",
            "Allocating ArrayType: {}[], size: {} bytes",
            elemType->toString(),
            sizeof(ArrayType)));
        void *mem = allocTypeStorage<ArrayType>(TypeStorageKind::Array);
        return new (mem) ArrayType(elemType);
    };
    const uintptr_t key = reinterpret_cast<uintptr_t>(elemType);
    Type *type          = TypeTable::instance().intern(
        TypeTable::Shape::Array,
        std::span<const uintptr_t>(&key, 1),
        make);
    return static_cast<ArrayType *>(type);
}

ArrayType *ArrayType::fromFactory(ArrayTypeFactory &factory) { return create(factory.elemType_); }

ArrayType *ArrayType::fromData(Type *elemType) { return create(elemType); }

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
        } else {
            throw DiagnosticBuilder::of(SemanticDiag::ElementTypeMismatch)
                .commit("Array", type->toString(), newElemType->toString());
        }
    }
    auto newArray = ArrayType::create(newElemType);
    ASSERT(newArray->resolved(), "ArrayType is not fully resolved");
    return newArray;
}

bool ArrayType::resolved() const { return elemType_->code() != TypeCode::Ref; }

string ArrayType::toString() const { return typeToString(this); }

std::string ArrayType::mangle() const { return typeMangle(this); }

Type *ArrayType::clone(bool deep /* = false */) const {
    // Interned: a shallow clone is this very object, which is what an immutable type allows.
    return create(deep ? elemType_->clone(true) : elemType_);
}

bool ArrayType::equals(Type *other) const { return typeEquals(this, other); }

// Converting a value to its own type is legal and does nothing (design principles 5.1): the
// rule compares structure, so `xs as int[]` passes for `xs: int[]` however each was written.
CastSafety ArrayType::castSafetyFrom(Type *sourceType) const {
    if (sourceType == nullptr)
        return CastSafety::Forbidden;
    return equals(sourceType) ? CastSafety::Safe : CastSafety::Forbidden;
}

// Every conversion castSafetyFrom admits (to the same type) keeps the value.
slot_t ArrayType::castSlotFrom(slot_t value, Type *sourceType) const {
    ASSERT(
        castSafetyFrom(sourceType) == CastSafety::Safe,
        "castSlotFrom only allowed when castSafetyFrom returns Safe");
    return value;
}

// Arrays have no element mutation, so they accept arrays of assignable element types
// (e.g. Tensor<float32>[] where Tensor[] is expected).
bool ArrayType::assignableFrom(Type *sourceType) const {
    if (this == sourceType) {
        return true;
    }
    if (!sourceType || sourceType->code() != TypeCode::Array) {
        return false;
    }
    return elemType_->assignableFrom(static_cast<const ArrayType &>(*sourceType).elemType_);
}

Type *ArrayType::widened() const {
    Type *elem = elemType_->widened();
    return elem == elemType_ ? const_cast<ArrayType *>(this) : ArrayType::create(elem);
}
