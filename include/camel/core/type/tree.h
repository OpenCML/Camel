// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The one definition of "what is a tree and what is a leaf" (design principles 13.5).
 *
 * A parameter tree is a value whose type is made of structs and tuples. An inner node is a
 * struct or an ordinary tuple (not a `T?`/`T!`, which are tagged tuples, and not a mutable
 * cell): those, and everything else, are leaves. An empty struct or tuple is an inner node
 * with no leaves, everywhere. Leaves come depth first, in declaration order, which is part
 * of the semantics.
 *
 * The path of a node is the field names (a tuple's indices) joined by dots, `blocks.0.w`; the
 * root's path is the empty string. `tree_paths`, the checkpoint files, and the diagnostics
 * about a leaf all use these paths. Which leaves a module accepts is for the module to say.
 */

#pragma once

#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"

#include <optional>
#include <string>
#include <utility>

namespace camel::core::type::tree {

/// Whether `type` is an inner node of a parameter tree.
inline bool isInternal(const Type *type) {
    if (type->code() == TypeCode::Struct)
        return true;
    if (type->code() != TypeCode::Tuple)
        return false;
    const auto *tuple = static_cast<const TupleType *>(type);
    return !tuple->isTagged() && !tuple->isCell();
}

/// The number of children of `type`, when `type` is the kind of type that has children -- a
/// `Struct` or a `Tuple`: **the only types this answers for.** Every other type -- an array, a
/// function, a number, a string, `nullptr` -- gets `std::nullopt`, because the question has no
/// answer there. It used to answer one anyway: every type that is not a `Struct` was cast to
/// `TupleType` and its member count read out of that object's bytes, so `arity(f32[3])` answered
/// `131961841501032` -- another object's bytes read as a count, with no error and no default. A
/// default can be argued with; another object's bytes cannot.
///
/// The no-answer is a *return value*, not an `assert`, on purpose: **an `assert` is compiled out of
/// the release build** (`NDEBUG`), so a guard written that way is loud on a developer's machine and
/// absent for every user -- which is worse than no guard, because the code then looks protected.
/// The empty value travels in every build, and a caller cannot take the count without saying what
/// it does when there is none.
///
/// **There is one `arity`, not two** (author's ruling, `a2cc57100`): a second name that still
/// answers a number keeps the defect alive for whoever reaches for it, and that caller gets no
/// diagnosis. A caller that has already established the type is a struct or an ordinary tuple may
/// say so and unwrap; a caller that has not must handle the empty value.
inline std::optional<size_t> arity(const Type *type) {
    if (type == nullptr) {
        return std::nullopt;
    }
    const TypeCode code = type->code();
    if (code == TypeCode::Struct) {
        return static_cast<const StructType *>(type)->size();
    }
    if (code == TypeCode::Tuple) {
        return static_cast<const TupleType *>(type)->size();
    }
    return std::nullopt;
}

inline Type *childType(const Type *type, size_t i) {
    return type->code() == TypeCode::Struct ? static_cast<const StructType *>(type)->typeAt(i)
                                            : static_cast<const TupleType *>(type)->typeAt(i);
}

/// The field name of child `i`, or its index for a tuple.
inline std::string childName(const Type *type, size_t i) {
    return type->code() == TypeCode::Struct
               ? std::string(static_cast<const StructType *>(type)->fieldName(i))
               : std::to_string(i);
}

/// The path of child `i` of the node at `parent`.
inline std::string childPath(const std::string &parent, const Type *type, size_t i) {
    return parent.empty() ? childName(type, i) : parent + "." + childName(type, i);
}

/// A path for a message: every message names a place, so the root -- whose path is empty -- is
/// shown as `<root>` rather than as nothing.
inline std::string displayPath(const std::string &path) {
    return path.empty() ? std::string("<root>") : path;
}

/// Calls `visit(path, leafType)` for every leaf of `type`, in the canonical order.
template <class Visit> void forEachLeaf(Type *type, Visit &&visit, const std::string &path = "") {
    const auto members = arity(type);
    // The empty answer is the leaf, which is what this walk does with every type that has no
    // members: the branch is the walk's own contract, not a case it skips. `isInternal` still
    // decides which types that *do* have members are inner nodes -- a tagged or cell tuple has
    // members and is a leaf.
    if (!members || !isInternal(type)) {
        visit(path, type);
        return;
    }
    for (size_t i = 0; i < *members; ++i)
        forEachLeaf(childType(type, i), visit, childPath(path, type, i));
}

/// The first leaf (path, type) for which `accepts(leafType)` is false, if any.
template <class Accepts>
std::optional<std::pair<std::string, Type *>> firstRejectedLeaf(Type *type, Accepts &&accepts) {
    std::optional<std::pair<std::string, Type *>> found;
    forEachLeaf(type, [&](const std::string &path, Type *leaf) {
        if (!found && !accepts(leaf))
            found = std::pair{path, leaf};
    });
    return found;
}

/// The same tree shape with every leaf replaced by `mapLeaf(path, leafType)`; nullptr when
/// `mapLeaf` returns nullptr for a leaf (it says why by itself).
template <class MapLeaf>
Type *mapLeaves(Type *type, MapLeaf &&mapLeaf, const std::string &path = "") {
    const auto members = arity(type);
    // As in `forEachLeaf`: no members means the type is a leaf, and the tag/cell rule belongs to
    // `isInternal`. `nullptr` takes the same branch instead of being dereferenced.
    if (!members || !isInternal(type))
        return mapLeaf(path, type);
    if (type->code() == TypeCode::Struct) {
        StructTypeFactory fields;
        for (size_t i = 0; i < *members; ++i) {
            Type *child = mapLeaves(childType(type, i), mapLeaf, childPath(path, type, i));
            if (!child)
                return nullptr;
            fields.add(childName(type, i), child);
        }
        return fields.build();
    }
    type_vec_t fields;
    for (size_t i = 0; i < *members; ++i) {
        Type *child = mapLeaves(childType(type, i), mapLeaf, childPath(path, type, i));
        if (!child)
            return nullptr;
        fields.push_back(child);
    }
    return TupleType::create(fields);
}

} // namespace camel::core::type::tree
