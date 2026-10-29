// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The pass of loop_inputs.h: prints the inputs of every LOOP once, then hands the program on.
 */

#include "camel/passes/test/loop_inputs.h"

#include "camel/core/type.h"
#include "camel/runtime/loop.h"

#include <format>
#include <unordered_set>

namespace {

using namespace camel::runtime;

const char *kindName(GCNodeKind kind) {
    switch (kind) {
    case GCNodeKind::Data:
        return "DATA";
    case GCNodeKind::Port:
        return "PORT";
    case GCNodeKind::Cast:
        return "CAST";
    case GCNodeKind::Copy:
        return "COPY";
    case GCNodeKind::Fill:
        return "FILL";
    case GCNodeKind::Accs:
        return "ACCS";
    case GCNodeKind::Brch:
        return "BRCH";
    case GCNodeKind::Join:
        return "JOIN";
    case GCNodeKind::Call:
        return "CALL";
    case GCNodeKind::Bind:
        return "BIND";
    case GCNodeKind::Func:
        return "FUNC";
    case GCNodeKind::Oper:
        return "OPER";
    case GCNodeKind::Gate:
        return "GATE";
    case GCNodeKind::Dref:
        return "DREF";
    case GCNodeKind::Loop:
        return "LOOP";
    case GCNodeKind::Dead:
        return "DEAD";
    }
    return "?";
}

/// One input of a loop's regions. `constant` is the role the caller determined, not a re-check of
/// the rule: the pass must test externalInputs, not a copy of its predicate.
void printInput(
    std::ostream &os, const char *role, bool constant, const GCGraph &graph, gc_node_ref_t ref) {
    const GCNode *node = graph.node(ref);
    os << std::format(
        "  {} {} constant={} type={}\n",
        role,
        kindName(node->kind),
        constant ? 1 : 0,
        node->dataType() != nullptr ? node->dataType()->toString() : std::string("none"));
}

} // namespace

const camel::runtime::Program *
LoopInputsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    for (GCGraph *graph : program->graphs()) {
        if (!graph->hasNodePayload()) {
            continue;
        }
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            if (!*it || (*it)->kind != GCNodeKind::Loop) {
                continue;
            }
            const gc_node_ref_t loop = it.ref();
            const LoopView view(*program, *graph, loop);
            const std::vector<gc_node_ref_t> captures = view.externalInputs(false);
            const std::vector<gc_node_ref_t> withConstants = view.externalInputs(true);
            os << std::format(
                "LOOP_INPUTS graph={} loop={} kind={} state={} with={} captures={} constants={}\n",
                graph->name(),
                loop,
                camel::core::loopKindName(view.kind()),
                view.stateCount(),
                graph->withInputsOf(loop).size(),
                captures.size(),
                withConstants.size() - captures.size());
            for (const gc_node_ref_t ref : captures) {
                printInput(os, "input", false, *graph, ref);
            }
            // A node the two lists disagree on is a constant: the default list left it out, the
            // list that asks for constants has it.
            std::unordered_set<gc_node_ref_t> captured(captures.begin(), captures.end());
            for (const gc_node_ref_t ref : withConstants) {
                if (!captured.contains(ref)) {
                    printInput(os, "constant", true, *graph, ref);
                }
            }
        }
    }
    return program;
}