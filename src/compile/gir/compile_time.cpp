// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The table of value-level transforms the front end answers while it builds (compile_time.h).
 *
 * The table is process-wide and a module fills it when it loads, which is before any program is
 * built: a program never registers a transform, and a module's entry is its own.
 */

#include "camel/compile/gir/compile_time.h"

#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/graph.h"

#include <map>
#include <vector>

namespace camel::compile::gir {

namespace {

std::map<std::string, CompileTimeTransform, std::less<>> &transforms() {
    static std::map<std::string, CompileTimeTransform, std::less<>> table;
    return table;
}

size_t instantiated = 0;

} // namespace

void registerCompileTimeTransform(std::string uri, CompileTimeTransform transform) {
    GUARD(!uri.empty(), "A compile-time transform needs the operator it answers.");
    GUARD(transform != nullptr, "A compile-time transform needs something to run.");
    auto &table = transforms();
    GUARD(
        table.find(uri) == table.end(),
        "A compile-time transform is already registered for this operator.");
    table.emplace(std::move(uri), std::move(transform));
}

const CompileTimeTransform *compileTimeTransformOf(std::string_view uri) {
    const auto &table = transforms();
    const auto found  = table.find(uri);
    return found == table.end() ? nullptr : &found->second;
}

void resetCompileTimeInstantiations() { instantiated = 0; }

size_t compileTimeInstantiationCount() { return instantiated; }

void replaceCallWithValue(graph_ptr_t caller, draft_node_ref_t node, draft_node_ref_t value) {
    camel::runtime::GraphDraft &draft = caller->edit();
    const auto copy                   = [](std::span<const camel::runtime::gc_node_ref_t> refs) {
        return std::vector<camel::runtime::gc_node_ref_t>(refs.begin(), refs.end());
    };
    // The call's own place in the chain is read before anything moves: the predecessors it waits on
    // are what the value will wait on, so the statements written before it stay ordered before it.
    const std::vector<camel::runtime::gc_node_ref_t> orderInputs = copy(draft.orderInputsOf(node));
    std::vector<camel::runtime::gc_node_ref_t> syncInputs          = copy(draft.syncInputsOf(node));
    if (syncInputs.empty()) {
        // The builder joins a statement to the one written before it at the node it created *first*
        // for it, which is where the statement's expression begins -- and the value of a call like
        // `split(jvp(f))` is the *last* node of the statement. So a call can end a chain segment
        // without being its successor's predecessor: what is ordered before it is ordered before its
        // own arguments. The value continues the chain after them, or the statements between the two
        // would end up joined to nothing -- and a statement joined to nothing does not run (the
        // effect rule keeps an effect by a chain, a data use, or a later read).
        const std::vector<camel::runtime::gc_node_ref_t> normInputs = copy(draft.normInputsOf(node));
        const std::vector<camel::runtime::gc_node_ref_t> withInputs = copy(draft.withInputsOf(node));
        for (const auto *inputs : {&normInputs, &withInputs}) {
            for (camel::runtime::gc_node_ref_t input : *inputs) {
                if (draft.alive(input) && !draft.syncInputsOf(input).empty()) {
                    syncInputs.push_back(input);
                    break;
                }
            }
            if (!syncInputs.empty()) {
                break;
            }
        }
    }
    const auto *header                           = draft.header(node);
    const auto wasStatement =
        static_cast<uint8_t>(header != nullptr ? header->runtimeFlags : 0) &
        static_cast<uint8_t>(camel::runtime::kGCNodeFlagSyncStatement);
    draft.setNodeRuntimeFlags(
        value,
        static_cast<uint8_t>(
            camel::runtime::kGCNodeFlagConstant |
            (wasStatement != 0 ? camel::runtime::kGCNodeFlagSyncStatement : 0)));
    if (!orderInputs.empty()) {
        draft.setOrderInputs(value, orderInputs);
    }
    if (!syncInputs.empty()) {
        draft.setSyncInputs(value, syncInputs);
        // The control edge names the same pair beside the sync edge (docs/design/effects/edges.cn.md
        // 1.5), and a consumer that still reads that kind has to find it here too.
        for (camel::runtime::gc_node_ref_t predecessor : syncInputs) {
            draft.appendInput(camel::runtime::DraftEdgeKind::Ctrl, value, predecessor);
        }
    }
    // Both directions of every kind: whoever read the call reads the value, and whoever was ordered
    // by the call is ordered by the value. `replaceAllCtrlUses` moves the control edges the sync
    // edges are written beside, so the chain's successors follow the value either way.
    draft.replaceAllValueUses(node, value);
    draft.replaceAllCtrlUses(node, value);
    draft.replaceAllOrderUses(node, value);
    draft.replaceAllSyncUses(node, value);
    draft.eraseNode(node);
}

} // namespace camel::compile::gir