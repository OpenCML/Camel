// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The passes of commit_checks.h write one thing a C++ pass can leave behind and the first commit
 * check of the effect analysis refuses (docs/design/effects/analysis.cn.md 4.1); what they print is
 * the reading a plan compares -- which function gave which its type, and whether the pass under
 * test refused the program -- so a reader can tell "the pass ran and the graph it handed back is the
 * one that was refused" from "the pass did nothing".
 *
 * The second one exists because the check is not asked of every pass in every build: a pass written
 * in Camel is exempt by the theorem of 2026-11-15, and the ruling of 2026-11-16 has the builds that
 * can afford it read the theorem anyway. A reading of that decision needs a program the check
 * refuses and a pass the exemption applies to, which is what the two halves of this file are.
 */

#include "camel/passes/test/commit_checks.h"

#include "../opt/generic/rewrite.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_table.h"
#include "camel/execute/pass/base.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/resources.h"

#include <utility>
#include <vector>

using namespace camel::runtime;

namespace {

/// The Camel pass the exemption is read on: `opt::cse`, the library's common-subexpression sweep
/// (`stdlib/opt.cml`), whose only rewriting interface is the graph module's. Its native twin is
/// `std::opt::cse`, which is a C++ pass and is checked in every build.
constexpr const char *kCamelPassUnderTest = "opt::cse";

/// Whether running `graph` writes a resource outside the program -- a print, a file, the clock. The
/// footprint of the graph is the analysis's own answer to that question; these passes do not decide
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
/// the resource clause -- which is exactly what the fold below moves from one to the other.
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

/// The pair the fold is about: a graph whose body writes to the outside, and a graph of the same
/// shape whose body does not. The calls of the program decide which graphs those are, so a graph
/// nobody calls is not part of a pair.
struct SameShapePair {
    const GCGraph *noisy = nullptr;
    const GCGraph *quiet = nullptr;
};

SameShapePair findSameShapePair(const Program &program) {
    // The calls of the program, split by whether the body they run writes to the outside. A quiet
    // call and a noisy one whose functions have the same shape are what this pass is after.
    struct Call {
        const GCGraph *owner  = nullptr;
        gc_node_ref_t node    = kInvalidNodeRef;
        const GCGraph *callee = nullptr;
    };
    std::vector<Call> quiet;
    std::vector<Call> noisy;
    for (const GCGraph *graph : program.graphs()) {
        if (graph == nullptr) {
            continue;
        }
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            const GCNode *node = graph->node(ref);
            if (node == nullptr || node->kind != GCNodeKind::Func) {
                continue;
            }
            const GCGraph *callee = program.calleeOf(*graph, ref);
            if (callee == nullptr || callee == graph) {
                continue;
            }
            (writesOutside(program, callee) ? noisy : quiet).push_back({graph, ref, callee});
        }
    }
    for (const Call &loud : noisy) {
        for (const Call &soft : quiet) {
            if (loud.callee == soft.callee || !sameShape(*loud.callee, *soft.callee)) {
                continue;
            }
            return SameShapePair{loud.callee, soft.callee};
        }
    }
    return {};
}

/// The fold, in one commit: the noisy graph publishes the type of the quiet one and keeps its body,
/// which is the program the containment check is about. Returns the program the commit made.
///
/// The session is opened over the whole program and only the noisy graph is edited: naming a graph
/// as the session's root would make that graph the program's entry when the commit replaces it, and
/// the fixture would be a program whose entry takes a parameter.
const Program *foldTypeOf(
    const camel::core::context::context_ptr_t &context, const Program *program,
    const SameShapePair &pair, std::ostream &out) {
    RuntimeGraphDraftSession session(context, program);
    auto &transaction = session.edit(const_cast<GCGraph *>(pair.noisy));
    transaction.edit().setFuncType(pair.quiet->funcType());
    out << "fold_function_type: graph '" << pair.noisy->name() << "' now publishes '"
        << pair.quiet->funcType()->toString() << "', the type of graph '" << pair.quiet->name()
        << "'; its body is unchanged\n";
    return session.commit();
}

} // namespace

const camel::runtime::Program *
FoldFunctionTypeTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr) {
        return program;
    }
    const SameShapePair pair = findSameShapePair(*program);
    if (pair.noisy == nullptr) {
        out << "fold_function_type: no two functions of one shape to fold\n";
        return program;
    }
    return foldTypeOf(context_, program, pair, out);
}

const camel::runtime::Program *
CamelCommitChecksTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr) {
        return program;
    }
    const SameShapePair pair = findSameShapePair(*program);
    if (pair.noisy == nullptr) {
        out << "CAMEL_CHECKS no_pair\n";
        return program;
    }
    const Program *uncontained = foldTypeOf(context_, program, pair, out);
    // What the build itself answers, printed before any run: the answer has to be on the record
    // before the run that reads it.
    const bool asked = camelPassCommitChecksEnabled();
    out << "CAMEL_CHECKS build asks=" << (asked ? 1 : 0) << '\n' << std::flush;
    // One run of a pass written in Camel over that program, with the commit checks asked of such a
    // pass exactly as this build asks them.
    const PassRunResult got =
        applyRegisteredPass(uncontained, kCamelPassUnderTest, context_, out, this);
    if (got.program == nullptr) {
        // Asked, and refused: the diagnostic is the reading, and the run stops there.
        return program;
    }
    out << "CAMEL_CHECKS accepted, asked=" << (asked ? 1 : 0) << '\n' << std::flush;
    if (asked) {
        // This build asked and the check did not fire. Say so with the line the plans reject
        // instead of ending quietly: a check that has stopped refusing has to be red somewhere.
        out << "CAMEL_CHECKS asked=1 -> accepted\n";
        return program;
    }
    // Not asked, and the run went on past an accepted program: that is what the shipped build's
    // answer buys, and the line is here so that "it accepted" cannot be read as "it was checked".
    out << "CAMEL_CHECKS released: the run continued past " << kCamelPassUnderTest
        << " with the checks off\n";
    setCamelPassCommitChecksEnabled(true);
    // The expectation is announced before the run and not confirmed after it: a refusal records
    // its diagnostic and comes back with no program, so the line below is printed only when the
    // second run did produce one. A check that has stopped refusing leaves it as the only trace,
    // and it is red on purpose.
    out << "CAMEL_CHECKS asked=1 -> the same program, the same pass\n" << std::flush;
    const PassRunResult again =
        applyRegisteredPass(got.program, kCamelPassUnderTest, context_, out, this);
    if (again.program != nullptr) {
        out << "CAMEL_CHECKS asked=1 -> accepted\n";
    }
    return program;
}