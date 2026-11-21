// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The vocabulary the writing engine's own files share. None of it is public: the public
 * entry stays include/camel/runtime/writings.h, and a name lands here only because two of
 * the engine's files read it -- the canonical form of a constant, the text of a node, the
 * type a rewriting keeps, and the facts about an operator that the matcher and its
 * diagnostics both ask.
 */

#pragma once

#include "camel/runtime/writings.h"
#include "camel/runtime/graph_txn.h"

#include "camel/core/type/intern.h"

#include <cstdint>
#include <string>

namespace camel::runtime {

using camel::core::type::Type;

/// What makes two nodes the same value when their inputs are.
struct NodeKey {
    uint8_t kind                           = 0;
    camel::core::sem::PermissionMask perms = 0;
    uint32_t region                        = 0;
    /// The canonical id of the node's type (camel/core/type/intern.h), kNoTypeId when it has
    /// none: one id per structure, so two nodes whose types were built in different places, or
    /// whose types spell their dimensions differently, are the same key.
    camel::core::type::TypeId typeId = camel::core::type::kNoTypeId;
    std::string payload;
    std::vector<gc_node_ref_t> norm;
    std::vector<gc_node_ref_t> with;
    bool operator==(const NodeKey &) const = default;
};

struct NodeKeyHash {
    size_t operator()(const NodeKey &key) const {
        size_t h = std::hash<std::string>{}(key.payload);
        auto mix = [&h](size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
        mix(key.kind);
        mix(key.perms);
        mix(key.region);
        mix(key.typeId);
        for (gc_node_ref_t in : key.norm) {
            mix(in);
        }
        mix(0xffffffffu);
        for (gc_node_ref_t in : key.with) {
            mix(in);
        }
        return h;
    }
};

struct DraftWritings::Index {
    std::unordered_map<NodeKey, gc_node_ref_t, NodeKeyHash> byKey;
    std::vector<std::optional<NodeKey>> keyOf;
    // 0 unknown, 1 takes part, 2 does not.
    std::vector<uint8_t> takes;
    // 0 unknown, 1 not written in place, 2 written.
    std::vector<uint8_t> written;
};

namespace detail {

/// The type a replacement writing gets: the node's own type when the pattern accepts it.
camel::core::type::Type *writingType(
    const camel::core::type::Type *patternType, const camel::core::type::Type *nodeType);

/// The text of a node while the draft still has it.
std::string nodeText(const GraphTransaction &transaction, gc_node_ref_t id);

/// The bits of a constant that are its value.
slot_t canonicalConstant(slot_t value, const camel::core::type::Type *type);

/// Whether two constants are the same value of the same type.
bool sameConstantValue(
    slot_t a, const camel::core::type::Type *ta, slot_t b, const camel::core::type::Type *tb);

/// The bytes a constant is keyed and compared by.
std::string constantPayload(slot_t value, const camel::core::type::Type *type);

/// The canonical id of `type`, `kNoTypeId` when it has none.
camel::core::type::TypeId canonicalTypeIdOf(const camel::core::type::Type *type);

/// Whether the operator's declared semantics say exchanging its operands gives the same value.
bool declaredCommutative(camel::core::OperId oper, const camel::core::type::Type *type);

/// Whether the operator's declared semantics say regrouping gives the same value.
bool declaredAssociative(camel::core::OperId oper, const camel::core::type::Type *type);

/// Whether a pattern term of `patternType` may match a node of `nodeType`.
bool acceptsType(
    const camel::core::type::Type *patternType, const camel::core::type::Type *nodeType);

} // namespace detail

} // namespace camel::runtime
