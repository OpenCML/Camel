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
 * The operations on Map and Set (the `table` namespace of camel/core/rtdata/collection.h):
 * lookup, construction from an array, the point updates that copy one path of the tree, and
 * the operations on two collections. Two leaves are handled by the single-array code
 * throughout; the tree code is entered only when a collection has outgrown a leaf.
 */

#include "camel/core/rtdata/collection.h"

#include "camel/core/rtdata/tuple.h"
#include "collection_tree.h"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <vector>

namespace camel::core::rtdata::table {

using namespace detail;

bool has(const Table *t, slot_t key, const type::Type *keyType) {
    return findInLeaf(leafFor(t, key, keyType), key, keyType) >= 0;
}

bool lookup(const Table *t, slot_t key, const type::Type *keyType, slot_t &value) {
    const Table *leaf  = leafFor(t, key, keyType);
    const ptrdiff_t at = findInLeaf(leaf, key, keyType);
    if (at < 0) {
        return false;
    }
    value = leaf->values()[at];
    return true;
}

Table *empty(const type::CollectionType *type) { return newLeaf(0, type); }

Table *fromArray(const Array *array, const type::CollectionType *type) {
    const size_t n      = array ? array->size() : 0;
    const bool map      = type->isMap();
    const Type *keyType = type->keyType();
    if (n == 0) {
        return newLeaf(0, type);
    }
    // The key and the value of entry i of the source. Read through getSlot: a numeric array is
// compact and does not have one slot per element.
    const auto keyAt = [&](size_t i) {
        const slot_t source = array->getSlot(i);
        return map ? fromSlot<Tuple *>(source)->get<slot_t>(0) : source;
    };
    const auto valueAt = [&](size_t i) {
        return fromSlot<Tuple *>(array->getSlot(i))->get<slot_t>(1);
    };

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
    // The entries are now in order and distinct: the tree is built over them directly.
    return checked(
        buildTree(
            count,
            type,
            heap(),
            [&](size_t i) { return keyAt(order[i]); },
            [&](size_t i) { return valueAt(order[i]); }),
        type);
}

namespace {

/// What a table became when an entry was put in: one table, or two when it had to be split.
/// `first` is the table itself when nothing changed.
struct Grown {
    Table *first;
    Table *second = nullptr;
};

/// Leaf `t` with (`key`, `value`) put in; `value` is ignored for a set.
Grown insertIntoLeaf(
    Table *t, slot_t key, slot_t value, const CollectionType *type, bool inTree) {
    const bool map      = type->isMap();
    const Type *keyType = type->keyType();
    const size_t n      = t->size();
    const size_t at     = lowerBound(t, key, keyType);
    const bool present  = at < n && compareKeys(t->keys()[at], key, keyType) == 0;
    if (present && !map) {
        return {t};
    }
    if (!present && n + 1 > leafCapacity(type)) {
        // Full: the n + 1 entries go to two leaves of equal share.
        const auto keyAt = [&](size_t i) {
            return i < at ? t->keys()[i] : (i == at ? key : t->keys()[i - 1]);
        };
        const auto valueAt = [&](size_t i) {
            return i < at ? t->values()[i] : (i == at ? value : t->values()[i - 1]);
        };
        const size_t half = (n + 1) / 2;
        return {
            leafOf(0, half, type, heap(), true, keyAt, valueAt),
            leafOf(half, n + 1, type, heap(), true, keyAt, valueAt)};
    }
    const size_t tail = n - at - (present ? 1 : 0);
    const size_t from = at + (present ? 1 : 0);
    Table *result     = newLeaf(present ? n : n + 1, type, inTree);
    std::memcpy(result->keys(), t->keys(), at * sizeof(slot_t));
    result->keys()[at] = present ? t->keys()[at] : key;
    std::memcpy(result->keys() + at + 1, t->keys() + from, tail * sizeof(slot_t));
    if (map) {
        std::memcpy(result->values(), t->values(), at * sizeof(slot_t));
        result->values()[at] = value;
        std::memcpy(result->values() + at + 1, t->values() + from, tail * sizeof(slot_t));
    }
    barrierAll(result, type);
    return {result};
}

/// The children of branch `t` written to `out`, with the child at `at` replaced by `a` and,
/// when it is not null, `b` after it. Returns their number.
size_t childrenWith(Table **out, const Table *t, size_t at, Table *a, Table *b) {
    Table *const *children = t->children();
    size_t count           = 0;
    for (size_t i = 0; i < at; ++i) {
        out[count++] = children[i];
    }
    out[count++] = a;
    if (b != nullptr) {
        out[count++] = b;
    }
    for (size_t i = at + 1; i < t->count(); ++i) {
        out[count++] = children[i];
    }
    return count;
}

/// The tree `t` with (`key`, `value`) put in: the tables on the path to the key are copied,
/// everything else is shared with `t`.
Grown insertInto(
    Table *t, slot_t key, slot_t value, const CollectionType *type, bool inTree = false) {
    if (t->isLeaf()) {
        return insertIntoLeaf(t, key, value, type, inTree);
    }
    const size_t at   = childIndex(t, key, type->keyType());
    Table *child      = t->children()[at];
    const Grown grown = insertInto(child, key, value, type, true);
    if (grown.first == child) {
        return {t};
    }
    Table *children[kMaxFanout + 1];
    const size_t count = childrenWith(children, t, at, grown.first, grown.second);
    if (count <= shape().fanout) {
        return {makeBranch(children, count, type, heap())};
    }
    const size_t half = count / 2;
    return {
        makeBranch(children, half, type, heap()),
        makeBranch(children + half, count - half, type, heap())};
}

Table *insert(const Table *t, slot_t key, slot_t value, const CollectionType *type) {
    const Grown grown = insertInto(const_cast<Table *>(t), key, value, type);
    if (grown.second == nullptr) {
        return grown.first;
    }
    // The root was split: a new root above the two halves.
    Table *halves[2] = {grown.first, grown.second};
    return makeBranch(halves, 2, type, heap());
}

/// Leaf `t` without `key`; `t` itself when the key is absent.
Table *removeFromLeaf(Table *t, slot_t key, const CollectionType *type, bool inTree) {
    const ptrdiff_t found = findInLeaf(t, key, type->keyType());
    if (found < 0) {
        return t;
    }
    const size_t at   = static_cast<size_t>(found);
    const size_t n    = t->size();
    const size_t tail = n - at - 1;
    Table *result     = newLeaf(n - 1, type, inTree);
    std::memcpy(result->keys(), t->keys(), at * sizeof(slot_t));
    std::memcpy(result->keys() + at, t->keys() + at + 1, tail * sizeof(slot_t));
    if (type->isMap()) {
        std::memcpy(result->values(), t->values(), at * sizeof(slot_t));
        std::memcpy(result->values() + at, t->values() + at + 1, tail * sizeof(slot_t));
    }
    barrierAll(result, type);
    return result;
}

/// Whether `t`, a table below a branch, holds less than a table must.
bool underfull(const Table *t, const CollectionType *type) {
    return t->isLeaf() ? t->size() < leafCapacity(type) / 2 : t->count() < shape().fanout / 2;
}

/// Two neighbouring tables of one height made into one table when their content fits, and
/// into two of equal share otherwise. Writes them to `out` and returns their number.
size_t combine(Table *a, Table *b, Table **out, const CollectionType *type) {
    if (a->isLeaf()) {
        const size_t split = a->size();
        const size_t total = split + b->size();
        const auto keyAt   = [&](size_t i) {
            return i < split ? a->keys()[i] : b->keys()[i - split];
        };
        const auto valueAt = [&](size_t i) {
            return i < split ? a->values()[i] : b->values()[i - split];
        };
        if (total <= leafCapacity(type)) {
            out[0] = leafOf(0, total, type, heap(), true, keyAt, valueAt);
            return 1;
        }
        out[0] = leafOf(0, total / 2, type, heap(), true, keyAt, valueAt);
        out[1] = leafOf(total / 2, total, type, heap(), true, keyAt, valueAt);
        return 2;
    }
    Table *children[2 * kMaxFanout];
    const size_t total = a->count() + b->count();
    std::copy(a->children(), a->children() + a->count(), children);
    std::copy(b->children(), b->children() + b->count(), children + a->count());
    if (total <= shape().fanout) {
        out[0] = makeBranch(children, total, type, heap());
        return 1;
    }
    out[0] = makeBranch(children, total / 2, type, heap());
    out[1] = makeBranch(children + total / 2, total - total / 2, type, heap());
    return 2;
}

/// The tree `t` without `key`; `t` itself when the key is absent. The result may hold less
/// than a table must (it may be an empty leaf, or a branch of one child): the caller, which
/// is copying the parent anyway, combines it with a neighbour.
Table *removeFrom(Table *t, slot_t key, const CollectionType *type, bool inTree = false) {
    if (t->isLeaf()) {
        return removeFromLeaf(t, key, type, inTree);
    }
    const size_t at = childIndex(t, key, type->keyType());
    Table *child    = t->children()[at];
    Table *shrunk   = removeFrom(child, key, type, true);
    if (shrunk == child) {
        return t;
    }
    Table *children[kMaxFanout + 1];
    size_t count = childrenWith(children, t, at, shrunk, nullptr);
    if (underfull(shrunk, type)) {
        ASSERT(count > 1, "A branch has a neighbour for each of its children.");
        const size_t left = at > 0 ? at - 1 : at;
        Table *made[2];
        if (combine(children[left], children[left + 1], made, type) == 2) {
            children[left]     = made[0];
            children[left + 1] = made[1];
        } else {
            children[left] = made[0];
            std::copy(children + left + 2, children + count, children + left + 1);
            --count;
        }
    }
    return makeBranch(children, count, type, heap());
}

Table *removeKey(const Table *t, slot_t key, const CollectionType *type) {
    Table *result = removeFrom(const_cast<Table *>(t), key, type);
    // A root needs no minimum, but a root with a single child is that child.
    while (!result->isLeaf() && result->count() == 1) {
        result = result->children()[0];
    }
    return result;
}

} // namespace

Table *put(const Table *t, slot_t key, slot_t value, const type::MapType *type) {
    return checked(insert(t, key, value, type), type);
}

Table *add(const Table *t, slot_t elem, const type::SetType *type) {
    return checked(insert(t, elem, NullSlot, type), type);
}

Table *remove(const Table *t, slot_t key, const type::CollectionType *type) {
    return checked(removeKey(t, key, type), type);
}

namespace {

/// Copies entry `from` of leaf `src` to entry `to` of leaf `dst`.
inline void copyEntry(Table *dst, size_t to, const Table *src, size_t from, bool map) {
    dst->keys()[to] = src->keys()[from];
    if (map) {
        dst->values()[to] = src->values()[from];
    }
}

/// The leaf a pass over two leaves left in `scratch`: its first `count` entries. `scratch` was
/// allocated for the largest possible result; tables are never resized in place, so a shorter
/// result is a second, exact allocation (the values of `scratch` sit after all of its key
/// slots).
Table *finished(Table *scratch, size_t count, const CollectionType *type) {
    if (count == scratch->size()) {
        barrierAll(scratch, type);
        return scratch;
    }
    Table *result = newLeaf(count, type);
    std::memcpy(result->keys(), scratch->keys(), count * sizeof(slot_t));
    if (type->isMap()) {
        std::memcpy(result->values(), scratch->values(), count * sizeof(slot_t));
    }
    barrierAll(result, type);
    return result;
}

Table *mergeLeaves(const Table *a, const Table *b, const CollectionType *type) {
    const bool map      = type->isMap();
    const Type *keyType = type->keyType();
    Table *scratch      = newLeaf(a->size() + b->size(), type);
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

Table *intersectLeaves(const Table *a, const Table *b, const CollectionType *type) {
    const Type *keyType = type->keyType();
    Table *scratch      = newLeaf(std::min(a->size(), b->size()), type);
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

Table *differenceLeaves(const Table *a, const Table *b, const CollectionType *type) {
    const Type *keyType = type->keyType();
    Table *scratch      = newLeaf(a->size(), type);
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

bool isSubsetOfLeaf(const Table *a, const Table *b, const Type *keyType) {
    size_t j = 0;
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

} // namespace

Table *merge(const Table *a, const Table *b, const type::CollectionType *type) {
    if (b->size() == 0) {
        return const_cast<Table *>(a);
    }
    if (a->size() == 0) {
        return const_cast<Table *>(b);
    }
    if (a->isLeaf() && b->isLeaf()) {
        return checked(normalized(mergeLeaves(a, b, type), type), type);
    }
    const Type *keyType = type->keyType();
    if (muchSmaller(b, a)) {
        // The few entries of `b` put into `a`.
        Table *result = const_cast<Table *>(a);
        for (Cursor from(b, type); !from.done(); from.next()) {
            result = insert(result, from.key(), from.value(), type);
        }
        return checked(result, type);
    }
    if (muchSmaller(a, b)) {
        // The few entries of `a` whose key `b` does not have, put into `b`.
        Table *result = const_cast<Table *>(b);
        for (Cursor from(a, type); !from.done(); from.next()) {
            if (!has(b, from.key(), keyType)) {
                result = insert(result, from.key(), from.value(), type);
            }
        }
        return checked(result, type);
    }
    Entries out(type);
    Cursor x(a, type), y(b, type);
    while (!x.done() && !y.done()) {
        const int order = compareKeys(x.key(), y.key(), keyType);
        if (order < 0) {
            out.push(x.key(), x.value());
            x.next();
        } else {
            out.push(y.key(), y.value());
            y.next();
            if (order == 0) {
                x.next();
            }
        }
    }
    for (; !x.done(); x.next()) {
        out.push(x.key(), x.value());
    }
    for (; !y.done(); y.next()) {
        out.push(y.key(), y.value());
    }
    return checked(out.build(heap()), type);
}

Table *intersect(const Table *a, const Table *b, const type::SetType *type) {
    if (a->isLeaf() && b->isLeaf()) {
        return intersectLeaves(a, b, type);
    }
    const Type *keyType = type->keyType();
    Entries out(type);
    if (muchSmaller(a, b) || muchSmaller(b, a)) {
        // The few elements of the smaller that the larger has too.
        const Table *small = a->size() <= b->size() ? a : b;
        const Table *large = small == a ? b : a;
        for (Cursor from(small, type); !from.done(); from.next()) {
            if (has(large, from.key(), keyType)) {
                out.push(from.key(), NullSlot);
            }
        }
        return checked(out.build(heap()), type);
    }
    Cursor x(a, type), y(b, type);
    while (!x.done() && !y.done()) {
        const int order = compareKeys(x.key(), y.key(), keyType);
        if (order < 0) {
            x.next();
        } else if (order > 0) {
            y.next();
        } else {
            out.push(x.key(), NullSlot);
            x.next();
            y.next();
        }
    }
    return checked(out.build(heap()), type);
}

Table *difference(const Table *a, const Table *b, const type::SetType *type) {
    if (b->size() == 0 || a->size() == 0) {
        return const_cast<Table *>(a);
    }
    if (a->isLeaf() && b->isLeaf()) {
        return differenceLeaves(a, b, type);
    }
    const Type *keyType = type->keyType();
    if (muchSmaller(b, a)) {
        // The few elements of `b` taken out of `a`.
        Table *result = const_cast<Table *>(a);
        for (Cursor from(b, type); !from.done(); from.next()) {
            result = removeKey(result, from.key(), type);
        }
        return checked(result, type);
    }
    Entries out(type);
    if (muchSmaller(a, b)) {
        for (Cursor from(a, type); !from.done(); from.next()) {
            if (!has(b, from.key(), keyType)) {
                out.push(from.key(), NullSlot);
            }
        }
        return checked(out.build(heap()), type);
    }
    Cursor x(a, type), y(b, type);
    while (!x.done()) {
        const int order = y.done() ? -1 : compareKeys(x.key(), y.key(), keyType);
        if (order < 0) {
            out.push(x.key(), NullSlot);
            x.next();
        } else if (order > 0) {
            y.next();
        } else {
            x.next();
            y.next();
        }
    }
    return checked(out.build(heap()), type);
}

bool isSubset(const Table *a, const Table *b, const type::SetType *type) {
    if (a->size() > b->size()) {
        return false;
    }
    const Type *keyType = type->keyType();
    if (a->isLeaf() && b->isLeaf()) {
        return isSubsetOfLeaf(a, b, keyType);
    }
    if (muchSmaller(a, b)) {
        for (Cursor from(a, type); !from.done(); from.next()) {
            if (!has(b, from.key(), keyType)) {
                return false;
            }
        }
        return true;
    }
    Cursor y(b, type);
    for (Cursor x(a, type); !x.done(); x.next()) {
        while (!y.done() && compareKeys(y.key(), x.key(), keyType) < 0) {
            y.next();
        }
        if (y.done() || compareKeys(y.key(), x.key(), keyType) != 0) {
            return false;
        }
        y.next();
    }
    return true;
}

namespace {

/// A new array of type `arrayType` holding the keys of `t`, or its values, in key order.
Array *arrayOf(const Table *t, bool values, const type::ArrayType *arrayType) {
    Array *result = Array::create(heap(), arrayType, t->size());
    if (t->size() == 0) {
        return result;
    }
    const bool traced = arrayType->elemType()->isGCTraced();
    size_t at         = 0;
    forEachLeaf(t, [&](const Table *leaf) {
        const slot_t *slots = values ? leaf->values() : leaf->keys();
        const size_t n      = leaf->size();
        if (!traced) {
            for (size_t i = 0; i < n; ++i) {
                result->setSlot(at + i, slots[i]);
            }
        } else {
            for (size_t i = 0; i < n; ++i) {
                result->set<slot_t>(at + i, slots[i], arrayType);
            }
        }
        at += n;
    });
    return result;
}

} // namespace

Array *keys(const Table *t, const type::CollectionType *type) {
    return arrayOf(t, false, type->keyArrayType());
}

Array *values(const Table *t, const type::MapType *type) {
    return arrayOf(t, true, type->valueArrayType());
}

Array *items(const Table *t, const type::MapType *type) {
    const type::TupleType *item  = type->itemType();
    const type::ArrayType *array = type->sourceArrayType();
    Array *result                = Array::create(heap(), array, t->size());
    size_t at                    = 0;
    forEachLeaf(t, [&](const Table *leaf) {
        for (size_t i = 0; i < leaf->size(); ++i) {
            Tuple *entry = Tuple::create(2, heap());
            entry->set<slot_t>(0, leaf->keys()[i], item);
            entry->set<slot_t>(1, leaf->values()[i], item);
            result->set<Object *>(at++, entry, array);
        }
    });
    return result;
}

void forEachEntry(
    const Table *t, const type::CollectionType *type,
    const std::function<void(slot_t, slot_t)> &visit) {
    const bool map = type->isMap();
    forEachLeaf(t, [&](const Table *leaf) {
        for (size_t i = 0; i < leaf->size(); ++i) {
            visit(leaf->keys()[i], map ? leaf->values()[i] : NullSlot);
        }
    });
}

void forEachSlot(
    const Table *t, const type::CollectionType *type,
    const std::function<void(slot_t, type::Type *)> &visit) {
    forEachLeaf(t, [&](const Table *leaf) {
        for (size_t i = 0; i < leaf->size(); ++i) {
            visit(leaf->keys()[i], type->keyType());
        }
        if (type->isMap()) {
            for (size_t i = 0; i < leaf->size(); ++i) {
                visit(leaf->values()[i], type->valueType());
            }
        }
    });
}

Table *withSlots(
    const Table *t, const type::CollectionType *type, mm::IAllocator &allocator,
    const std::function<slot_t(slot_t, type::Type *)> &rewrite) {
    Entries out(type);
    out.keys.reserve(t->size());
    if (out.map) {
        out.values.reserve(t->size());
    }
    bool changed = false;
    forEachLeaf(t, [&](const Table *leaf) {
        for (size_t i = 0; i < leaf->size(); ++i) {
            out.keys.push_back(rewrite(leaf->keys()[i], type->keyType()));
            changed |= out.keys.back() != leaf->keys()[i];
        }
        if (out.map) {
            for (size_t i = 0; i < leaf->size(); ++i) {
                out.values.push_back(rewrite(leaf->values()[i], type->valueType()));
                changed |= out.values.back() != leaf->values()[i];
            }
        }
    });
    if (!changed) {
        return const_cast<Table *>(t);
    }
    return checked(out.build(allocator), type);
}

} // namespace camel::core::rtdata::table
