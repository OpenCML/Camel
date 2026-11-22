// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The run-time object of Map and Set: see camel/core/rtdata/collection.h. This file is the
 * object itself (what the collector, printing, equality and copying ask of a table) and the
 * order of keys; the tree is in collection_tree.cpp and the operations in collection_ops.cpp.
 */

#include "camel/core/rtdata/collection.h"

#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/rtdata/variant.h"
#include "camel/core/type/enum.h"
#include "collection_tree.h"

#include <algorithm>
#include <cstring>

namespace camel::core::rtdata {

namespace {

using namespace table::detail;

const CollectionType *collectionTypeOf(const Type *type) {
    const CollectionType *result = type::asCollectionType(type);
    GUARD(result != nullptr, "A Table needs its Map or Set type, and the type check returned none: the table has no layout to place entries in");
    return result;
}

} // namespace

// ---- Table ----

Table *Table::create(size_t size, size_t slotCount, mm::IAllocator &allocator) {
    GUARD(slotCount >= size, "A leaf has one or two slots per entry, and this leaf declares fewer slots than entries: two entries would share a slot and the second write would be read back as the first");
    void *memory = allocator.alloc(sizeof(Table) + slotCount * sizeof(slot_t), alignof(Table));
    if (!memory) {
        throw std::bad_alloc();
    }
    Table *t = new (memory) Table(size, 0, 0);
    std::fill(t->data_, t->data_ + slotCount, NullSlot);
    return t;
}

Table *Table::createBranch(
    size_t count, size_t height, size_t size, size_t width, mm::IAllocator &allocator) {
    GUARD(height > 0 && count > 0 && count <= width, "Not the shape of a branch: a branch needs a positive height, at least one entry, and no more entries than its width, because the walk uses the count as the number of live arms");
    const size_t slotCount = 2 * width;
    void *memory = allocator.alloc(sizeof(Table) + slotCount * sizeof(slot_t), alignof(Table));
    if (!memory) {
        throw std::bad_alloc();
    }
    Table *t = new (memory) Table(size, height, count);
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
    const Type *keyType   = collType->keyType();
    const Type *valueType = collType->valueType();
    if (!isLeaf() || !rhs->isLeaf()) {
        // Two trees with the same entries may differ in shape: compare entry by entry.
        Cursor a(this, collType), b(rhs, collType);
        for (; !a.done(); a.next(), b.next()) {
            if (compareKeys(a.key(), b.key(), keyType) != 0) {
                return false;
            }
            if (!collType->isMap()) {
                continue;
            }
            if (deep ? !valuesEqual(a.value(), b.value(), valueType) : a.value() != b.value()) {
                return false;
            }
        }
        return true;
    }
    for (size_t i = 0; i < size_; ++i) {
        if (compareKeys(keys()[i], rhs->keys()[i], keyType) != 0) {
            return false;
        }
    }
    if (!collType->isMap()) {
        return true;
    }
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
    const auto copyOf              = [&](slot_t slot, const Type *slotType) {
        if (deep && slotType->isGCTraced()) {
            if (Object *ref = fromSlot<Object *>(slot)) {
                return toSlot(ref->clone(allocator, slotType, true));
            }
        }
        return slot;
    };
    // Every table of the copy is new, so that the copy lies in `allocator` as a whole.
    Entries entries(collType);
    entries.keys.reserve(size_);
    if (entries.map) {
        entries.values.reserve(size_);
    }
    forEachLeaf(this, [&](const Table *leaf) {
        for (size_t i = 0; i < leaf->size(); ++i) {
            entries.keys.push_back(copyOf(leaf->keys()[i], collType->keyType()));
        }
        if (entries.map) {
            for (size_t i = 0; i < leaf->size(); ++i) {
                entries.values.push_back(copyOf(leaf->values()[i], collType->valueType()));
            }
        }
    });
    return entries.build(allocator);
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
    bool first = true;
    forEachLeaf(this, [&](const Table *leaf) {
        for (size_t i = 0; i < leaf->size(); ++i) {
            if (!first) {
                os << ", ";
            }
            first = false;
            printSlot(os, leaf->keys()[i], collType->keyType());
            if (collType->isMap()) {
                os << ": ";
                printSlot(os, leaf->values()[i], collType->valueType());
            }
        }
    });
    os << "}";
}

// Nothing derives from the address of a table.
void Table::onMoved() {}

void Table::updateRefs(const RefRelocator &relocate, const type::Type *type) {
    const CollectionType *collType = type::asCollectionType(type);
    if (collType == nullptr) {
        return;
    }
    const auto trace =
        [&](slot_t *slots, size_t count, const Type *slotType, std::string_view name) {
            if (slotType == nullptr || !slotType->isGCTraced()) {
                return;
            }
            Object **refs = reinterpret_cast<Object **>(slots);
            for (size_t i = 0; i < count; ++i) {
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
    if (isLeaf()) {
        trace(keys(), size_, collType->keyType(), "key");
        if (collType->isMap()) {
            trace(values(), size_, collType->valueType(), "value");
        }
        return;
    }
    // A child is a table of the same collection type: the collector traces it with the type
    // this table was traced with, and so on down to the leaves.
    trace(keys(), count_, collType->keyType(), "separator");
    trace(data_ + count_, count_, type, "child");
}

// ---- Order and equality ----

// Design principles 11.3: a collection is ordered by its keys, and this function is that
// order. Everything that shows an order (keys, values, items, printing) and equality follow
// it, so a collection is determined by its entries and by nothing else.
int compareKeys(slot_t a, slot_t b, const type::Type *keyType) {
    const auto three = [](auto x, auto y) { return x < y ? -1 : (y < x ? 1 : 0); };
    if (const auto *enumType = type::asEnumType(keyType); enumType != nullptr) {
        // Design principles 11.3: variants in the order they are declared, then field by field.
        const Variant *lhs = fromSlot<Variant *>(a);
        const Variant *rhs = fromSlot<Variant *>(b);
        if (const int byTag = three(lhs->tag(), rhs->tag()); byTag != 0) {
            return byTag;
        }
        const auto &fields = enumType->variant(lhs->tag()).fieldTypes;
        for (size_t i = 0; i < fields.size(); ++i) {
            const int order = compareKeys(lhs->get(i), rhs->get(i), fields[i]);
            if (order != 0) {
                return order;
            }
        }
        return 0;
    }
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
        // Unreachable, and nothing is checked here: collectionKeyProblem (core/type/collection)
        // admits only int, i32, bool, string, tuples of those and enums of those, which are
        // exactly the cases above, so no collection value carries a key type this switch does
        // not order. There is no call that could reach this branch, so there is none to report.
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
} // namespace camel::core::rtdata
