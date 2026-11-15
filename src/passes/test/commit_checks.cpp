// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The pass of commit_checks.h writes one thing a C++ pass can leave behind and the first commit
 * check of the effect analysis refuses (docs/design/effects/analysis.cn.md 4.1); what it prints is
 * the reading the plan compares -- which function gave which its type -- so a reader can tell "the
 * pass ran and the graph it handed back is the one that was refused" from "the pass did nothing".
 */

#include "camel/passes/test/commit_checks.h"

#include "../opt/generic/rewrite.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_table.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/resources.h"

#include <utility>
#include <vector>

using namespace camel::runtime;

namespace {

/// Whether running `graph` writes a resource outside the program -- a print, a file, the clock. The
/// footprint of the graph is the analysis's own answer to that question; this pass does not decide
/// it a second time.
bool writesOutside(const Program &program, const GCGraph *graph) {
    if (graph == nullptr) {
        return false;
    }
    for (const camel::core::sem::ResourceAccess &access : footprintOf(program, graph).accesses) {
        if (access.resource.cls == camel::core::sem::ResourceClass::Ambient &&
            camel::core::sem::writes(access.mode)) {
            return true;
        }
    }
    return false;
}

/// Whether two graphs publish function types of the same shape: the same `with` and ordinary
/// parameter types and the same exit type. What differs between them, when the two pass this, is
/// the resource clause -- which is exactly what the pass below moves from one to the other.
bool sameShape(const GCGraph &a, const GCGraph &b) {
    const auto *left  = a.funcType();
    const auto *right = b.funcType();
    if (left == nullptr || right == nullptr) {
        return false;
    }
    if (left->withTypesCount() != right->withTypesCount() ||
        left->normTypesCount() != right->normTypesCount()) {
        return false;
    }
    if (!typesCompatible(left->exitType(), right->exitType())) {
        return false;
    }
    for (size_t i = 0; i < left->withTypesCount(); ++i) {
        if (!typesCompatible(left->withTypeAt(i), right->withTypeAt(i))) {
            return false;
        }
    }
    for (size_t i = 0; i < left->normTypesCount(); ++i) {
        if (!typesCompatible(left->normTypeAt(i), right->normTypeAt(i))) {
            return false;
        }
    }
    return true;
}

} // namespace

const camel::runtime::Program *
FoldFunctionTypeTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    using camel::runtime::gc_node_ref_t;
    using camel::runtime::GCNodeKind;
    // The calls of the program, split by whether the body they run writes to the outside. A quiet
    // call and a noisy one whose functions have the same shape are what this pass is after.
    struct Call {
        const GCGraph *owner  = nullptr;
        gc_node_ref_t node    = kInvalidNodeRef;
        const GCGraph *callee = nullptr;
    };
    std::vector<Call> quiet;
    std::vector<Call> noisy;
    for (const GCGraph *graph : program->graphs()) {
        if (graph == nullptr) {
            continue;
        }
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            const GCNode *node = graph->node(ref);
            if (node == nullptr || node->kind != GCNodeKind::Func) {
                continue;
            }
            const GCGraph *callee = program->calleeOf(*graph, ref);
            if (callee == nullptr || callee == graph) {
                continue;
            }
            (writesOutside(*program, callee) ? noisy : quiet).push_back({graph, ref, callee});
        }
    }
    for (const Call &loud : noisy) {
        for (const Call &soft : quiet) {
            if (loud.callee == soft.callee || !sameShape(*loud.callee, *soft.callee)) {
                continue;
            }
            camel::runtime::RuntimeGraphDraftSession session(context_, program);
            auto &transaction = session.edit(const_cast<GCGraph *>(loud.callee));
            transaction.edit().setFuncType(soft.callee->funcType());
            out << "fold_function_type: graph '" << loud.callee->name() << "' now publishes '"
                << soft.callee->funcType()->toString() << "', the type of graph '"
                << soft.callee->name() << "'; its body is unchanged\n";
            return session.commit();
        }
    }
    out << "fold_function_type: no two functions of one shape to fold\n";
    return program;
}
