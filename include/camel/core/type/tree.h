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

/// The number of children of an inner node.
inline size_t arity(const Type *type) {
    return type->code() == TypeCode::Struct ? static_cast<const StructType *>(type)->size()
                                            : static_cast<const TupleType *>(type)->size();
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

/// A path for a message: the root, whose path is empty, is written `<root>`.
inline std::string displayPath(const std::string &path) {
    return path.empty() ? std::string("<root>") : path;
}

/// Calls `visit(path, leafType)` for every leaf of `type`, in the canonical order.
template <class Visit> void forEachLeaf(Type *type, Visit &&visit, const std::string &path = "") {
    if (!isInternal(type)) {
        visit(path, type);
        return;
    }
    for (size_t i = 0; i < arity(type); ++i)
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
    if (!isInternal(type))
        return mapLeaf(path, type);
    if (type->code() == TypeCode::Struct) {
        StructTypeFactory fields;
        for (size_t i = 0; i < arity(type); ++i) {
            Type *child = mapLeaves(childType(type, i), mapLeaf, childPath(path, type, i));
            if (!child)
                return nullptr;
            fields.add(childName(type, i), child);
        }
        return fields.build();
    }
    type_vec_t fields;
    for (size_t i = 0; i < arity(type); ++i) {
        Type *child = mapLeaves(childType(type, i), mapLeaf, childPath(path, type, i));
        if (!child)
            return nullptr;
        fields.push_back(child);
    }
    return TupleType::create(fields);
}

} // namespace camel::core::type::tree
