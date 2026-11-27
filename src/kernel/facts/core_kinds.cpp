// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The core's kinds of fact, as the facts framework declares them. The first to move is
 * `core.shape`, and it is the shape every later kind is written in: a computation reads the graph
 * through the view the store hands it (`GraphAccess::view`) and reports what it read into
 * `ReadLog`, so the fact is stale when that node's own content changed and not when anything else
 * in the graph did. `core.permissions` follows it, and is the kind the framework's own definition
 * of `Class::Content` names.
 *
 * `core.instance` and `core.reorder` follow, and they are the kinds that showed the read has to be
 * one: both ask what the node's operator declares *at that node*, which is instantiated from the
 * operand types and from the static arguments the constant operands hold. That question is now
 * answered off the view (`runtime::instanceOf(const GraphView &, NodeRef)`), and the frozen-graph
 * answers are thin shells over it, so the facts and the guard (an object constant counts only when
 * every user of it recomputes) have one body.
 *
 * The value, its text form and its readers stay where they are (`camel/runtime/core_facts.h`):
 * `TensorShape` is the kind's payload in both registries, and a second copy of it would be a second
 * truth about what a shape is. That header is a dependency of a moved kind, not of the framework --
 * a kind that returns this value is the reason for it, and the kinds still living in
 * `src/kernel/runtime/core_facts.cpp` are the reason it is still there.
 */

#include "camel/facts/core_kinds.h"

#include "camel/runtime/core_facts.h"
#include "camel/runtime/operator_instance.h"

#include <optional>
#include <span>

namespace camel::facts {

using camel::runtime::fact::NodeInstance;
using camel::runtime::fact::Reorder;
using camel::runtime::fact::TensorShape;

KindId shapeKind() {
    static const KindId id = registerKind<TensorShape>(Kind<TensorShape>{
        .key     = "core.shape",
        .owner   = "core",
        .version = 1,
        // Derived: the shape is what the node's type says, and the type is part of the graph, so a
        // stored answer is correct until the node it is anchored on changes.
        .cls = Class::Derived,
        // One node: a shape is asked of the value one node produces.
        .scope = Scope::Node,
        // Reported: the computation says which node it read, so a change elsewhere in the graph
        // does not invalidate the answer. The runtime kind it replaces said `Dependency::Node`,
        // which was the coarse version of the same claim.
        .dependence = Dependence::Reported,
        // Adding nodes or edges cannot change the shape a node's type states.
        .monotone = false,
        .compute  = [](std::span<const Anchor> anchors, const GraphAccess &graphs, ReadLog &log)
            -> std::optional<TensorShape> {
            if (anchors.size() != 1 || anchors[0].graph == nullptr ||
                anchors[0].node == kWholeGraph || !graphs.view) {
                return std::nullopt;
            }
            const graph::GraphView *view = graphs.view(*anchors[0].graph);
            if (view == nullptr) {
                return std::nullopt;
            }
            const auto ref = static_cast<graph::NodeRef>(anchors[0].node);
            if (!view->contains(ref)) {
                return std::nullopt;
            }
            // What it read: the node's own record -- its type, which is what the shape comes from.
            // A reader that reports nothing is stale as soon as anything the fact is anchored on
            // changes, which would be right but would recompute a shape whenever a neighbour moved.
            log.node(anchors[0].node);
            return camel::runtime::fact::shapeOfType(view->typeOf(ref));
        },
        // No merge, and the reason: a shape is about one value, and there is no shape of a union of
        // two values. A caller that wants the shapes of several nodes asks for each of them.
        // Stating it here is what makes this a cache of a per-node answer rather than a half-stated
        // analysis (design principles red line 4).
        .merge = {},
        // The text the kind prints; `camel inspect --facts` shows this line.
        .print = [](const TensorShape &shape) { return camel::runtime::fact::toString(shape); },
        // No anchorsIn override: the default for a Node kind is every node of the view, and every
        // node of a graph has a type, so every node is a place to ask.
        .anchorsIn = {},
        // No encode/decode yet: their forms live with the runtime kind for now, and a portable form
        // belongs to the step that persists facts, not to the move of the kind.
        .encode = {},
        .decode = {},
    });
    return id;
}

KindId permissionsKind() {
    static const KindId id = registerKind<int>(Kind<int>{
        .key     = "core.permissions",
        .owner   = "core",
        .version = 1,
        // Content, and the framework's own definition of the class says so in as many words: "what
        // cannot be recovered from the graph -- the numeric permissions a program granted: the
        // graph's content. A kind of this class reads it from the view each time; it is never
        // stored, so it is never stale." The runtime kind it replaces said
        // `FactClass::GraphContent`, which is the same class under the registry that came before
        // this one.
        .cls = Class::Content,
        // One node: the permissions are granted at a place, and `#strict` revokes them there.
        .scope = Scope::Node,
        // The field is inert for a Content kind -- nothing is stored, so nothing is ever found
        // stale -- and `Reported` is what the computation would claim if it were stored: it reads
        // the node's own tags and says so.
        .dependence = Dependence::Reported,
        // Adding nodes or edges cannot change what a node was granted.
        .monotone = false,
        .compute  = [](std::span<const Anchor> anchors, const GraphAccess &graphs, ReadLog &log)
            -> std::optional<int> {
            if (anchors.size() != 1 || anchors[0].graph == nullptr ||
                anchors[0].node == kWholeGraph || !graphs.view) {
                return std::nullopt;
            }
            const graph::GraphView *view = graphs.view(*anchors[0].graph);
            if (view == nullptr) {
                return std::nullopt;
            }
            const auto ref = static_cast<graph::NodeRef>(anchors[0].node);
            if (!view->contains(ref)) {
                return std::nullopt;
            }
            // The node's own tags are what it read; the mask itself is the view's answer and this
            // kind does not derive it a second time (`core::tag::permissionsOfTags` stays the one
            // place that reads a tag set, and every adapter's `permissionsOf` goes through it).
            log.node(anchors[0].node);
            const camel::core::sem::PermissionMask mask = view->permissionsOf(ref);
            // No mask is not the value 0: a node nobody granted anything at has no fact of this
            // kind, which is the difference between "nothing granted" and "granted nothing".
            return mask != 0 ? std::optional<int>(static_cast<int>(mask)) : std::nullopt;
        },
        // No merge, and the reason: permissions are granted per place, and there is no permission
        // of a union of two places -- a union is not a place a program can be in. The framework
        // requires the reason where a kind has no merge, and for a Content kind it is also why the
        // class cannot be an analysis: nothing to combine because nothing is stored.
        .merge = {},
        // The text the kind prints, from the one function that names a mask in text; a second
        // spelling here would be a second answer to "what is this mask".
        .print =
            [](const int &mask) {
                return camel::core::sem::permissionsText(
                    static_cast<camel::core::sem::PermissionMask>(mask));
            },
        // No anchorsIn override: a Node kind is asked at every node of the view, which is where a
        // program can grant a permission.
        .anchorsIn = {},
        // No encode/decode yet: the runtime kind keeps its own form (a decimal number) until the
        // step that persists facts, and two encoders now would be two texts for one value.
        .encode = {},
        .decode = {},
    });
    return id;
}

KindId instanceKind() {
    static const KindId id = registerKind<NodeInstance>(Kind<NodeInstance>{
        .key     = "core.instance",
        .owner   = "core",
        .version = 1,
        // Derived: the instance is what the operator declares, instantiated with the facts at the
        // node (the operand types and the constants among them), and both are the graph's content.
        .cls   = Class::Derived,
        .scope = Scope::Node,
        // Reported: it reads the node and the operator's record, and says so below.
        .dependence = Dependence::Reported,
        .monotone   = false,
        .compute    = [](std::span<const Anchor> anchors, const GraphAccess &graphs, ReadLog &log)
            -> std::optional<NodeInstance> {
            if (anchors.size() != 1 || anchors[0].graph == nullptr ||
                anchors[0].node == kWholeGraph || !graphs.view) {
                return std::nullopt;
            }
            const camel::graph::GraphView *view = graphs.view(*anchors[0].graph);
            if (view == nullptr) {
                return std::nullopt;
            }
            const auto ref = static_cast<camel::graph::NodeRef>(anchors[0].node);
            if (!view->contains(ref)) {
                return std::nullopt;
            }
            log.node(anchors[0].node);
            // The registration this kind replaces asked first whether the operator declares index
            // semantics at all, and answered a value either way: a node whose operator declares
            // them but whose instance cannot be worked out still has the fact, printed as `none`.
            // That is the difference between "no instance here" and "this node is not about
            // instances", and the move keeps it.
            if (view->kindOf(ref) != camel::graph::NodeKind::Oper) {
                return std::nullopt;
            }
            const core::OperId oper = view->operatorOf(ref);
            const camel::core::sem::OperatorSemantics *semantics =
                camel::core::OperatorTable::instance().semantics(oper);
            if (semantics == nullptr || semantics->indexing == nullptr) {
                return std::nullopt;
            }
            // The record is half the answer: it is what the operator declares, and what the
            // instance's text is written against.
            log.record(oper);
            NodeInstance value;
            const std::optional<camel::core::sem::IndexedInstance> instance =
                camel::runtime::instanceOf(*view, ref);
            if (instance) {
                value.instance = instance;
                value.text     = camel::core::sem::toString(*instance, semantics);
            }
            return value;
        },
        // No merge, and the reason: an instance is what one node's operator declares with the facts
        // at that node. Two nodes' instances are two answers, and an index instance has no union --
        // a place computed by two different indices is not a place with one index.
        .merge = {},
        // The text the runtime kind prints: the instance's canonical text (`sem::toString`), which
        // is what two instances are compared by.
        .print     = [](const NodeInstance &value) { return value.text; },
        .anchorsIn = {},
        .encode    = {},
        .decode    = {},
    });
    return id;
}

KindId reorderKind() {
    static const KindId id = registerKind<Reorder>(Kind<Reorder>{
        .key     = "core.reorder",
        .owner   = "core",
        .version = 1,
        // Derived: the freedom is what the operator's law and the program's permissions make of the
        // node, and both are the graph's content.
        .cls   = Class::Derived,
        .scope = Scope::Node,
        // Reported: it reads the node, its operator's record and its permissions, and says so.
        .dependence = Dependence::Reported,
        .monotone   = false,
        .compute    = [](std::span<const Anchor> anchors, const GraphAccess &graphs, ReadLog &log)
            -> std::optional<Reorder> {
            if (anchors.size() != 1 || anchors[0].graph == nullptr ||
                anchors[0].node == kWholeGraph || !graphs.view) {
                return std::nullopt;
            }
            const camel::graph::GraphView *view = graphs.view(*anchors[0].graph);
            if (view == nullptr) {
                return std::nullopt;
            }
            const auto ref = static_cast<camel::graph::NodeRef>(anchors[0].node);
            if (!view->contains(ref)) {
                return std::nullopt;
            }
            log.node(anchors[0].node);
            if (view->kindOf(ref) == camel::graph::NodeKind::Oper) {
                log.record(view->operatorOf(ref));
            }
            // The default policy asks for nothing the program did not write, which is what the
            // runtime kind asked for: its signature defaulted to `explicitOnly()`.
            const std::optional<camel::core::sem::ReorderFreedom> freedom =
                camel::runtime::reorderFreedomOf(
                    *view,
                    ref,
                    camel::core::sem::NumericPolicy::explicitOnly());
            if (!freedom) {
                return std::nullopt;
            }
            return Reorder{
                .regroup         = freedom->regroup,
                .permute         = freedom->permute,
                .blockIdentity   = freedom->blockIdentity,
                .contract        = freedom->contract,
                .changePrecision = freedom->changePrecision,
            };
        },
        // No merge, and the reason: what may be done with one reduction is about that reduction.
        // Two nodes that both reduce do not give a freedom of their union -- a union of two loops
        // is not a loop whose reduction anyone can relax.
        .merge = {},
        // The text the runtime kind prints, from the one function that names this value.
        .print     = [](const Reorder &reorder) { return camel::runtime::fact::toString(reorder); },
        .anchorsIn = {},
        .encode    = {},
        .decode    = {},
    });
    return id;
}
} // namespace camel::facts