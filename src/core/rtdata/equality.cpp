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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
    case TypeCode::Byte:
    case TypeCode::String:
    case TypeCode::Complex64:
    case TypeCode::Complex128:
        return true;
    case TypeCode::Array:
        return isContentComparableImpl(static_cast<const ArrayType *>(type)->elemType(), active);
    case TypeCode::Tuple: {
        const auto *tuple = static_cast<const TupleType *>(type);
        if (tuple->isCell()) {
            return false;
        }
        if (tuple->isTagged()) {
            return isContentComparableImpl(tuple->payloadType(), active);
        }
        for (size_t i = 0; i < tuple->size(); ++i) {
            if (!isContentComparableImpl(tuple->typeAt(i), active)) {
                return false;
            }
        }
        return true;
    }
    case TypeCode::Struct: {
        const auto *st = static_cast<const StructType *>(type);
        for (size_t i = 0; i < st->size(); ++i) {
            if (!isContentComparableImpl(st->typeAt(i), active)) {
                return false;
            }
        }
        return true;
    }
    default:
        break;
    }
    if (const EnumType *enumType = asEnumType(type)) {
        // A recursive enum (`Tree<int>` holds `Tree<int>`) is comparable once what it is
        // built from is; the fields that name the enum itself are answered by this check.
        if (std::find(active.begin(), active.end(), type) != active.end()) {
            return true;
        }
        active.push_back(type);
        for (const EnumVariant &variant : enumType->variants()) {
            for (Type *fieldType : variant.fieldTypes) {
                // A field of a variant that does not determine an argument cannot hold a
                // value of this instance, so it does not decide whether values compare.
                if (containsEnumHole(fieldType)) {
                    continue;
                }
                if (!isContentComparableImpl(fieldType, active)) {
                    active.pop_back();
                    return false;
                }
            }
        }
        active.pop_back();
        return true;
    }
    if (const CollectionType *coll = asCollectionType(type)) {
        // A key type is comparable by construction; the values of a map must be.
        return !coll->isMap() || isContentComparableImpl(coll->valueType(), active);
    }
    return false;
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