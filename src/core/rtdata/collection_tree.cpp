// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The tables of the tree of Map and Set and what is common to all its operations: the widths
 * (table::shape), allocation and filling of one table, search within a table, the bottom-up
 * builder, and the check of the invariants (table::validate). See collection_tree.h.
 */

#include "collection_tree.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

namespace camel::core::rtdata::table {

namespace {

// Measured defaults: docs/technical/31_map_and_set.md, section 9.3.
constexpr size_t kDefaultLeafCapacity = 64;
constexpr size_t kDefaultFanout       = 64;

size_t envNumber(const char *name, size_t fallback) {
#ifdef _WIN32
    char *buffer = nullptr;
    size_t size  = 0;
    if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
        return fallback;
    }
    const std::string raw(buffer);
    std::free(buffer);
#else
    const char *value = std::getenv(name);
    if (value == nullptr) {
        return fallback;
    }
    const std::string raw(value);
#endif
    if (raw.empty()) {
        return fallback;
    }
    char *end                       = nullptr;
    const unsigned long long parsed = std::strtoull(raw.c_str(), &end, 10);
    return end != nullptr && *end == '\0' ? static_cast<size_t>(parsed) : fallback;
}

} // namespace

const Shape &shape() {
    static const Shape configured = [] {
        Shape s{};
        // 0 asks for leaves without bound: no collection ever becomes a tree.
        const size_t leaf = envNumber("CAMEL_COLL_LEAF_CAPACITY", kDefaultLeafCapacity);
        s.leafCapacity    = leaf == 0 ? std::numeric_limits<size_t>::max()
                                      : std::max<size_t>(leaf, 2);
        s.fanout =
            std::clamp<size_t>(envNumber("CAMEL_COLL_FANOUT", kDefaultFanout), 4, detail::kMaxFanout);
        s.padded   = envNumber("CAMEL_COLL_PAD_NODES", 1) != 0;
        s.validate = envNumber("CAMEL_COLL_VALIDATE", 0) != 0;
        return s;
    }();
    return configured;
}

namespace detail {

// ---- One table ----

size_t leafCapacity(const CollectionType *type) {
    const size_t capacity = table::shape().leafCapacity;
    return type->isMap() || capacity > std::numeric_limits<size_t>::max() / 2 ? capacity
                                                                             : 2 * capacity;
}

size_t leafSlots(size_t size, const CollectionType *type, bool inTree) {
    const size_t used = type->isMap() ? 2 * size : size;
    return inTree && table::shape().padded ? std::max(used, 2 * table::shape().leafCapacity)
                                           : used;
}

void barrierAll(Table *t, const CollectionType *type) {
    const Type *keyType = type->keyType();
    const size_t keys   = t->isLeaf() ? t->size() : t->count();
    if (keyType->isGCTraced()) {
        for (size_t i = 0; i < keys; ++i) {
            mm::writeBarrier(t, type, t->keys()[i], keyType);
        }
    }
    if (!t->isLeaf()) {
        for (size_t i = 0; i < t->count(); ++i) {
            mm::writeBarrier(t, type, t->children()[i], type);
        }
        return;
    }
    const Type *valueType = type->valueType();
    if (valueType != nullptr && valueType->isGCTraced()) {
        for (size_t i = 0; i < t->size(); ++i) {
            mm::writeBarrier(t, type, t->values()[i], valueType);
        }
    }
}

Table *newLeaf(size_t size, const CollectionType *type, bool inTree) {
    return Table::create(size, leafSlots(size, type, inTree), heap());
}

Table *makeBranch(
    Table *const *children, size_t count, const CollectionType *type,
    mm::IAllocator &allocator) {
    size_t size = 0;
    for (size_t i = 0; i < count; ++i) {
        ASSERT(children[i]->size() > 0, "A table below a branch is never empty.");
        size += children[i]->size();
    }
    const size_t width = table::shape().padded ? std::max(count, table::shape().fanout) : count;
    Table *branch = Table::createBranch(count, children[0]->height() + 1, size, width, allocator);
    for (size_t i = 0; i < count; ++i) {
        branch->keys()[i]     = children[i]->keys()[0];
        branch->children()[i] = children[i];
    }
    barrierAll(branch, type);
    return branch;
}

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

ptrdiff_t findInLeaf(const Table *t, slot_t key, const Type *keyType) {
    const size_t at = lowerBound(t, key, keyType);
    if (at < t->size() && compareKeys(t->keys()[at], key, keyType) == 0) {
        return static_cast<ptrdiff_t>(at);
    }
    return -1;
}

size_t childIndex(const Table *t, slot_t key, const Type *keyType) {
    size_t lo = 1, hi = t->count();
    const slot_t *separators = t->keys();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (compareKeys(separators[mid], key, keyType) <= 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo - 1;
}

const Table *leafFor(const Table *t, slot_t key, const Type *keyType) {
    while (!t->isLeaf()) {
        t = t->children()[childIndex(t, key, keyType)];
    }
    return t;
}

// ---- Building a tree bottom-up ----

Table *buildBranches(
    std::vector<Table *> level, const CollectionType *type, mm::IAllocator &allocator) {
    const size_t fanout = table::shape().fanout;
    while (level.size() > 1) {
        const Shares shares(level.size(), fanout);
        std::vector<Table *> above(shares.groups);
        for (size_t g = 0; g < shares.groups; ++g) {
            above[g] = makeBranch(
                level.data() + shares.bound(g),
                shares.bound(g + 1) - shares.bound(g),
                type,
                allocator);
        }
        level = std::move(above);
    }
    return level.front();
}

Table *normalized(Table *leaf, const CollectionType *type) {
    if (leaf->size() <= leafCapacity(type)) {
        return leaf;
    }
    return buildTree(
        leaf->size(),
        type,
        heap(),
        [&](size_t i) { return leaf->keys()[i]; },
        [&](size_t i) { return leaf->values()[i]; });
}

bool muchSmaller(const Table *small, const Table *large) {
    return small->size() <= large->size() / kPointwiseRatio;
}

Table *checked(Table *t, const CollectionType *type) {
    if (table::shape().validate) {
        const std::string violation = table::validate(t, type);
        if (!violation.empty()) {
            std::fprintf(
                stderr,
                "[camel] invalid %s table: %s\n",
                type->toString().c_str(),
                violation.c_str());
            std::abort();
        }
    }
    return t;
}

} // namespace detail

// ---- The invariants ----

namespace {

using namespace detail;

/// The state of one `validate` walk.
struct Validation {
    const CollectionType *type;
    const Table *root;
    // The key before the one being looked at, across leaves.
    bool hasLast = false;
    slot_t last  = NullSlot;
    std::string violation;

    bool fail(const Table *t, const std::string &what) {
        if (violation.empty()) {
            violation = (t->isLeaf() ? "leaf" : "branch at height " + std::to_string(t->height())) +
                        std::string(" of ") + std::to_string(t->size()) + " entries: " + what;
        }
        return false;
    }

    bool check(const Table *t) {
        const Shape &s      = shape();
        const Type *keyType = type->keyType();
        if (t->isLeaf()) {
            if (t->size() > leafCapacity(type)) {
                return fail(t, "more entries than a leaf may hold");
            }
            if (t != root && (t->size() == 0 || t->size() < leafCapacity(type) / 2)) {
                return fail(t, "fewer entries than a leaf below a branch must hold");
            }
            for (size_t i = 0; i < t->size(); ++i) {
                if (hasLast && compareKeys(last, t->keys()[i], keyType) >= 0) {
                    return fail(t, "key " + std::to_string(i) + " is not after the key before it");
                }
                hasLast = true;
                last    = t->keys()[i];
            }
            return true;
        }
        if (t->count() > s.fanout || t->count() < (t == root ? 2 : s.fanout / 2)) {
            return fail(t, std::to_string(t->count()) + " children");
        }
        size_t size = 0;
        for (size_t i = 0; i < t->count(); ++i) {
            const Table *child = t->children()[i];
            if (child == nullptr) {
                return fail(t, "child " + std::to_string(i) + " is null");
            }
            if (child->height() + 1 != t->height()) {
                return fail(t, "child " + std::to_string(i) + " is not one level below");
            }
            if (child->size() == 0 ||
                compareKeys(t->keys()[i], child->keys()[0], keyType) != 0) {
                return fail(
                    t,
                    "separator " + std::to_string(i) + " is not the smallest key of its child");
            }
            if (!check(child)) {
                return false;
            }
            size += child->size();
        }
        if (size != t->size()) {
            return fail(t, "the children hold " + std::to_string(size) + " entries");
        }
        return true;
    }
};

} // namespace

std::string validate(const Table *t, const type::CollectionType *type) {
    Validation validation{
        .type = type, .root = t, .hasLast = false, .last = NullSlot, .violation = {}};
    validation.check(t);
    return validation.violation;
}

} // namespace camel::core::rtdata::table
