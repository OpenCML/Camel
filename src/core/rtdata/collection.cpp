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
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The run-time object of Map and Set: see camel/core/rtdata/collection.h.
 *
 * Every function that builds a table allocates it, fills it and returns it without calling
 * anything that could run a collection in between: an allocation is not a safepoint (note 23,
 * "Phase 2 Safepoint Contract"), so the slots copied from the arguments stay valid.
 */

#include "camel/core/rtdata/collection.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tuple.h"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <vector>

namespace camel::core::rtdata {

namespace {

using type::CollectionType;
using type::MapType;
using type::SetType;
using type::Type;
using type::TypeCode;

const CollectionType *collectionTypeOf(const Type *type) {
    const CollectionType *result = type::asCollectionType(type);
    ASSERT(result != nullptr, "A Table needs its Map or Set type.");
    return result;
}

mm::IAllocator &heap() { return mm::autoSpace(); }

/// The barrier for every reference `t` holds. Called once a new table is filled.
void barrierAll(Table *t, const CollectionType *type) {
    const Type *keyType   = type->keyType();
    const Type *valueType = type->valueType();
    if (keyType->isGCTraced()) {
        for (size_t i = 0; i < t->size(); ++i) {
            mm::writeBarrier(t, type, t->keys()[i], keyType);
        }
    }
    if (valueType != nullptr && valueType->isGCTraced()) {
        for (size_t i = 0; i < t->size(); ++i) {
            mm::writeBarrier(t, type, t->values()[i], valueType);
        }
    }
}

Table *newTable(size_t size, const CollectionType *type) {
    return Table::create(size, type->isMap() ? 2 * size : size, heap());
}

/// The first index of `t` whose key is not before `key`.
size_t lowerBound(const Table *t, slot_t key, const Type *keyType) {
    size_t lo = 0, hi = t->size();
    const slot_t *keys = t->keys();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (compareKeys(keys[mid], key, keyType) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/// Copies entry `from` of `src` to entry `to` of `dst`.
inline void copyEntry(Table *dst, size_t to, const Table *src, size_t from, bool map) {
    dst->keys()[to] = src->keys()[from];
    if (map) {
        dst->values()[to] = src->values()[from];
    }
}

/// The table a merge left in `scratch`: its first `count` entries. `scratch` was allocated for
/// the largest possible result; tables are never resized in place, so a shorter result is a
/// second, exact allocation (the values of `scratch` sit after all of its key slots).
Table *finished(Table *scratch, size_t count, const CollectionType *type) {
    if (count == scratch->size()) {
        barrierAll(scratch, type);
        return scratch;
    }
    Table *result = newTable(count, type);
    std::memcpy(result->keys(), scratch->keys(), count * sizeof(slot_t));
    if (type->isMap()) {
        std::memcpy(result->values(), scratch->values(), count * sizeof(slot_t));
    }
    barrierAll(result, type);
    return result;
}

} // namespace

// ---- Table ----

Table *Table::create(size_t size, size_t slotCount, mm::IAllocator &allocator) {
    ASSERT(slotCount == size || slotCount == 2 * size, "A table has one or two slots per entry.");
    void *memory = allocator.alloc(sizeof(Table) + slotCount * sizeof(slot_t), alignof(Table));
    if (!memory) {
        throw std::bad_alloc();
    }
    Table *t = new (memory) Table(size);
    std::fill(t->data_, t->data_ + slotCount, NullSlot);
    return t;
}

bool Table::equals(const Object *other, const type::Type *type, bool deep) const {
    if (this == other) {
        return true;
    }
    if (!isOfSameCls(this, other)) {
        return false;
    }
    const CollectionType *collType = collectionTypeOf(type);
    const Table *rhs               = static_cast<const Table *>(other);
    if (size_ != rhs->size_) {
        return false;
    }
    const Type *keyType = collType->keyType();
    for (size_t i = 0; i < size_; ++i) {
        if (compareKeys(keys()[i], rhs->keys()[i], keyType) != 0) {
            return false;
        }
    }
    if (!collType->isMap()) {
        return true;
    }
    const Type *valueType = collType->valueType();
    if (!deep) {
        return std::memcmp(values(), rhs->values(), size_ * sizeof(slot_t)) == 0;
    }
    for (size_t i = 0; i < size_; ++i) {
        if (!valuesEqual(values()[i], rhs->values()[i], valueType)) {
            return false;
        }
    }
    return true;
}

Object *Table::clone(mm::IAllocator &allocator, const type::Type *type, bool deep) const {
    const CollectionType *collType = collectionTypeOf(type);
    const size_t slotCount         = collType->isMap() ? 2 * size_ : size_;
    Table *copy                    = Table::create(size_, slotCount, allocator);
    std::memcpy(copy->data_, data_, slotCount * sizeof(slot_t));
    if (deep) {
        const auto cloneSlots = [&](slot_t *slots, const Type *slotType) {
            if (slotType == nullptr || !slotType->isGCTraced()) {
                return;
            }
            for (size_t i = 0; i < size_; ++i) {
                if (Object *ref = fromSlot<Object *>(slots[i])) {
                    slots[i] = toSlot(ref->clone(allocator, slotType, true));
                }
            }
        };
        cloneSlots(copy->keys(), collType->keyType());
        if (collType->isMap()) {
            cloneSlots(copy->values(), collType->valueType());
        }
    }
    barrierAll(copy, collType);
    return copy;
}

// DESIGN-QUESTION(map-equality-print): `{k: v, ...}` for a map, `{x, ...}` for a set, in key
// order; strings unquoted, as inside an array or a struct.
void Table::print(std::ostream &os, const type::Type *type) const {
    const CollectionType *collType = type::asCollectionType(type);
    if (collType == nullptr) {
        os << "<" << (type ? type->toString() : std::string("collection")) << ">";
        return;
    }
    os << "{";
    for (size_t i = 0; i < size_; ++i) {
        if (i > 0) {
            os << ", ";
        }
        printSlot(os, keys()[i], collType->keyType());
        if (collType->isMap()) {
            os << ": ";
            printSlot(os, values()[i], collType->valueType());
        }
    }
    os << "}";
}

// Nothing derives from the table's address.
void Table::onMoved() {}

void Table::updateRefs(const RefRelocator &relocate, const type::Type *type) {
    const CollectionType *collType = type::asCollectionType(type);
    if (collType == nullptr) {
        return;
    }
    const auto trace = [&](slot_t *slots, const Type *slotType, std::string_view name) {
        if (slotType == nullptr || !slotType->isGCTraced()) {
            return;
        }
        Object **refs = reinterpret_cast<Object **>(slots);
        for (size_t i = 0; i < size_; ++i) {
            if (Object *&ref = refs[i]) {
                ref = relocate(
                    ref,
                    slotType,
                    RefTraceInfo{
                        .owner     = this,
                        .ownerType = type,
                        .slotType  = slotType,
                        .ownerKind = "Table",
                        .slotName  = name,
                        .slotIndex = i,
                    });
            }
        }
    };
    trace(keys(), collType->keyType(), "key");
    if (collType->isMap()) {
        trace(values(), collType->valueType(), "value");
    }
}

// ---- Order and equality ----

int compareKeys(slot_t a, slot_t b, const type::Type *keyType) {
    const auto three = [](auto x, auto y) { return x < y ? -1 : (y < x ? 1 : 0); };
    switch (keyType->code()) {
    case TypeCode::Int64:
        return three(fromSlot<Int64>(a), fromSlot<Int64>(b));
    case TypeCode::Int32:
        return three(fromSlot<Int32>(a), fromSlot<Int32>(b));
    case TypeCode::Bool:
        return three(fromSlot<Bool>(a), fromSlot<Bool>(b));
    case TypeCode::String: {
        const int order = fromSlot<String *>(a)->compare(fromSlot<String *>(b));
        return order < 0 ? -1 : (order > 0 ? 1 : 0);
    }
    case TypeCode::Tuple: {
        const auto *tupleType = static_cast<const type::TupleType *>(keyType);
        const Tuple *lhs      = fromSlot<Tuple *>(a);
        const Tuple *rhs      = fromSlot<Tuple *>(b);
        for (size_t i = 0; i < tupleType->size(); ++i) {
            const int order =
                compareKeys(lhs->get<slot_t>(i), rhs->get<slot_t>(i), tupleType->typeAt(i));
            if (order != 0) {
                return order;
            }
        }
        return 0;
    }
    default:
        ASSERT(false, "Not a key type: " + keyType->toString());
        return three(a, b);
    }
}

bool valuesEqual(slot_t a, slot_t b, const type::Type *type) {
    if (type->isGCTraced()) {
        const Object *lhs = fromSlot<Object *>(a);
        const Object *rhs = fromSlot<Object *>(b);
        if (lhs == rhs) {
            return true;
        }
        if (lhs == nullptr || rhs == nullptr) {
            return false;
        }
        return lhs->equals(rhs, type, true);
    }
    switch (type->code()) {
    case TypeCode::Float64:
        return fromSlot<Float64>(a) == fromSlot<Float64>(b);
    case TypeCode::Float32:
        return fromSlot<Float32>(a) == fromSlot<Float32>(b);
    case TypeCode::Int32:
        return fromSlot<Int32>(a) == fromSlot<Int32>(b);
    case TypeCode::Bool:
        return fromSlot<Bool>(a) == fromSlot<Bool>(b);
    default:
        return a == b;
    }
}

// ---- Operations ----

namespace table {

ptrdiff_t find(const Table *t, slot_t key, const type::Type *keyType) {
    const size_t at = lowerBound(t, key, keyType);
    if (at < t->size() && compareKeys(t->keys()[at], key, keyType) == 0) {
        return static_cast<ptrdiff_t>(at);
    }
    return -1;
}

Table *empty(const type::CollectionType *type) { return newTable(0, type); }

Table *fromArray(const Array *array, const type::CollectionType *type) {
    const size_t n      = array ? array->size() : 0;
    const bool map      = type->isMap();
    const Type *keyType = type->keyType();
    if (n == 0) {
        return newTable(0, type);
    }
    // The key and the value of entry i of the source.
    const slot_t *source = array->data();
    const auto keyAt     = [&](size_t i) {
        return map ? fromSlot<Tuple *>(source[i])->get<slot_t>(0) : source[i];
    };
    const auto valueAt = [&](size_t i) { return fromSlot<Tuple *>(source[i])->get<slot_t>(1); };

    // The source positions in key order; equal keys stay in source order (stable), so the
    // last of a run of equal keys is the later entry, which is the one kept.
    std::vector<uint32_t> order(n);
    std::iota(order.begin(), order.end(), 0u);
    bool ascending = true;
    for (size_t i = 1; i < n && ascending; ++i) {
        ascending = compareKeys(keyAt(i - 1), keyAt(i), keyType) < 0;
    }
    size_t count = n;
    if (!ascending) {
        std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
            return compareKeys(keyAt(a), keyAt(b), keyType) < 0;
        });
        count = 0;
        for (size_t i = 0; i < n; ++i) {
            const bool lastOfRun =
                i + 1 == n || compareKeys(keyAt(order[i]), keyAt(order[i + 1]), keyType) != 0;
            if (lastOfRun) {
                order[count++] = order[i];
            }
        }
    }
    Table *result = newTable(count, type);
    for (size_t i = 0; i < count; ++i) {
        result->keys()[i] = keyAt(order[i]);
        if (map) {
            result->values()[i] = valueAt(order[i]);
        }
    }
    barrierAll(result, type);
    return result;
}

namespace {

/// `t` with (`key`, `value`) put in; `value` is ignored for a set.
Table *insert(const Table *t, slot_t key, slot_t value, const CollectionType *type) {
    const bool map      = type->isMap();
    const Type *keyType = type->keyType();
    const size_t n      = t->size();
    const size_t at     = lowerBound(t, key, keyType);
    const bool present  = at < n && compareKeys(t->keys()[at], key, keyType) == 0;
    if (present && !map) {
        return const_cast<Table *>(t);
    }
    const size_t tail = n - at - (present ? 1 : 0);
    const size_t from = at + (present ? 1 : 0);
    Table *result     = newTable(present ? n : n + 1, type);
    std::memcpy(result->keys(), t->keys(), at * sizeof(slot_t));
    result->keys()[at] = present ? t->keys()[at] : key;
    std::memcpy(result->keys() + at + 1, t->keys() + from, tail * sizeof(slot_t));
    if (map) {
        std::memcpy(result->values(), t->values(), at * sizeof(slot_t));
        result->values()[at] = value;
        std::memcpy(result->values() + at + 1, t->values() + from, tail * sizeof(slot_t));
    }
    barrierAll(result, type);
    return result;
}

} // namespace

Table *put(const Table *t, slot_t key, slot_t value, const type::MapType *type) {
    return insert(t, key, value, type);
}

Table *add(const Table *t, slot_t elem, const type::SetType *type) {
    return insert(t, elem, NullSlot, type);
}

Table *remove(const Table *t, slot_t key, const type::CollectionType *type) {
    const ptrdiff_t found = find(t, key, type->keyType());
    if (found < 0) {
        return const_cast<Table *>(t);
    }
    const size_t at   = static_cast<size_t>(found);
    const size_t n    = t->size();
    const size_t tail = n - at - 1;
    Table *result     = newTable(n - 1, type);
    std::memcpy(result->keys(), t->keys(), at * sizeof(slot_t));
    std::memcpy(result->keys() + at, t->keys() + at + 1, tail * sizeof(slot_t));
    if (type->isMap()) {
        std::memcpy(result->values(), t->values(), at * sizeof(slot_t));
        std::memcpy(result->values() + at, t->values() + at + 1, tail * sizeof(slot_t));
    }
    barrierAll(result, type);
    return result;
}

Table *merge(const Table *a, const Table *b, const type::CollectionType *type) {
    if (b->size() == 0) {
        return const_cast<Table *>(a);
    }
    if (a->size() == 0) {
        return const_cast<Table *>(b);
    }
    const bool map      = type->isMap();
    const Type *keyType = type->keyType();
    Table *scratch      = newTable(a->size() + b->size(), type);
    size_t i = 0, j = 0, count = 0;
    while (i < a->size() && j < b->size()) {
        const int order = compareKeys(a->keys()[i], b->keys()[j], keyType);
        if (order < 0) {
            copyEntry(scratch, count++, a, i++, map);
        } else if (order > 0) {
            copyEntry(scratch, count++, b, j++, map);
        } else {
            copyEntry(scratch, count++, b, j++, map);
            ++i;
        }
    }
    while (i < a->size()) {
        copyEntry(scratch, count++, a, i++, map);
    }
    while (j < b->size()) {
        copyEntry(scratch, count++, b, j++, map);
    }
    return finished(scratch, count, type);
}

Table *intersect(const Table *a, const Table *b, const type::SetType *type) {
    const Type *keyType = type->keyType();
    Table *scratch      = newTable(std::min(a->size(), b->size()), type);
    size_t i = 0, j = 0, count = 0;
    while (i < a->size() && j < b->size()) {
        const int order = compareKeys(a->keys()[i], b->keys()[j], keyType);
        if (order < 0) {
            ++i;
        } else if (order > 0) {
            ++j;
        } else {
            scratch->keys()[count++] = a->keys()[i++];
            ++j;
        }
    }
    return finished(scratch, count, type);
}

Table *difference(const Table *a, const Table *b, const type::SetType *type) {
    if (b->size() == 0 || a->size() == 0) {
        return const_cast<Table *>(a);
    }
    const Type *keyType = type->keyType();
    Table *scratch      = newTable(a->size(), type);
    size_t i = 0, j = 0, count = 0;
    while (i < a->size()) {
        const int order =
            j < b->size() ? compareKeys(a->keys()[i], b->keys()[j], keyType) : -1;
        if (order < 0) {
            scratch->keys()[count++] = a->keys()[i++];
        } else if (order > 0) {
            ++j;
        } else {
            ++i;
            ++j;
        }
    }
    return finished(scratch, count, type);
}

bool isSubset(const Table *a, const Table *b, const type::SetType *type) {
    if (a->size() > b->size()) {
        return false;
    }
    const Type *keyType = type->keyType();
    size_t j            = 0;
    for (size_t i = 0; i < a->size(); ++i) {
        while (j < b->size() && compareKeys(b->keys()[j], a->keys()[i], keyType) < 0) {
            ++j;
        }
        if (j == b->size() || compareKeys(b->keys()[j], a->keys()[i], keyType) != 0) {
            return false;
        }
        ++j;
    }
    return true;
}

namespace {

/// A new array of type `arrayType` holding the `n` slots at `slots`.
Array *arrayOf(const slot_t *slots, size_t n, const type::ArrayType *arrayType) {
    Array *result = Array::create(heap(), n);
    if (n == 0) {
        return result;
    }
    if (!arrayType->elemType()->isGCTraced()) {
        std::memcpy(result->data(), slots, n * sizeof(slot_t));
        return result;
    }
    for (size_t i = 0; i < n; ++i) {
        result->set<slot_t>(i, slots[i], arrayType);
    }
    return result;
}

} // namespace

Array *keys(const Table *t, const type::CollectionType *type) {
    return arrayOf(t->keys(), t->size(), type->keyArrayType());
}

Array *values(const Table *t, const type::MapType *type) {
    return arrayOf(t->values(), t->size(), type->valueArrayType());
}

Array *items(const Table *t, const type::MapType *type) {
    const size_t n               = t->size();
    const type::TupleType *item  = type->itemType();
    const type::ArrayType *array = type->sourceArrayType();
    Array *result                = Array::create(heap(), n);
    for (size_t i = 0; i < n; ++i) {
        Tuple *entry = Tuple::create(2, heap());
        entry->set<slot_t>(0, t->keys()[i], item);
        entry->set<slot_t>(1, t->values()[i], item);
        result->set<Object *>(i, entry, array);
    }
    return result;
}

void forEachSlot(
    const Table *t, const type::CollectionType *type,
    const std::function<void(slot_t, type::Type *)> &visit) {
    for (size_t i = 0; i < t->size(); ++i) {
        visit(t->keys()[i], type->keyType());
    }
    if (type->isMap()) {
        for (size_t i = 0; i < t->size(); ++i) {
            visit(t->values()[i], type->valueType());
        }
    }
}

Table *withSlots(
    const Table *t, const type::CollectionType *type, mm::IAllocator &allocator,
    const std::function<slot_t(slot_t, type::Type *)> &rewrite) {
    const size_t n         = t->size();
    const size_t slotCount = type->isMap() ? 2 * n : n;
    std::vector<slot_t> slots(slotCount);
    bool changed = false;
    for (size_t i = 0; i < n; ++i) {
        slots[i] = rewrite(t->keys()[i], type->keyType());
        changed |= slots[i] != t->keys()[i];
    }
    for (size_t i = n; i < slotCount; ++i) {
        slots[i] = rewrite(t->values()[i - n], type->valueType());
        changed |= slots[i] != t->values()[i - n];
    }
    if (!changed) {
        return const_cast<Table *>(t);
    }
    Table *result = Table::create(n, slotCount, allocator);
    std::copy(slots.begin(), slots.end(), result->keys());
    barrierAll(result, type);
    return result;
}

} // namespace table

} // namespace camel::core::rtdata
