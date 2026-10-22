// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/rtdata/equality.h"

#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/rtdata/variant.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/enum.h"

#include "collection_tree.h"

#include <algorithm>
#include <vector>

using namespace camel::core::type;

namespace camel::core::rtdata {

namespace {

bool isContentComparableImpl(const Type *type, std::vector<const Type *> &active) {
    if (type == nullptr) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::Bool:
    case TypeCode::String:
    case TypeCode::Complex64:
    case TypeCode::Complex128:
        return true;
    default:
        break;
    }
    // A recursive type is a cycle of type objects (`Node -> Node[] -> Node`). A type already
    // on the path is comparable once its parts are, and they are being checked: checking it
    // again would never end.
    if (std::find(active.begin(), active.end(), type) != active.end()) {
        return true;
    }
    const TypeCode code = type->code();
    const bool onPath   = code == TypeCode::Array || code == TypeCode::Tuple ||
                        code == TypeCode::Struct || asEnumType(type) != nullptr ||
                        asCollectionType(type) != nullptr;
    if (onPath) {
        active.push_back(type);
    }
    bool comparable = true;
    bool handled    = code == TypeCode::Array || code == TypeCode::Tuple ||
                   code == TypeCode::Struct;
    switch (code) {
    case TypeCode::Array:
        comparable =
            isContentComparableImpl(static_cast<const ArrayType *>(type)->elemType(), active);
        break;
    case TypeCode::Tuple: {
        const auto *tuple = static_cast<const TupleType *>(type);
        if (tuple->isCell()) {
            comparable = false;
        } else if (tuple->isTagged()) {
            comparable = isContentComparableImpl(tuple->payloadType(), active);
        } else {
            for (size_t i = 0; i < tuple->size() && comparable; ++i) {
                comparable = isContentComparableImpl(tuple->typeAt(i), active);
            }
        }
        break;
    }
    case TypeCode::Struct: {
        const auto *st = static_cast<const StructType *>(type);
        for (size_t i = 0; i < st->size() && comparable; ++i) {
            comparable = isContentComparableImpl(st->typeAt(i), active);
        }
        break;
    }
    default:
        break;
    }
    if (comparable) {
        if (const EnumType *enumType = asEnumType(type)) {
            handled = true;
            for (const EnumVariant &variant : enumType->variants()) {
                for (Type *fieldType : variant.fieldTypes) {
                    // A field of a variant that does not determine an argument cannot hold a
                    // value of this instance, so it does not decide whether values compare.
                    if (containsEnumHole(fieldType)) {
                        continue;
                    }
                    if (!isContentComparableImpl(fieldType, active)) {
                        comparable = false;
                        break;
                    }
                }
                if (!comparable) {
                    break;
                }
            }
        }
    }
    if (comparable) {
        if (const CollectionType *coll = asCollectionType(type)) {
            handled = true;
            // A key type is comparable by construction; the values of a map must be.
            comparable = !coll->isMap() || isContentComparableImpl(coll->valueType(), active);
        }
    }
    if (!handled) {
        comparable = false;
    }
    if (onPath) {
        active.pop_back();
    }
    return comparable;
}

} // namespace

bool isContentComparable(const Type *type) {
    std::vector<const Type *> active;
    return isContentComparableImpl(type, active);
}

namespace {

/// One pair still to compare. Equality follows the shape of the data, so a value a million
/// nodes deep would need a million native frames were it recursive; it is a work list instead,
/// and this is one item of it.
struct EqualPair {
    slot_t a;
    slot_t b;
    const Type *type;
};

/// Compares one pair. Scalars are answered here; a composite appends its parts to `pending` and
/// returns true. Iterating the parts here instead is what this function avoids: the parts of a
/// recursive enum are where the depth is.
bool comparePair(slot_t a, slot_t b, const Type *type, std::vector<EqualPair> &pending) {
    switch (type->code()) {
    case TypeCode::Float32:
        return fromSlot<Float32>(a) == fromSlot<Float32>(b);
    case TypeCode::Float64:
        return fromSlot<Float64>(a) == fromSlot<Float64>(b);
    case TypeCode::String: {
        if (a == b) {
            return true;
        }
        if (a == NullSlot || b == NullSlot) {
            return false;
        }
        return fromSlot<const String *>(a)->view() == fromSlot<const String *>(b)->view();
    }
    case TypeCode::Array: {
        if (a == NullSlot || b == NullSlot) {
            return a == b;
        }
        const auto *lhs      = fromSlot<const Array *>(a);
        const auto *rhs      = fromSlot<const Array *>(b);
        const Type *elemType = static_cast<const ArrayType *>(type)->elemType();
        if (lhs->size() != rhs->size()) {
            return false;
        }
        // Not short-cut by identity: an array that holds a NaN does not equal itself.
        for (size_t i = 0; i < lhs->size(); ++i) {
            pending.push_back({lhs->get<slot_t>(i), rhs->get<slot_t>(i), elemType});
        }
        return true;
    }
    case TypeCode::Tuple: {
        const auto *tuple = static_cast<const TupleType *>(type);
        if (tuple->isNullable()) {
            if (isNullValue(a) || isNullValue(b)) {
                return isNullValue(a) && isNullValue(b);
            }
            pending.push_back(
                {nullablePayload(a), nullablePayload(b), tuple->payloadType()});
            return true;
        }
        if (tuple->isFallible()) {
            const bool failedA = isErrorValue(a), failedB = isErrorValue(b);
            if (failedA != failedB) {
                return false;
            }
            if (failedA) {
                pending.push_back({fallibleError(a), fallibleError(b), errorType()});
            } else {
                pending.push_back(
                    {falliblePayload(a), falliblePayload(b), tuple->payloadType()});
            }
            return true;
        }
        if (a == NullSlot || b == NullSlot) {
            return a == b;
        }
        const auto *lhs = fromSlot<const Tuple *>(a);
        const auto *rhs = fromSlot<const Tuple *>(b);
        for (size_t i = 0; i < tuple->size(); ++i) {
            pending.push_back({lhs->get<slot_t>(i), rhs->get<slot_t>(i), tuple->typeAt(i)});
        }
        return true;
    }
    case TypeCode::Struct: {
        if (a == NullSlot || b == NullSlot) {
            return a == b;
        }
        const auto *st  = static_cast<const StructType *>(type);
        const auto *lhs = fromSlot<const Struct *>(a);
        const auto *rhs = fromSlot<const Struct *>(b);
        for (size_t i = 0; i < st->size(); ++i) {
            pending.push_back({lhs->get<slot_t>(i), rhs->get<slot_t>(i), st->typeAt(i)});
        }
        return true;
    }
    default:
        break;
    }
    if (!type->isGCTraced()) {
        return a == b;
    }
    if (a == NullSlot || b == NullSlot) {
        return a == b;
    }
    const auto *lhs = fromSlot<const Object *>(a);
    const auto *rhs = fromSlot<const Object *>(b);
    // An enum is a Variant: its fields are the parts that carry the depth. Comparing them here
    // keeps `a == b` on a million-node chain on one native frame.
    if (const EnumType *enumType = asEnumType(type)) {
        if (!isOfSameCls(lhs, rhs)) {
            return false;
        }
        const auto *lhsVariant = static_cast<const Variant *>(lhs);
        const auto *rhsVariant = static_cast<const Variant *>(rhs);
        if (lhsVariant->tag() != rhsVariant->tag() ||
            lhsVariant->fieldCount() != rhsVariant->fieldCount()) {
            return false;
        }
        const EnumVariant &variant = enumType->variant(lhsVariant->tag());
        for (size_t i = 0; i < lhsVariant->fieldCount(); ++i) {
            pending.push_back(
                {lhsVariant->get(i), rhsVariant->get(i), variant.fieldTypes[i]});
        }
        return true;
    }
    // A map or a set: the entries in key order, exactly as Table::equals compares them. Two
    // tables with the same entries may differ in shape, so the comparison is entry by entry;
    // a value that is itself a composite is compared later, on the work list.
    if (const CollectionType *collType = asCollectionType(type)) {
        if (!isOfSameCls(lhs, rhs)) {
            return false;
        }
        const auto *lhsTable = static_cast<const Table *>(lhs);
        const auto *rhsTable = static_cast<const Table *>(rhs);
        if (lhsTable == rhsTable) {
            return true;
        }
        if (lhsTable->size() != rhsTable->size()) {
            return false;
        }
        const Type *keyType   = collType->keyType();
        const Type *valueType = collType->valueType();
        table::detail::Cursor left(lhsTable, collType), right(rhsTable, collType);
        for (; !left.done(); left.next(), right.next()) {
            if (compareKeys(left.key(), right.key(), keyType) != 0) {
                return false;
            }
            if (!collType->isMap()) {
                continue;
            }
            const slot_t leftValue = left.value(), rightValue = right.value();
            // As Table::equals: a value that is the same object is the same value. (This is
            // the map rule; an array's own `==` does not short-cut identity, so that an array
            // holding a NaN does not equal itself.)
            if (leftValue == rightValue) {
                continue;
            }
            if (leftValue == NullSlot || rightValue == NullSlot) {
                return false;
            }
            pending.push_back({leftValue, rightValue, valueType});
        }
        return true;
    }
    return lhs->equals(rhs, type, true);
}

} // namespace

bool valuesEqualByContent(slot_t a, slot_t b, const Type *type) {
    std::vector<EqualPair> pending;
    pending.push_back({a, b, type});
    while (!pending.empty()) {
        const EqualPair pair = pending.back();
        pending.pop_back();
        if (!comparePair(pair.a, pair.b, pair.type, pending)) {
            return false;
        }
    }
    return true;
}

} // namespace camel::core::rtdata