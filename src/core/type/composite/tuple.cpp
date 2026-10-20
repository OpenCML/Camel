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
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/type/composite/tuple.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/storage.h"
#include "camel/core/type/text.h"
#include "camel/core/type/typevar.h"
#include "camel/utils/assert.h"
#include "camel/utils/log.h"
#include "camel/utils/type.h"

#include <functional>
#include <mutex>

namespace mm = camel::core::mm;
using namespace std;
using namespace camel::core::type;

namespace {

bool isRuntimeReferenceSlot(Type *type) {
    if (!type) {
        return false;
    }
    return type->isGCTraced() || type->code() == TypeCode::Ref;
}

TupleTypeLayout computeLayout(size_t size, size_t refCount) {
    size_t typesSize     = size * sizeof(Type *);
    size_t typeCodesSize = size * sizeof(TypeCode);
    size_t refsSize      = refCount * sizeof(size_t);
    size_t aTypes        = mm::alignUp(typesSize, alignof(TypeCode));
    size_t aTypeCodes    = mm::alignUp(typeCodesSize, alignof(size_t));
    size_t totalSize     = sizeof(TupleType) + aTypes + aTypeCodes + refsSize;
    return TupleTypeLayout{
        .totalSize            = totalSize,
        .size                 = size,
        .refCount             = refCount,
        .alignedTypesSize     = aTypes,
        .alignedTypeCodesSize = aTypeCodes,
    };
}

} // namespace

TupleType *TupleTypeFactory::build() { return TupleType::fromFactory(*this); }

TupleType::TupleType(const TupleTypeLayout &layout, const std::vector<Type *> &types)
    : CompositeType(TypeCode::Tuple), size_(layout.size), refCount_(layout.refCount) {
    auto p        = layout.ptrs(data_);
    typesPtr_     = p.types;
    typeCodesPtr_ = p.typeCodes;
    refsPtr_      = p.refs;
    size_t refIdx = 0;
    for (size_t i = 0; i < size_; ++i) {
        Type *t        = types[i];
        p.types[i]     = t;
        p.typeCodes[i] = t->code();
        if (isRuntimeReferenceSlot(t)) {
            p.refs[refIdx++] = i;
        }
    }
}

TupleType *TupleType::allocate(const std::vector<Type *> &types) {
    size_t refCount = 0;
    for (Type *t : types) {
        if (isRuntimeReferenceSlot(t))
            ++refCount;
    }
    TupleTypeLayout layout = computeLayout(types.size(), refCount);
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "TupleType",
        "Allocating TupleType: size={}, refCount={}, totalSize: {} bytes",
        layout.size,
        layout.refCount,
        layout.totalSize));
    void *mem = allocTypeStorage<TupleType>(TypeStorageKind::Tuple, layout.totalSize);
    return new (mem) TupleType(layout, types);
}

TupleType *TupleType::create() { return create(std::vector<Type *>{}); }

TupleType *TupleType::create(const std::vector<Type *> &types) {
    return create(std::span<Type *const>(types.data(), types.size()));
}

TupleType *TupleType::create(std::vector<Type *> &&types) { return create(types); }

TupleType *TupleType::create(std::span<Type *const> types) {
    // One object per sequence of element type objects (see intern.h). Frame and static-area
    // layouts are asked for again at every commit of a rewrite, for graphs it did not change
    // too, and the many small graphs of a program (branch arms above all) share a handful of
    // them; a tuple type a program writes is shared by every place that writes it.
    static_assert(sizeof(Type *) == sizeof(uintptr_t));
    const std::span<const uintptr_t> key(
        reinterpret_cast<const uintptr_t *>(types.data()),
        types.size());
    Type *type = TypeTable::instance().intern(TypeTable::Shape::Tuple, key, [&]() -> Type * {
        return allocate(std::vector<Type *>(types.begin(), types.end()));
    });
    return static_cast<TupleType *>(type);
}

TupleType *TupleType::layout(std::span<Type *const> slotTypes) { return create(slotTypes); }

namespace {

size_t runtimeReferenceCount(std::span<Type *const> types) {
    size_t refCount = 0;
    for (Type *t : types) {
        if (isRuntimeReferenceSlot(t)) {
            ++refCount;
        }
    }
    return refCount;
}

} // namespace

size_t TupleType::frameLayoutBytes(std::span<Type *const> slotTypes) {
    return mm::alignUp(
        computeLayout(slotTypes.size(), runtimeReferenceCount(slotTypes)).totalSize,
        alignof(TupleType));
}

TupleType *TupleType::constructFrameLayout(void *memory, std::span<Type *const> slotTypes) {
    ASSERT(memory != nullptr, "A frame layout needs memory to be built in.");
    const TupleTypeLayout layout =
        computeLayout(slotTypes.size(), runtimeReferenceCount(slotTypes));
    return new (memory) TupleType(layout, std::vector<Type *>(slotTypes.begin(), slotTypes.end()));
}

TupleType *TupleType::cell(Type *valueType) {
    ASSERT(valueType != nullptr, "A variable cell needs a value type.");
    static std::mutex mutex;
    static std::vector<TupleType *> cells;
    std::lock_guard lock(mutex);
    for (TupleType *known : cells) {
        if (known->typeAt(0) == valueType || known->typeAt(0)->equals(valueType)) {
            return known;
        }
    }
    // Cells are shared by the equality of their value types, a wider rule than the type
    // table's key, so they keep their own list and only take a TypeId from the table.
    TupleType *type = allocate(std::vector<Type *>{valueType});
    type->cell_     = true;
    TypeTable::instance().adopt(type);
    cells.push_back(type);
    return type;
}

namespace {

/// One object per payload type and form, as for cells.
TupleType *internTagged(
    std::vector<TupleType *> &known, Type *payloadType, const std::function<TupleType *()> &make) {
    // Occurrences of one type variable are equal whether or not they bind (`typeas T` and
    // `T`), so a type written with variables is never shared: sharing would drop the mark.
    if (containsTypeVar(payloadType)) {
        return make();
    }
    for (TupleType *type : known) {
        if (type->typeAt(0) == payloadType || type->typeAt(0)->equals(payloadType)) {
            return type;
        }
    }
    TupleType *type = make();
    known.push_back(type);
    return type;
}

} // namespace

TupleType *TupleType::nullable(Type *payloadType) {
    ASSERT(payloadType != nullptr, "A nullable type needs a payload type.");
    static std::mutex mutex;
    static std::vector<TupleType *> known;
    std::lock_guard lock(mutex);
    return internTagged(known, payloadType, [&] {
        TupleType *type = allocate(std::vector<Type *>{payloadType});
        type->tag_      = Tag::Nullable;
        TypeTable::instance().adopt(type);
        return type;
    });
}

TupleType *TupleType::fallible(Type *payloadType) {
    ASSERT(payloadType != nullptr, "A fallible type needs a payload type.");
    static std::mutex mutex;
    static std::vector<TupleType *> known;
    std::lock_guard lock(mutex);
    return internTagged(known, payloadType, [&] {
        TupleType *type = allocate(std::vector<Type *>{payloadType, errorType()});
        type->tag_      = Tag::Fallible;
        TypeTable::instance().adopt(type);
        return type;
    });
}

StructType *camel::core::type::errorType() {
    static StructType *type = [] {
        StructTypeFactory factory;
        factory.add("kind", Type::String());
        factory.add("message", Type::String());
        factory.add("location", Type::String());
        return factory.build();
    }();
    return type;
}

size_t camel::core::type::errorFieldIndex(std::string_view name) {
    const auto index = errorType()->findField(name);
    ASSERT(index.has_value(), "Unknown field of the built-in error structure.");
    return *index;
}

TupleType *TupleType::fromFactory(TupleTypeFactory &factory) { return create(factory.types_); }

TupleType *TupleType::slice(size_t start, size_t end) const {
    ASSERT(start <= end && end <= size_, "TupleType slice indices out of range");
    std::vector<Type *> slicedTypes;
    slicedTypes.reserve(end - start);
    for (size_t i = start; i < end; ++i) {
        slicedTypes.push_back(typesPtr_[i]);
    }
    return TupleType::create(std::move(slicedTypes));
}

Type *TupleType::resolve(const type_vec_t &typeList) const {
    ASSERT(typeList.size() > 0, "Type list is empty");
    ASSERT(!resolved(), "TupleType is already resolved");
    std::vector<Type *> types;
    types.reserve(size_);
    for (size_t i = 0; i < size_; ++i) {
        types.push_back(typesPtr_[i]);
    }
    size_t i = 0, j = 0;
    while (i < types.size() && j < typeList.size()) {
        if (types[i]->code() == TypeCode::Ref) {
            types[i] = typeList[j];
            j++;
        }
        i++;
    }
    ASSERT(j == typeList.size(), "Not all types in typeList are used");
    auto newTuple = TupleType::create(std::move(types));
    ASSERT(newTuple->resolved(), "TupleType is not fully resolved");
    return newTuple;
}

bool TupleType::resolved() const {
    for (size_t i = 0; i < size_; ++i) {
        if (typesPtr_[i]->code() == TypeCode::Ref) {
            return false;
        }
    }
    return true;
}

string TupleType::toString() const { return typeToString(this); }

std::string TupleType::mangle() const { return typeMangle(this); }

Type *TupleType::clone(bool deep /* = false */) const {
    if (cell_ || isTagged()) {
        return const_cast<TupleType *>(this);
    }
    std::vector<Type *> clonedTypes;
    clonedTypes.reserve(size_);
    if (deep) {
        for (size_t i = 0; i < size_; ++i) {
            clonedTypes.push_back(typesPtr_[i]->clone(true));
        }
    } else {
        for (size_t i = 0; i < size_; ++i) {
            clonedTypes.push_back(typesPtr_[i]);
        }
    }
    return TupleType::create(std::move(clonedTypes));
}

bool TupleType::equals(Type *other) const { return typeEquals(this, other); }

CastSafety TupleType::castSafetyFrom(Type *sourceType) const {
    // Converting a value to its own type is legal and does nothing (design principles 5.1):
    // decided by structure, so `t as (int, int)` passes for `t: (int, int)`.
    if (equals(sourceType))
        return CastSafety::Safe;
    if (isTagged() && sourceType != nullptr) {
        // `null` is the literal of every nullable type.
        if (isNullable() && sourceType->code() == TypeCode::Void) {
            return CastSafety::Safe;
        }
        // A value enters the form: `v as T?`, `v as T!` (also a tagged one: `T?` into `T?!`).
        if (typesPtr_[0]->assignableFrom(sourceType)) {
            return CastSafety::Safe;
        }
        if (TupleType *source = asTagged(sourceType)) {
            // Retyping a failed `U!` as `T!` is the error arm of `?->` and the last step of
            // `throw`: sound only where the compiler knows the value has failed, so it is
            // not a conversion a program may write.
            return source->tag_ == tag_ && isFallible() ? CastSafety::Unsafe
                                                        : CastSafety::Forbidden;
        }
    }
    return CastSafety::Forbidden;
}

slot_t TupleType::castSlotFrom(slot_t value, Type *sourceType) const {
    if (sourceType == nullptr) {
        return Type::castSlotFrom(value, sourceType);
    }
    if (equals(sourceType)) {
        return value; // a conversion to the value's own type does nothing
    }
    if (!isTagged()) {
        return Type::castSlotFrom(value, sourceType);
    }
    if (isNullable() && sourceType->code() == TypeCode::Void) {
        return NullSlot;
    }
    if (asTagged(sourceType) != nullptr && !typesPtr_[0]->assignableFrom(sourceType)) {
        // A failed value retyped (see castSafetyFrom): the same object.
        return value;
    }
    ::Tuple *box = ::Tuple::create(size_, mm::autoSpace());
    box->set<slot_t>(0, value, this);
    return camel::core::rtdata::toSlot(box);
}

// Elements are immutable, so a tuple accepts any same-length tuple of assignable element types.
bool TupleType::assignableFrom(Type *sourceType) const {
    if (this == sourceType) {
        return true;
    }
    if (!sourceType || sourceType->code() != TypeCode::Tuple) {
        return false;
    }
    const TupleType &other = static_cast<const TupleType &>(*sourceType);
    if (size_ != other.size_ || cell_ != other.cell_ || tag_ != other.tag_) {
        return false;
    }
    if (cell_) {
        // A cell is written through every reference to it: the content types must be equal.
        return typesPtr_[0]->equals(other.typesPtr_[0]);
    }
    for (size_t i = 0; i < size_; i++) {
        if (!typesPtr_[i]->assignableFrom(other.typesPtr_[i])) {
            return false;
        }
    }
    return true;
}

Type *TupleType::widened() const {
    if (cell_) {
        return const_cast<TupleType *>(this);
    }
    if (isTagged()) {
        Type *payload = typesPtr_[0]->widened();
        if (payload == typesPtr_[0]) {
            return const_cast<TupleType *>(this);
        }
        return isNullable() ? nullable(payload) : fallible(payload);
    }
    std::vector<Type *> types;
    bool changed = false;
    for (size_t i = 0; i < size_; i++) {
        types.push_back(typesPtr_[i]->widened());
        changed = changed || types.back() != typesPtr_[i];
    }
    return changed ? TupleType::create(std::move(types)) : const_cast<TupleType *>(this);
}

namespace {

bool isEmptyArrayLiteralType(Type *type) {
    return type != nullptr && type->code() == TypeCode::Array &&
           static_cast<ArrayType *>(type)->elemType()->code() == TypeCode::Void;
}

} // namespace

bool camel::core::type::entersImplicitly(Type *target, Type *source) {
    if (target == nullptr || source == nullptr) {
        return false;
    }
    // `void` (and so `void[]`, the type of `[]`) is assignable from anything; nothing enters
    // "no value", and nothing but `[]` is a `[]`.
    if (target->code() == TypeCode::Void || isEmptyArrayLiteralType(target)) {
        return target->equals(source);
    }
    if (target->assignableFrom(source)) {
        return true;
    }
    if (TupleType *tagged = asTagged(target)) {
        if (tagged->isNullable() && source->code() == TypeCode::Void) {
            return true; // null
        }
        // A tagged value of the same form with another payload is not put into the form
        // again: `int?` does not enter `float?`, and never `int??`.
        if (TupleType *from = asTagged(source);
            from != nullptr && from->isNullable() == tagged->isNullable()) {
            return false;
        }
        return entersImplicitly(tagged->payloadType(), source);
    }
    return target->code() == TypeCode::Array && isEmptyArrayLiteralType(source);
}

Type *camel::core::type::joinTypes(Type *a, Type *b) {
    if (a == nullptr || b == nullptr) {
        return nullptr;
    }
    if (Type *unified = a->unify(b)) {
        return unified;
    }
    // A value and `null`: the value's type, nullable. (Asked first: `void` is assignable
    // from anything, which is not what joining with `null` means.)
    // Design principles 3.7: an arm without a value (`void`: the literal `null`, a call of
    // a function that returns nothing, an empty block) and an arm of type `T` make the
    // branch a `T?`; the arm without a value gives `null`.
    if (a->code() == TypeCode::Void || b->code() == TypeCode::Void) {
        Type *value = a->code() == TypeCode::Void ? b : a;
        return asNullable(value) != nullptr ? value : TupleType::nullable(value);
    }
    if (entersImplicitly(a, b)) {
        return a;
    }
    if (entersImplicitly(b, a)) {
        return b;
    }
    return nullptr;
}
