// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The run-time object of `Map<K, V>` and `Set<T>` (docs/technical/31_map_and_set.md, section 5).
 *
 * A collection is a persistent B+ tree of Tables. A Table is one allocation, either a leaf or
 * a branch; `height` says which.
 *
 *   leaf (height 0), `size` entries:
 *     Set<T>:    data[0 .. size)          elements, ascending
 *     Map<K,V>:  data[0 .. size)          keys, ascending
 *                data[size .. 2*size)     values, in the order of their keys
 *
 *   branch (height h > 0), `count` children holding `size` entries between them:
 *                data[0 .. count)         the smallest key under each child, ascending
 *                data[count .. 2*count)   the children, tables of height h - 1
 *
 * A collection of at most `leafCapacity` entries is a single leaf: the tree only begins beyond
 * that size, so a small collection costs what a sorted array costs.
 *
 * Invariants:
 *   - A table is never changed after the operation that built it has returned. Every
 *     operation of the `table` namespace returns a new table (or one of its arguments, which
 *     is the same value); new tables share the unchanged subtrees of old ones.
 *   - Keys are strictly ascending across the whole tree under compareKeys, which looks at the
 *     contents of the keys only: nothing depends on an address, so the collector may move
 *     tables and keys.
 *   - All leaves are at the same depth. A leaf other than the root holds between
 *     leafCapacity / 2 and leafCapacity entries, a branch other than the root between
 *     fanout / 2 and fanout children; a root branch has at least two children. No leaf below
 *     a branch is empty.
 *   - A table holds no pointer into itself; the slots follow the header. A child is an
 *     ordinary object reference that updateRefs hands to the collector.
 *   - Which layout a table has, and what its slots hold, is known from the static type
 *     (type::CollectionType), which every function here is handed, and the header of the
 *     table.
 *   - No function here keeps a pointer to a table after it returns. Within a call, pointers are
 *     held across allocations, which are not safepoints (docs/technical/23_gc_runtime_lifetime.md,
 *     "Phase 2 Safepoint Contract").
 *
 * The shape of a tree depends on the history of the value and is never observable: order,
 * equality and printing are those of the entries.
 */

#pragma once

#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/collection.h"

#include <functional>
#include <string>

namespace camel::core::rtdata {

class Table final : public Object {
  public:
    Table(const Table &)            = delete;
    Table &operator=(const Table &) = delete;

    /// A leaf of `size` entries with `slotCount` slots, all null: at least `size` for a set and
    /// `2 * size` for a map; any more are room left unused at the end. The caller fills it
    /// before anything else can see it.
    static Table *create(size_t size, size_t slotCount, camel::core::mm::IAllocator &allocator);
    /// A branch of `count` children at `height` (at least 1) holding `size` entries, with room
    /// for `width` children, all slots null. The caller fills it before anything else can see
    /// it.
    static Table *createBranch(
        size_t count, size_t height, size_t size, size_t width,
        camel::core::mm::IAllocator &allocator);

    /// The number of entries of the collection rooted here.
    size_t size() const { return size_; }
    bool isLeaf() const { return height_ == 0; }
    size_t height() const { return height_; }

    /// The keys of a leaf, or the separators of a branch (the smallest key under each child).
    /// Either way the first is the smallest key of this table.
    slot_t *keys() { return data_; }
    const slot_t *keys() const { return data_; }
    /// The values of a leaf of a map. Not defined for a set or a branch.
    slot_t *values() { return data_ + size_; }
    const slot_t *values() const { return data_ + size_; }

    /// The number of children of a branch.
    size_t count() const { return count_; }
    Table **children() { return reinterpret_cast<Table **>(data_ + count_); }
    Table *const *children() const { return reinterpret_cast<Table *const *>(data_ + count_); }

    bool equals(const Object *other, const type::Type *type, bool deep = false) const override;
    /// A copy that shares no table with this one (all of its tables are in `allocator`); keys
    /// and values are shared unless `deep`.
    Object *clone(
        camel::core::mm::IAllocator &allocator, const type::Type *type,
        bool deep = false) const override;
    void print(std::ostream &os, const type::Type *type) const override;
    void onMoved() override;
    void updateRefs(const RefRelocator &relocate, const type::Type *type) override;

  private:
    Table(size_t size, size_t height, size_t count)
        : size_(static_cast<uint32_t>(size)), height_(static_cast<uint16_t>(height)),
          count_(static_cast<uint16_t>(count)) {}

    uint32_t size_;
    uint16_t height_;
    uint16_t count_;
    slot_t data_[];
};

/// The order of keys: negative, zero or positive as `a` is before, equal to or after `b`.
/// Integers numerically, false before true, strings by bytes, tuples element by element.
int compareKeys(slot_t a, slot_t b, const type::Type *keyType);

/// Equality of two values of `type`, as `==` on a map compares its values: objects
/// structurally, floats numerically, everything else by its bits.
bool valuesEqual(slot_t a, slot_t b, const type::Type *type);

namespace table {

/// The widths of the tree: a leaf of a map holds at most `leafCapacity` entries (a leaf of a
/// set twice as many, in the same room), a branch at most `fanout` children. Read once from
/// the environment, as a test and measurement control: CAMEL_COLL_LEAF_CAPACITY (0: unbounded,
/// so that every collection is one sorted array), CAMEL_COLL_FANOUT, CAMEL_COLL_PAD_NODES (0:
/// a table below a root is allocated for its content only, not at full width) and
/// CAMEL_COLL_VALIDATE (every table an operation returns is checked with `validate`, and the
/// process stops at the first violation).
struct Shape {
    size_t leafCapacity;
    size_t fanout;
    bool padded;
    bool validate;
};
const Shape &shape();

/// Whether `key` is in `t`.
bool has(const Table *t, slot_t key, const type::Type *keyType);
/// The value of `key` in the map `t`, stored to `value`; false when the key is absent.
bool lookup(const Table *t, slot_t key, const type::Type *keyType, slot_t &value);

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

/// Calls `visit(key, value)` for every entry in key order; `value` is the null slot for a set.
void forEachEntry(
    const Table *t, const type::CollectionType *type,
    const std::function<void(slot_t, slot_t)> &visit);

// For code that walks constant data by its static type (the static area of a graph: note 22),
// which meets a collection as a container of typed slots like an array or a tuple.

/// Calls `visit(slot, slotType)` for every key and, for a map, every value.
void forEachSlot(
    const Table *t, const type::CollectionType *type,
    const std::function<void(slot_t, type::Type *)> &visit);
/// The collection with every key and value replaced by `rewrite(slot, slotType)`: `t` itself
/// when none changed, otherwise a new tree all of whose tables are in `allocator`. A rewrite
/// must keep the content of a key (it may move or copy the object), so the order of the
/// entries stands.
Table *withSlots(
    const Table *t, const type::CollectionType *type, camel::core::mm::IAllocator &allocator,
    const std::function<slot_t(slot_t, type::Type *)> &rewrite);

/// Checks the invariants listed at the top of this file. Empty when they hold, otherwise a
/// description of the first violation.
std::string validate(const Table *t, const type::CollectionType *type);

} // namespace table

} // namespace camel::core::rtdata
