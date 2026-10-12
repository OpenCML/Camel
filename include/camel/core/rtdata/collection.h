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
 * The run-time object of `Map<K, V>` and `Set<T>` (docs/technical/31_map_and_set.md).
 *
 * A Table is one allocation: a size and the slots, ordered by key.
 *
 *   Set<T>:    data[0 .. size)        elements, ascending
 *   Map<K,V>:  data[0 .. size)        keys, ascending
 *              data[size .. 2*size)   values, in the order of their keys
 *
 * Invariants:
 *   - A table is never changed after the operation that built it has returned. Every
 *     operation of the `table` namespace returns a new table (or one of its arguments, which
 *     is the same value).
 *   - Keys are strictly ascending under compareKeys, which looks at the keys' contents only:
 *     nothing depends on an address, so the collector may move a table and its keys.
 *   - The object holds no pointer into itself; the slots follow the header.
 *   - Which of the two layouts a table has, and what its slots hold, is known from the static
 *     type alone (type::CollectionType), which every function here is handed.
 *
 * DESIGN-QUESTION(map-persistence): a sorted array copied by every `put` (O(n)). Only this
 * file and collection.cpp know the layout; a persistent tree can replace it behind the same
 * functions.
 */

#pragma once

#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/collection.h"

#include <functional>

namespace camel::core::rtdata {

class Table final : public Object {
  public:
    Table(const Table &)            = delete;
    Table &operator=(const Table &) = delete;

    /// A table of `size` entries with `slotCount` slots (`size` for a set, `2 * size` for a
    /// map), all null. The caller fills it before anything else can see it.
    static Table *create(size_t size, size_t slotCount, camel::core::mm::IAllocator &allocator);

    size_t size() const { return size_; }
    slot_t *keys() { return data_; }
    const slot_t *keys() const { return data_; }
    /// The values of a map. Not defined for a set.
    slot_t *values() { return data_ + size_; }
    const slot_t *values() const { return data_ + size_; }

    bool equals(const Object *other, const type::Type *type, bool deep = false) const override;
    Object *clone(
        camel::core::mm::IAllocator &allocator, const type::Type *type,
        bool deep = false) const override;
    void print(std::ostream &os, const type::Type *type) const override;
    void onMoved() override;
    void updateRefs(const RefRelocator &relocate, const type::Type *type) override;

  private:
    explicit Table(size_t size) : size_(static_cast<uint32_t>(size)) {}

    uint32_t size_;
    slot_t data_[];
};

/// The order of keys: negative, zero or positive as `a` is before, equal to or after `b`.
/// Integers numerically, false before true, strings by bytes, tuples element by element.
int compareKeys(slot_t a, slot_t b, const type::Type *keyType);

/// Equality of two values of `type`, as `==` on a map compares its values: objects
/// structurally, floats numerically, everything else by its bits.
bool valuesEqual(slot_t a, slot_t b, const type::Type *type);

namespace table {

/// The index of `key` in `t`, or -1.
ptrdiff_t find(const Table *t, slot_t key, const type::Type *keyType);

/// The empty collection of `type`.
Table *empty(const type::CollectionType *type);
/// `array as Map<K, V>` (an array of `(K, V)`) or `array as Set<T>`. Of two entries with
/// equal keys the later one is kept (DESIGN-QUESTION(map-duplicate-keys)).
Table *fromArray(const Array *array, const type::CollectionType *type);

Table *put(const Table *t, slot_t key, slot_t value, const type::MapType *type);
Table *add(const Table *t, slot_t elem, const type::SetType *type);
Table *remove(const Table *t, slot_t key, const type::CollectionType *type);

/// The entries of both; on a common key the entry of `b`. The union of two sets.
Table *merge(const Table *a, const Table *b, const type::CollectionType *type);
Table *intersect(const Table *a, const Table *b, const type::SetType *type);
Table *difference(const Table *a, const Table *b, const type::SetType *type);
bool isSubset(const Table *a, const Table *b, const type::SetType *type);

/// New arrays, in key order: the keys (the elements of a set), the values, the items.
Array *keys(const Table *t, const type::CollectionType *type);
Array *values(const Table *t, const type::MapType *type);
Array *items(const Table *t, const type::MapType *type);

// For code that walks constant data by its static type (the static area of a graph: note 22),
// which meets a collection as a container of typed slots like an array or a tuple.

/// Calls `visit(slot, slotType)` for every key slot and, for a map, every value slot.
void forEachSlot(
    const Table *t, const type::CollectionType *type,
    const std::function<void(slot_t, type::Type *)> &visit);
/// The table with every slot replaced by `rewrite(slot, slotType)`: `t` itself when no slot
/// changed, a new table in `allocator` otherwise. A rewrite must keep the content of a key
/// (it may move or copy the object), so the order of the entries stands.
Table *withSlots(
    const Table *t, const type::CollectionType *type, camel::core::mm::IAllocator &allocator,
    const std::function<slot_t(slot_t, type::Type *)> &rewrite);

} // namespace table

} // namespace camel::core::rtdata
