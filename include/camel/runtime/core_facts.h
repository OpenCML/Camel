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
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The kinds of graph fact the core computes (camel/runtime/facts.h; docs/design/op-semantics,
 * sections 10 and 11.3). All are registered by the core with the owner `core`, the first time
 * coreKinds() is asked (Context::facts() asks it), and computed on demand:
 *
 *   core.shape        Node, (ii)  the tensor shape of the node's value: element type and
 *                                 extents, as the node's type carries them. The type is where
 *                                 the compiler's shape inference puts what it knows (the
 *                                 operator's type rule; specialization to the shapes of a call
 *                                 refines it). Read through a ShapeReader the module defining
 *                                 the tensor type registers. Its cone is the node's type: a
 *                                 rewrite that changes the graph but not that type keeps it
 *                                 (Dependency::Cone). Encodable.
 *   core.instance     Node, (ii)  the index instance of an OPER node whose operator declares
 *                                 index semantics (camel/runtime/operator_instance.h,
 *                                 instanceOf): the instance, or "none" when the facts at the
 *                                 node give none (cached too: asking again costs nothing).
 *                                 Reads declarations.
 *   core.reorder      Node, (ii)  what the reduction of such a node may do (reorderFreedomOf):
 *                                 the law with the permissions granted. Reads declarations.
 *                                 Encodable.
 *   core.effects      Graph, (ii) the effect summary of a graph, through what it calls and the
 *                                 loops it runs (effectSummaryOf). Encodable.
 *   core.loopDomain   CrossGraph, (ii)  a LOOP node and its body graph (design 11.3): the form,
 *                                 the Sequential dim of For/ForWhile, `t < len(xs)`, with the
 *                                 extent when the array is a constant, and the element port of
 *                                 the body graph that reads xs(t); While has no bound.
 *   core.permissions  Node, (i)   the numeric permissions the program granted the node: graph
 *                                 content (GCGraph::nodePermissions), read, never stored.
 *                                 Encodable.
 *   core.resources    Node, (ii)  what the node does to resources besides computing its value
 *                                 (camel/runtime/resources.h, resourcesOf): its accesses, each
 *                                 with the node whose value names the resource, how its result
 *                                 relates to them, what it allocates, whether it has an external
 *                                 effect. Only at the nodes that touch something. Reads
 *                                 declarations; reads the graphs a FUNC, CALL or LOOP runs (the
 *                                 anchor's serial covers them).
 *
 * The prefix `tiles.` is reserved to the module tiles (facts.h, registerKind): its analyses
 * register their own kinds and store them with their target's hash (class iii).
 */

#pragma once

#include "camel/core/index_semantics.h"
#include "camel/core/loop_kind.h"
#include "camel/core/type/base.h"
#include "camel/runtime/facts.h"
#include "camel/runtime/loop.h"
#include "camel/runtime/resources.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace camel::runtime::fact {

/// The value of core.shape. `dims` is nullopt when the rank is unknown; an unknown extent is -1.
struct TensorShape {
    std::optional<camel::core::type::TypeCode> dtype;
    std::optional<std::vector<int64_t>> dims;
    bool operator==(const TensorShape &) const = default;
};

/// How the core reads the shape of a value of a type it does not define (a tensor): the module
/// that defines the type registers one when it loads. nullopt: not a type of that module.
using ShapeReader = std::function<std::optional<TensorShape>(const camel::core::type::Type *)>;
void registerShapeReader(ShapeReader reader);
/// The shape of a value of `type`, by the registered readers; nullopt when none knows it.
std::optional<TensorShape> shapeOfType(const camel::core::type::Type *type);

/// The value of core.instance: the instance, nullopt when the facts at the node give none;
/// `text` is its canonical text (sem::toString), which is what two instances are compared by.
struct NodeInstance {
    std::optional<camel::core::sem::IndexedInstance> instance;
    std::string text;
    bool operator==(const NodeInstance &other) const { return text == other.text; }
};

/// The value of core.reorder.
struct Reorder {
    bool regroup = false, permute = false, blockIdentity = false, contract = false,
         changePrecision = false;
    bool operator==(const Reorder &) const = default;
};

/// The value of core.loopDomain (design 11.3).
struct LoopDomain {
    camel::core::LoopKind kind = camel::core::LoopKind::While;
    /// For, ForWhile: the node (of the graph with the LOOP) that is the array iterated over.
    std::optional<gc_node_ref_t> iterable;
    /// For, ForWhile: len(xs) when the array is a constant of the graph.
    std::optional<int64_t> extent;
    /// For, ForWhile: the body graph's port of the element, which reads xs(t).
    std::optional<gc_node_ref_t> elementPort;
    bool operator==(const LoopDomain &) const = default;
};

/// The ids of the core's kinds (registered on the first call).
struct CoreKinds {
    KindId shape, instance, reorder, effects, loopDomain, permissions, resources;
};
const CoreKinds &coreKinds();

/// Text forms of the values (what camel inspect --facts prints as "value").
std::string toString(const TensorShape &shape);
std::string toString(const Reorder &reorder);
std::string toString(const EffectSummary &effects);
std::string toString(const LoopDomain &domain);

} // namespace camel::runtime::fact
