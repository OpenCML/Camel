// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
 *                                 loops it runs (effectwalk::ofGraph). Encodable.
 *   core.nodeEffects  Node, (ii)  what running one node does besides computing its value: an
 *                                 operator's record, the graph a call names, the conservative
 *                                 answer for a call through a value, fault and non-termination for
 *                                 a loop. Read on demand; nothing reads it in bulk.
 *   core.regionEffects Node, (ii) the regions a control node runs -- a JOIN's arms, a LOOP's body
 *                                 and condition -- each with its own summary. A region is not an
 *                                 anchor a fact can name, so the answer is kept at its owner.
 *   core.operatorEffects Node, (ii) the effect of the operator a node runs, read at the node: the
 *                                 accesses its record states united with the ones its function
 *                                 operand brings. What `graph.is_pure` asks for.
 *   core.loopDomain   CrossGraph, (ii)  a LOOP node and its body graph (design 11.3): the form,
 *                                 the Sequential dim of For/ForWhile, `t < len(xs)`, with the
 *                                 extent — `shape[0]`, the rows of a constant array — and the
 *                                 element port of the body graph that reads xs(t); While has no
 *                                 bound.
 *   core.permissions  Node, (i)   the numeric permissions the program granted the node: graph
 *                                 content (GCGraph::nodePermissions), read, never stored.
 *                                 Encodable.
 *   core.resources    Node, (ii)  what the node does to resources besides computing its value
 *                                 (camel/runtime/resources.h, resourcesOf): its accesses, each
 *                                 with the node whose value names the resource, how its result
 *                                 relates to them, what it allocates, whether it has an external
 *                                 effect. Only at the nodes that touch something. Reads
 *                                 declarations; reads the graphs a FUNC, CALL or LOOP runs (the
 *                                 anchor's revision covers them).
 *
 * The prefix `tiles.` is reserved to the module tiles (facts.h, registerKind): its analyses
 * register their own kinds and store them with their target's hash (class iii).
 */

#pragma once

#include "camel/core/index_semantics.h"
#include "camel/core/loop_kind.h"
#include "camel/core/type/base.h"
#include "camel/runtime/effects.h"
#include "camel/runtime/facts.h"
#include "camel/runtime/loop.h"
#include "camel/runtime/resources.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace camel::runtime::fact {

/// The value of core.shape. `dims` is nullopt when the rank is unknown; an extent nobody stated is
/// an absent entry of the vector (plan section 5.3 of the arrays-memory design: the transitional
/// `-1` sentinel is gone). The text form of the kind (`encode`/`decode`) still spells an absent
/// extent as `-1`, which is the spelling its readers already parse.
struct TensorShape {
    std::optional<camel::core::type::TypeCode> dtype;
    std::optional<std::vector<std::optional<int64_t>>> dims;
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
    /// For, ForWhile: how many elements the loop takes from that array along its first dimension
    /// when the array is a constant of the graph — `shape[0]`, the element count of a rank-1
    /// array and the row count of a higher-rank one (design principles 5.4: the extent is
    /// `shape[0]`, one fact for every reader). The
    /// iterable of a loop over an array is the array itself (the core takes an array's elements,
    /// no module's `__iter__`), so a constant array gives its own length.
    std::optional<int64_t> extent;
    /// For, ForWhile: the body graph's port of the element, which reads xs(t).
    std::optional<gc_node_ref_t> elementPort;
    bool operator==(const LoopDomain &) const = default;
};

/// The ids of the core's kinds (registered on the first call).
struct CoreKinds {
    KindId shape, instance, reorder, effects, nodeEffects, regionEffects, operatorEffects,
        loopDomain, permissions, resources;
};
const CoreKinds &coreKinds();

/// The effects of running `graph`, a graph of `program`, once: the `core.effects` fact
/// (nullptr: none). This is the graph-level answer, through the graphs it calls and the loops it
/// runs.
EffectSummary graphEffects(FactStore &facts, const Program &program, const GCGraph *graph);
/// What running one node of `graph` does besides computing its value: the `core.nodeEffects` fact.
/// An operator's answer is its record's, a call's is the graph it names, a call through a value is
/// the conservative one, and a loop's is that it may fault and may not end. A branch, and a loop's
/// body, are regions of this graph, so what runs inside them is in `regionEffectsOf` and not here.
EffectSummary
nodeEffects(FactStore &facts, const Program &program, const GCGraph &graph, gc_node_ref_t node);

/// The effects of the regions a control node runs (`core.regionEffects`): a JOIN's arms, a LOOP's
/// body and condition. Empty for every other node. A region is not an anchor a fact can name, so
/// the answer is stored at the node that owns the regions and each field is the region's own
/// summary.
struct RegionEffects {
    /// JOIN: the union of its arms.
    EffectSummary arms;
    /// LOOP: its body region.
    EffectSummary body;
    /// LOOP: its condition region.
    EffectSummary cond;
    bool operator==(const RegionEffects &) const = default;
};
RegionEffects regionEffectsOf(
    FactStore &facts, const Program &program, const GCGraph &graph, gc_node_ref_t node);

/// The effect of the operator the node runs, read at the node: the `core.operatorEffects` fact, and
/// the answer `graph.is_pure` asks for. It is the same reading `core.nodeEffects` takes at an
/// operation node -- the accesses the declaration states united with the ones the node's function
/// operand brings (design principles 3.3) -- so the two facts cannot disagree about that node.
EffectSummary operatorEffectsOf(
    FactStore &facts, const Program &program, const GCGraph &graph, gc_node_ref_t node);

/// Text forms of the values (what camel inspect --facts prints as "value").
std::string toString(const TensorShape &shape);
std::string toString(const Reorder &reorder);
std::string toString(const EffectSummary &effects);
std::string toString(const RegionEffects &effects);
std::string toString(const LoopDomain &domain);

} // namespace camel::runtime::fact
