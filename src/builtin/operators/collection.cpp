// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Operators over Map and Set; see collection.h. The work is in rtdata::table
 * (camel/core/rtdata/collection.h); this file is the argument plumbing and the typing.
 *
 * An operator reads the static type of its collection argument (ArgsView::type): a table
 * carries no type, and the type is what says whether it is a map and how its keys compare.
 */

#include "collection.h"
#include "camel/core/context/context.h"
#include "camel/core/rtdata/collection.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/resolver.h"
#include "camel/core/type/text.h"

#include <functional>
#include <optional>

using namespace camel::core::context;
using namespace camel::core::rtdata;
using namespace camel::core::type;

namespace {

const CollectionType *collTypeOf(ArgsView &args, size_t index) {
    const CollectionType *type = asCollectionType(args.type(index));
    ASSERT(type != nullptr, "A collection operator was given a value that is not a Map or a Set.");
    return type;
}

const MapType *mapTypeOf(ArgsView &args, size_t index) {
    return static_cast<const MapType *>(collTypeOf(args, index));
}

const SetType *setTypeOf(ArgsView &args, size_t index) {
    return static_cast<const SetType *>(collTypeOf(args, index));
}

/// `m[k]`: null when absent, otherwise the value in the form the lookup type has
/// (DESIGN-QUESTION(map-nullable-value)).
slot_t lookup(const Table *table, slot_t key, const MapType *type) {
    slot_t value = NullSlot;
    if (!table::lookup(table, key, type->keyType(), value)) {
        return NullSlot;
    }
    if (!type->lookupBoxes()) {
        return value;
    }
    return makeSome(value, static_cast<const TupleType *>(type->lookupType()));
}

/// Argument `index` of those that configure an operation (the key, the value). A program
/// writes them as `with` arguments, `put<k, v>(m)` (design principles 11.4: the one calling
/// form); the other branch only serves a caller inside the runtime that passes them after
/// the collection.
slot_t configArg(ArgsView &with, ArgsView &norm, size_t index) {
    return with.size() > 0 ? with.slot(index) : norm.slot(index + 1);
}

} // namespace

slot_t __coll_map_idx__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return lookup(norm.get<Table *>(0), norm.slot(1), mapTypeOf(norm, 0));
}

slot_t __coll_map_get__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return lookup(norm.get<Table *>(0), configArg(with, norm, 0), mapTypeOf(norm, 0));
}

slot_t __coll_map_get_or__(ArgsView &with, ArgsView &norm, Context &ctx) {
    slot_t value     = NullSlot;
    const bool found = table::lookup(
        norm.get<Table *>(0),
        configArg(with, norm, 0),
        mapTypeOf(norm, 0)->keyType(),
        value);
    return found ? value : configArg(with, norm, 1);
}

slot_t __coll_map_put__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(
        table::put(
            norm.get<Table *>(0),
            configArg(with, norm, 0),
            configArg(with, norm, 1),
            mapTypeOf(norm, 0)));
}

slot_t __coll_set_add__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(table::add(norm.get<Table *>(0), configArg(with, norm, 0), setTypeOf(norm, 0)));
}

slot_t __coll_remove__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(
        table::remove(norm.get<Table *>(0), configArg(with, norm, 0), collTypeOf(norm, 0)));
}

slot_t __coll_has__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot<Bool>(
        table::has(norm.get<Table *>(0), configArg(with, norm, 0), collTypeOf(norm, 0)->keyType()));
}

slot_t __coll_len__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(static_cast<Int64>(norm.get<Table *>(0)->size()));
}

slot_t __coll_keys__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(table::keys(norm.get<Table *>(0), collTypeOf(norm, 0)));
}

slot_t __coll_map_values__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(table::values(norm.get<Table *>(0), mapTypeOf(norm, 0)));
}

slot_t __coll_map_items__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(table::items(norm.get<Table *>(0), mapTypeOf(norm, 0)));
}

slot_t __coll_merge__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(table::merge(norm.get<Table *>(0), norm.get<Table *>(1), collTypeOf(norm, 0)));
}

slot_t __coll_set_intersect__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(
        table::intersect(norm.get<Table *>(0), norm.get<Table *>(1), setTypeOf(norm, 0)));
}

slot_t __coll_set_difference__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(
        table::difference(norm.get<Table *>(0), norm.get<Table *>(1), setTypeOf(norm, 0)));
}

slot_t __coll_set_is_subset__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot<Bool>(
        table::isSubset(norm.get<Table *>(0), norm.get<Table *>(1), setTypeOf(norm, 0)));
}

slot_t __coll_eq__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot<Bool>(norm.get<Table *>(0)->equals(norm.get<Table *>(1), norm.type(0), true));
}

slot_t __coll_neq__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot<Bool>(!norm.get<Table *>(0)->equals(norm.get<Table *>(1), norm.type(0), true));
}

const std::vector<oper_group_ptr_t> &getCollectionOperatorGroups() {
    using std::nullopt;
    using std::optional;
    using resolver_fn =
        std::function<optional<Type *>(const type_vec_t &with, const type_vec_t &norm)>;
    // `withCount` with arguments and `normCount` ordinary ones, none of them `var`.
    const auto typed = [](int withCount, int normCount, const char *signature, resolver_fn fn) {
        return DynamicFuncTypeResolver::create(
            {{withCount, std::vector<bool>(withCount, false)},
             {normCount, std::vector<bool>(normCount, false)}},
            signature,
            [fn = std::move(fn)](
                const type_vec_t &with,
                const type_vec_t &norm) -> optional<Type *> { return fn(with, norm); });
    };
    // An operation on one collection configured by `count` arguments: the collection is the
    // ordinary argument and what configures the operation is written in angle brackets,
    // `put<k, v>(m)` or `m->put<k, v>`, the one calling form of the standard library (design
    // principles 11.4). `fn` is handed the collection's type and the configuring types.
    using keyed_fn = std::function<optional<Type *>(Type *coll, const type_vec_t &config)>;
    const auto keyed = [](size_t count, const char *signature, keyed_fn fn) {
        return DynamicFuncTypeResolver::create(
            {{static_cast<int>(count), std::vector<bool>(count, false)}, {1, {false}}},
            signature,
            [count, fn = std::move(fn)](
                const type_vec_t &with,
                const type_vec_t &norm) -> optional<Type *> {
                if (with.size() == count && norm.size() == 1) {
                    return fn(norm[0], with);
                }
                return nullopt;
            });
    };
    // A key argument must have exactly the key type: nothing is converted.
    const auto mapWithKey = [](Type *m, Type *key) -> const MapType * {
        const MapType *map = asMapType(m);
        return map != nullptr && map->keyType()->equals(key) ? map : nullptr;
    };
    const auto collWithKey = [](Type *c, Type *key) -> const CollectionType * {
        const CollectionType *coll = asCollectionType(c);
        return coll != nullptr && coll->keyType()->equals(key) ? coll : nullptr;
    };
    // Two collections of one type.
    const auto sameCollections = [](const type_vec_t &norm) -> const CollectionType * {
        const CollectionType *coll = asCollectionType(norm[0]);
        return coll != nullptr && coll->equals(norm[1]) ? coll : nullptr;
    };
    const auto sameSets = [=](const type_vec_t &norm) -> const CollectionType * {
        const CollectionType *coll = sameCollections(norm);
        return coll != nullptr && !coll->isMap() ? coll : nullptr;
    };
    const auto itself = [](const CollectionType *type) -> optional<Type *> {
        return type ? optional<Type *>(const_cast<CollectionType *>(type)) : nullopt;
    };
    const auto boolIf = [](bool matches) -> optional<Type *> {
        return matches ? optional<Type *>(Type::Bool()) : nullopt;
    };
    const auto setOperation = [=](const char *signature) {
        return typed(0, 2, signature, [=](const type_vec_t &, const type_vec_t &norm) {
            return itself(sameSets(norm));
        });
    };

    // `items(m)`: the (key, value) pairs in key order (design principles 11.3). The loop
    // builder uses the same operator for the elements a `for` takes from a map.
    const auto mapItems = typed(
        0,
        1,
        "(m: Map<K, V>) => (K, V)[]",
        [](const type_vec_t &, const type_vec_t &norm) -> optional<Type *> {
            const MapType *map = asMapType(norm[0]);
            return map ? optional<Type *>(map->sourceArrayType()) : nullopt;
        });

    static const std::vector<oper_group_ptr_t> groups = {
        // ======= Lookup =======
        // `m[k]`: the subscript of a map. It has type `V?` and never faults, where the
        // subscript of an array has type `T` and faults out of range; the overload is chosen
        // by the type of the left operand (DESIGN-QUESTION(map-subscript)).
        OperatorGroup::create(
            "__idx__",
            {{":coll/map_idx",
              typed(
                  0,
                  2,
                  "(m: Map<K, V>, key: K) => V?",
                  [=](const type_vec_t &, const type_vec_t &norm) -> optional<Type *> {
                      const MapType *map = mapWithKey(norm[0], norm[1]);
                      return map ? optional<Type *>(map->lookupType()) : nullopt;
                  })}}),
        // The rules of `get` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("get", {}),
        // The rules of `get_or` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("get_or", {}),
        // The rules of `has` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("has", {}),
        // `contains<x>(s)`, beside the array overload `contains<v>(xs)`.
        // The rules of `contains` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("contains", {}),
        // The rules of `len` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("len", {}),
        // ======= New values =======
        // The rules of `put` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("put", {}),
        // The rules of `add` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("add", {}),
        // The rules of `remove` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("remove", {}),
        // The rules of `merge` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("merge", {}),
        // The rules of `union` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("union", {}),
        // The rules of `intersect` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("intersect", {}),
        // The rules of `difference` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("difference", {}),
        // The rules of `is_subset` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("is_subset", {}),
        // ======= To arrays, in key order =======
        // The rules of `keys` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("keys", {}),
        // The rules of `values` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("values", {}),
        // The rules of `items` are the declarations of stdlib/prelude.cml.
        OperatorGroup::create("items", {}),
        // The elements a `for` takes from a map: its (key, value) pairs in key order
        // (design principles 11.3). The loop builder converts a map to that array once; a
        // program does not write `__iter__` itself. A string adds its own overload
        // (globals.cpp); a tensor is a numeric array, which the core iterates itself, so no
        // module adds one for it.
        OperatorGroup::create("__iter__", {{":coll/map_items", mapItems}}),
        // ======= Equality (DESIGN-QUESTION(map-equality-print)) =======
        OperatorGroup::create(
            "__eq__",
            {{":coll/eq",
              typed(
                  0,
                  2,
                  "(a: Map<K, V> | Set<T>, b: Map<K, V> | Set<T>) => bool",
                  [=](const type_vec_t &, const type_vec_t &norm) -> optional<Type *> {
                      return boolIf(sameCollections(norm) != nullptr);
                  })}}),
        OperatorGroup::create(
            "__neq__",
            {{":coll/neq",
              typed(
                  0,
                  2,
                  "(a: Map<K, V> | Set<T>, b: Map<K, V> | Set<T>) => bool",
                  [=](const type_vec_t &, const type_vec_t &norm) -> optional<Type *> {
                      return boolIf(sameCollections(norm) != nullptr);
                  })}}),
    };
    return groups;
}
