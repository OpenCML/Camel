// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The pass of loop_inputs.h: prints the inputs of every LOOP once, then hands the program on.
 */

#include "camel/passes/test/loop_inputs.h"

#include "camel/core/type.h"
#include "camel/execute/pass/registration.h"
#include "camel/runtime/loop.h"
#include "camel/runtime/oper_shape.h"

#include <format>
#include <string_view>
#include <unordered_set>

namespace {

using namespace camel::runtime;

const char *kindName(GCNodeKind kind) {
    // The kinds are named once (`camel/runtime/oper_shape.h#nodeKindName`); "?" is this dump's own word
    // for a value that table does not name. Every name it answers points at a string literal, so
    // `data()` is the C string the callers print.
    const std::string_view name = camel::runtime::nodeKindName(kind);
    return name.empty() ? "?" : name.data();
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
    for (auto *graph : program->graphs()) {
        if (!graph->hasNodePayload()) {
            continue;
        }
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            if (!*it || (*it)->kind != GCNodeKind::Loop) {
                continue;
            }
            const gc_node_ref_t loop = it.ref();
            const LoopView view(*graph, loop);
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

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The test-only passes this file implements. Each name is the constant its own header
/// declares, so the name a pipeline writes and the class that answers it are one fact.
const PassKernel loopInputsKernel(kLoopInputsTestPass, makePass<LoopInputsTestPass>());

} // namespace
