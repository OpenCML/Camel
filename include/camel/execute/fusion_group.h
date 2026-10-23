// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Fusion groups a back end may run as one kernel (design principles 9.6, 7.3, 7.4).
 *
 * A group is an attribute of the nodes, not an operator: the graph keeps the program's operators
 * (matmul, add, relu, divide, multiply, ...), and the attribute (camel/core/node_attrs.h) says
 * that these nodes are one group a back end may run in one pass over their buffers. The core
 * knows no key: a module registers, per attribute key and value name, the kernel that recognizes
 * the group and runs it. A back end that finds no registered kernel for a node runs the operator
 * as the program says -- the fallback -- so the attribute never makes a node unexecutable.
 *
 * The shape of a group is the graph's. A back end asks at each node that carries attributes;
 * the module's kernel establishes the members from the node and its users and tells the back end
 * which members it will compute later, and which node runs the group. A group runs at its last
 * node (its tail), where every operand the members read has a value; the members before it are
 * only deferred when nothing outside the group observes them (`FusionGroupView::isPrivateTo`), so
 * no value is missing.
 *
 * A deferred member's operator does not run, so a tool must not report it as one: the node VM
 * never asks the debugger to stop at it (the group's tail is where the execution is), and the
 * group run is recorded in the profile as a group, its members counted once (design principles
 * 7.3: a measurement must not change what it measures, and must not show work that did not
 * happen). `FusionGroupRef::tail` and the `members` of a `Ran` result are what those reports
 * read.
 *
 * Numeric contract: a group kernel runs the definition graphs of its members. It may only
 * produce the bits those operators produce under the permissions in force (7.3): for a `#strict`
 * node, or under the safe mode, exactly the canonical bits, or the kernel must decline the group
 * and let the nodes run one by one (7.4). The attribute itself grants nothing.
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/node_attrs.h"
#include "camel/core/operator_table.h"
#include "camel/runtime/exec_plan.h"
#include "camel/runtime/graph.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace camel::execute {

/// What a back end should do with the node it asked about.
enum class FusionGroupAction : uint8_t {
    /// No registered kernel owns this node: run its operator as the program says.
    None,
    /// The node is a member the group computes at its tail: do not run its operator, and do not
    /// run the members the kernel named either (`Deferred` reports them).
    Deferred,
    /// The group ran here and wrote this node's value; do not run the node's operator.
    Ran,
};

/// A group as one of its nodes sees it: the graph, the frame the node writes, the plan that names
/// the frame slots, and the attribute (key, value) that all members carry.
class FusionGroupView {
  public:
    FusionGroupView(
        const camel::runtime::GCGraph &graph, camel::core::context::Frame &frame,
        const camel::runtime::ExecPlan &plan, camel::core::attr::AttrKey key,
        camel::core::attr::AttrValueId value)
        : graph_(&graph), frame_(&frame), plan_(&plan), key_(key), value_(value) {}

    const camel::runtime::GCGraph &graph() const { return *graph_; }
    camel::core::context::Frame &frame() const { return *frame_; }
    camel::core::attr::AttrKey key() const { return key_; }
    camel::core::attr::AttrValueId value() const { return value_; }

    /// Whether `node` carries this group's attribute (the key with this value).
    bool memberOf(camel::runtime::gc_node_ref_t node) const {
        return camel::core::attr::valueOf(graph_->nodeAttrs(node), key_) == value_;
    }
    /// The node's value: the frame slot it writes (NullSlot when it has none).
    slot_t valueOf(camel::runtime::gc_node_ref_t node) const;
    /// Publishes the node's value, as the back end publishes an operator's return value.
    void setValueOf(camel::runtime::gc_node_ref_t node, slot_t value) const;
    /// Static type of the node's value.
    camel::core::type::Type *typeOf(camel::runtime::gc_node_ref_t node) const {
        const auto *n = graph_->node(node);
        return n != nullptr ? n->dataType() : nullptr;
    }
    /// The normal inputs of `node`, in order.
    std::span<const camel::runtime::gc_node_ref_t>
    inputsOf(camel::runtime::gc_node_ref_t node) const {
        return graph_->normInputsOf(node);
    }
    /// The nodes that read `node`'s value, in no particular order.
    std::span<const camel::runtime::gc_node_ref_t>
    usersOf(camel::runtime::gc_node_ref_t node) const {
        return graph_->normOutputsOf(node);
    }
    /// The operator of an OPER node; kNoOper for any other node.
    camel::core::OperId operOf(camel::runtime::gc_node_ref_t node) const;
    /// True when only nodes of `members` read `node`'s value, and it is not the graph's output,
    /// return or exit value: a value the group may compute at its tail without storing it before.
    bool isPrivateTo(
        camel::runtime::gc_node_ref_t node,
        std::span<const camel::runtime::gc_node_ref_t> members) const;

  private:
    const camel::runtime::GCGraph *graph_;
    camel::core::context::Frame *frame_;
    const camel::runtime::ExecPlan *plan_;
    camel::core::attr::AttrKey key_;
    camel::core::attr::AttrValueId value_;
};

/// A group as a back end reports it after asking the registry: the attribute that names it and
/// the node it runs at. `name` is the attribute's value (the name the module registered the
/// kernel under); an empty `name` means no kernel owns the node.
struct FusionGroupRef {
    camel::core::attr::AttrKey key = camel::core::attr::kNoKey;
    std::string_view name;
    /// The node the group runs at (its tail): the last member, where every operand has a value.
    camel::runtime::gc_node_ref_t tail = camel::runtime::kInvalidNodeRef;
};

/// A module's group kernel. It decides what `node` is and, at the group's tail, runs it: returns
/// `Deferred` and fills `members` with the nodes the back end must not execute from now on (the
/// ones the group's tail computes) and `group.tail` with that tail; `Ran` when it computed
/// `node`'s value here (filling `members` with the nodes it computed, the tail excluded, and
/// `group.tail` with `node`); or `None` when this is not a group it can run (the back end then
/// runs `node`'s operator).
using FusionGroupKernel = FusionGroupAction (*)(
    const FusionGroupView &view, camel::runtime::gc_node_ref_t node,
    std::span<const camel::runtime::gc_node_ref_t> deferred,
    std::vector<camel::runtime::gc_node_ref_t> &members, FusionGroupRef &group);

/// Registers the kernel of the group `name` (the attribute's Symbol value) under `key`, when the
/// module loads. A name registered again replaces its kernel.
void registerFusionGroup(
    camel::core::attr::AttrKey key, std::string_view name, FusionGroupKernel kernel);

/// Asks the registered kernels about `node`, a node that carries attributes. `deferred` are the
/// nodes this back end already deferred in the current execution of `graph`. Returns `None` when
/// no kernel owns the node; the back end then runs its operator. On a group, `group` names it and
/// its tail, and `members` is filled as the kernel reports it.
///
/// `Ran` is only returned at the tail a previous `Deferred` named: a group runs when its earlier
/// members were deferred to it, so a back end that must report the group before it runs (to a
/// debugger) knows the tail first and calls this again there.
FusionGroupAction runFusionGroup(
    const camel::runtime::GCGraph &graph, camel::core::context::Frame &frame,
    const camel::runtime::ExecPlan &plan, camel::runtime::gc_node_ref_t node,
    std::span<const camel::runtime::gc_node_ref_t> deferred,
    std::vector<camel::runtime::gc_node_ref_t> &members, FusionGroupRef &group);

} // namespace camel::execute