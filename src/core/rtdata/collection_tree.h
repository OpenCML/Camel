// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The inside of the tree of Map and Set, shared by the files that implement it
 * (collection.cpp: the table object; collection_tree.cpp: tables, the bottom-up builder and
 * the shape; collection_ops.cpp: the operations). Not an interface: nothing outside
 * src/core/rtdata includes it. The layout and its invariants are described in
 * camel/core/rtdata/collection.h.
 *
 * Everything here allocates tables, fills them and returns them without calling anything that
 * could run a collection in between: an allocation is not a safepoint (note 23, "Phase 2
 * Safepoint Contract"), so slots copied from the arguments and tables built so far stay valid
 * until the operation returns. A Cursor and an Entries live on the stack of one operation.
 */

#pragma once

#include "camel/core/mm.h"
#include "camel/core/rtdata/collection.h"

#include <vector>

namespace camel::core::rtdata::table::detail {

using type::CollectionType;
using type::MapType;
using type::SetType;
using type::Type;
using type::TypeCode;

// A branch being rebuilt keeps its children in an array on the stack.
constexpr size_t kMaxFanout = 256;
// A branch has at least two children and a leaf at least one entry, and a table counts its
// entries in 32 bits, so no tree is higher than 32.
constexpr size_t kMaxHeight = 40;
// An operation on two collections applies the smaller to the larger entry by entry, in
// O(m log n), when the larger has at least this many times the entries; otherwise it walks
// both, in O(n + m).
constexpr size_t kPointwiseRatio = 32;

inline mm::IAllocator &heap() { return mm::autoSpace(); }

// ---- One table ----

/// The most entries a leaf of `type` holds. A set has one slot per entry where a map has two,
/// so its leaves hold twice as many in the same room.
size_t leafCapacity(const CollectionType *type);

/// The slots a leaf of `size` entries is allocated with. A leaf that is a whole collection has
/// exactly the slots of its entries. A leaf below a branch is allocated at full width, like
/// every branch, so that all the tables of a tree have one size: a table freed by the
/// collector leaves a block that fits the next one (note 31, section 5.3).
size_t leafSlots(size_t size, const CollectionType *type, bool inTree);

/// The barrier for every reference `t` holds. Called once a new table is filled.
void barrierAll(Table *t, const CollectionType *type);

/// A leaf of `size` entries in the collected heap, all slots null.
Table *newLeaf(size_t size, const CollectionType *type, bool inTree = false);

/// A leaf holding entries `begin` to `end` of a sequence given by `keyAt` and `valueAt`.
template <typename KeyAt, typename ValueAt>
Table *leafOf(
    size_t begin, size_t end, const CollectionType *type, mm::IAllocator &allocator, bool inTree,
    const KeyAt &keyAt, const ValueAt &valueAt) {
    const bool map    = type->isMap();
    const size_t size = end - begin;
    Table *leaf       = Table::create(size, leafSlots(size, type, inTree), allocator);
    for (size_t i = 0; i < size; ++i) {
        leaf->keys()[i] = keyAt(begin + i);
        if (map) {
            leaf->values()[i] = valueAt(begin + i);
        }
    }
    barrierAll(leaf, type);
    return leaf;
}

/// A branch over `count` tables of one height, none of them empty.
Table *makeBranch(
    Table *const *children, size_t count, const CollectionType *type,
    mm::IAllocator &allocator);

/// The first index of leaf `t` whose key is not before `key`.
size_t lowerBound(const Table *t, slot_t key, const Type *keyType);

/// The index of `key` in leaf `t`, or -1.
ptrdiff_t findInLeaf(const Table *t, slot_t key, const Type *keyType);

/// The child of branch `t` under which `key` belongs: the last whose smallest key is not
/// after `key`. A key before every separator belongs under the first child.
size_t childIndex(const Table *t, slot_t key, const Type *keyType);

/// The leaf of the tree `t` in which `key` is, if it is in the tree at all.
const Table *leafFor(const Table *t, slot_t key, const Type *keyType);

/// Calls `visit(leaf)` for the leaves of `t` from left to right.
template <typename Visit> void forEachLeaf(const Table *t, const Visit &visit) {
    if (t->isLeaf()) {
        visit(t);
        return;
    }
    for (size_t i = 0; i < t->count(); ++i) {
        forEachLeaf(t->children()[i], visit);
    }
}

// ---- Building a tree bottom-up ----

/// `n` items cut into the fewest groups of at most `capacity`, of equal share (they differ by
/// one at most): group g is the items bound(g) to bound(g + 1). For n above the capacity
/// every share is at least half of it, which is the minimum a table must hold.
struct Shares {
    size_t n;
    size_t groups;
    Shares(size_t n, size_t capacity)
        : n(n), groups(n <= capacity ? 1 : n / capacity + (n % capacity != 0 ? 1 : 0)) {}
    size_t bound(size_t g) const { return g * n / groups; }
};

/// The tree over `level`, tables of one height in key order: branches are put above them
/// until one table is left.
Table *buildBranches(
    std::vector<Table *> level, const CollectionType *type, mm::IAllocator &allocator);

/// The collection of the `n` entries given by `keyAt` and `valueAt`, which are in strictly
/// ascending key order: O(n), no comparison.
template <typename KeyAt, typename ValueAt>
Table *buildTree(
    size_t n, const CollectionType *type, mm::IAllocator &allocator, const KeyAt &keyAt,
    const ValueAt &valueAt) {
    const size_t capacity = leafCapacity(type);
    if (n <= capacity) {
        return leafOf(0, n, type, allocator, false, keyAt, valueAt);
    }
    const Shares shares(n, capacity);
    std::vector<Table *> leaves(shares.groups);
    for (size_t g = 0; g < shares.groups; ++g) {
        leaves[g] =
            leafOf(shares.bound(g), shares.bound(g + 1), type, allocator, true, keyAt, valueAt);
    }
    return buildBranches(std::move(leaves), type, allocator);
}

/// Entries in key order gathered by a bulk operation before its result is built. The slots
/// are held outside the heap only until the operation returns.
struct Entries {
    explicit Entries(const CollectionType *type) : type(type), map(type->isMap()) {}

    void push(slot_t key, slot_t value) {
        keys.push_back(key);
        if (map) {
            values.push_back(value);
        }
    }
    size_t size() const { return keys.size(); }
    Table *build(mm::IAllocator &allocator) const {
        return buildTree(
            keys.size(),
            type,
            allocator,
            [&](size_t i) { return keys[i]; },
            [&](size_t i) { return values[i]; });
    }

    const CollectionType *type;
    bool map;
    std::vector<slot_t> keys;
    std::vector<slot_t> values;
};

/// A leaf an operation on leaves produced, as a collection: itself when it fits a leaf, a
/// tree of its entries otherwise.
Table *normalized(Table *leaf, const CollectionType *type);

// ---- Walking a tree in key order ----

/// A position in a collection, advanced entry by entry. It holds the path from the root to
/// the current leaf, so it lives on the stack of one operation and no longer.
class Cursor {
  public:
    Cursor(const Table *root, const CollectionType *type) : map_(type->isMap()) {
        leaf_ = leftmostLeaf(root);
        if (leaf_->size() == 0) {
            leaf_ = nullptr;
        }
    }

    bool done() const { return leaf_ == nullptr; }
    slot_t key() const { return leaf_->keys()[pos_]; }
    /// The null slot in a set.
    slot_t value() const { return map_ ? leaf_->values()[pos_] : NullSlot; }

    void next() {
        if (++pos_ < leaf_->size()) {
            return;
        }
        pos_ = 0;
        // Up to the nearest table that has a further child, then down its leftmost path.
        while (depth_ > 0 && path_[depth_ - 1].index + 1 == path_[depth_ - 1].node->count()) {
            --depth_;
        }
        if (depth_ == 0) {
            leaf_ = nullptr;
            return;
        }
        Step &step = path_[depth_ - 1];
        leaf_      = leftmostLeaf(step.node->children()[++step.index]);
    }

  private:
    struct Step {
        const Table *node;
        size_t index;
    };

    const Table *leftmostLeaf(const Table *t) {
        while (!t->isLeaf()) {
            ASSERT(depth_ < kMaxHeight, "A collection tree is higher than its size allows.");
            path_[depth_++] = Step{t, 0};
            t               = t->children()[0];
        }
        return t;
    }

    bool map_;
    const Table *leaf_ = nullptr;
    size_t pos_        = 0;
    size_t depth_      = 0;
    Step path_[kMaxHeight];
};

/// Whether an operation on `small` and `large` should go through `small` entry by entry.
bool muchSmaller(const Table *small, const Table *large);

/// Stops the process when `t` breaks an invariant and --coll validation asks for the check.
Table *checked(Table *t, const CollectionType *type);

} // namespace camel::core::rtdata::table::detail
