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
#include "camel/core/rtdata/base.h"
#include "camel/execute/pass/base.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/resources.h"

#include <span>
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
///
/// This commit adds no node, so it adds no access, so the commit is not asked the effect or the order
/// questions (the tier of 2026-11-18, `commitAddsAccess`): a type replacement is what a draft does
/// without adding an access, and the program it publishes is the one the boundary check below reads.
/// The fixture could not hand that program on under the ruling of 2026-11-16 -- the commit refused it
/// there -- which is why the tier changed rather than the fixture.
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

/// The console a case prints to. `println` in the prelude is `#op(':io/println')`
/// (stdlib/prelude.cml), so a node with this uri is the program's own write of `stdout` and a node
/// this file adds with the same uri writes it too: one resource, two writers, which is what the
/// order check is about.
constexpr const char *kPrintUri = ":io/println";

/// Where the program prints, and the graph the question belongs in: the check is per graph, so the
/// splice goes where the write it conflicts with is. The search is the program's and not the root's
/// -- the case prints in a function it is entered through, and that function is not necessarily the
/// root -- and it reads the frozen graph, because a draft is only what a graph is about to become.
struct ConsoleWrite {
    const GCGraph *graph = nullptr;
    gc_node_ref_t node   = kInvalidNodeRef;
};

ConsoleWrite findPrint(const Program &program) {
    for (const GCGraph *graph : program.graphs()) {
        if (graph == nullptr) {
            continue;
        }
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            const GCNode *node = graph->node(ref);
            if (node == nullptr || node->kind != GCNodeKind::Oper) {
                continue;
            }
            const auto *body = graph->nodeBodyAs<GCOperBody>(ref);
            if (body != nullptr && std::string_view(body->uri()) == kPrintUri) {
                return ConsoleWrite{graph, ref};
            }
        }
    }
    return {};
}

/// Splices one print into the graph, standing on the effect it is placed after. A sync edge is what
/// orders two effects and what the credential check reads (`effectStandsOnSomething`,
/// src/execute/pass.cpp): an order edge orders two nodes and makes neither of them run, so an effect
/// whose only company is an order edge stands on nothing and the boundary refuses it -- which is a
/// different question from the one this file is about. `predecessor` is the write it waits for;
/// `kInvalidNodeRef` leaves it waiting for nothing, which is the shape the order check refuses.
gc_node_ref_t splicePrint(GraphTransaction &transaction, gc_node_ref_t predecessor) {
    auto &draft               = transaction.edit();
    const gc_node_ref_t value = draft.materializeStaticValue(
        camel::core::rtdata::toSlot<float>(7.0f), camel::core::type::Type::Float32());
    const gc_node_ref_t node =
        draft.addOperNode(camel::core::type::Type::Float32(), nullptr, kPrintUri);
    draft.setNormInputs(node, std::span<const gc_node_ref_t>(&value, 1));
    if (predecessor != kInvalidNodeRef) {
        const gc_node_ref_t waits = predecessor;
        draft.setSyncInputs(node, std::span<const gc_node_ref_t>(&waits, 1));
    }
    return node;
}

/// The two prints `spliceUnorderedPrintPair` wrote, in chain order.
struct SplicedPair {
    gc_node_ref_t first  = kInvalidNodeRef;
    gc_node_ref_t second = kInvalidNodeRef;
};

/// Splices two prints, the second after the first, and answers both. The pair stands on its own sync
/// edge, so the credential check is satisfied and the boundary has nothing to say; and it stands on
/// nothing the program already had, so the order check has a pair with no path between them. Two and
/// not one because a single print waiting for nothing would stand on nothing at all.
SplicedPair spliceUnorderedPrintPair(GraphTransaction &transaction) {
    SplicedPair pair;
    pair.first  = splicePrint(transaction, kInvalidNodeRef);
    pair.second = splicePrint(transaction, pair.first);
    return pair;
}

/// Says what the draft added, on the record, before the commit is asked: the tier of the checks is
/// what the draft added and not what the build is (the ruling of 2026-11-18, `commitAddsAccess` in
/// camel/runtime/resources.h), so a run that was not asked cannot look like one that was. The line
/// is a claim the plan can compare, and the one field it does not read is the build's cell.
void sayWhatTheDraftAdded(std::ostream &out, const char *who, std::string_view added) {
    out << who << ": the draft " << added << '\n'
        << who << ": it adds an access, so the commit asks the effect and order checks in every "
                "build\n"
        << std::flush;
}

/// Whether a graph's own type promises nothing at all: no ambient resource, no parameter, no
/// capture, no `anyOf` and no effect outside the program. That is the type a splice into the body
/// has to reach past, and the type of the quiet half of the fixture. `pure()` is the analysis's own
/// answer to it, so this does not decide it a second time.
bool declaresNothing(const GCGraph &graph) {
    const auto *type = graph.funcType();
    return type != nullptr && type->resources().pure();
}

/// One arm of one branch of one graph: the region, the node that owns it, and a node to point the
/// reading at.
struct BranchArm {
    const GCGraph *graph = nullptr;
    uint32_t region      = 0;
    gc_node_ref_t owner  = kInvalidNodeRef;
    gc_node_ref_t member = kInvalidNodeRef; // a node already in the arm
};

/// The first arm of the first branch of the program. A rewrite writes what it writes in a region it
/// can name, and the region tree is the graph's: this reads the frozen graph, because a draft is
/// only what a graph is about to become.
BranchArm findBranchArm(const Program &program) {
    for (const GCGraph *graph : program.graphs()) {
        if (graph == nullptr) {
            continue;
        }
        for (uint32_t region = 1; region < graph->regionCount(); ++region) {
            const GCRegion *record = graph->region(region);
            if (record == nullptr || record->kind != GCRegionKind::Arm ||
                record->owner == kInvalidNodeRef) {
                continue;
            }
            for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
                if (graph->node(ref) != nullptr && graph->nodeRegion(ref) == region) {
                    return BranchArm{graph, region, record->owner, ref};
                }
            }
            return BranchArm{graph, region, record->owner, kInvalidNodeRef};
        }
    }
    return {};
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

const camel::runtime::Program *
CommitOrderTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr) {
        return program;
    }
    sayWhatTheDraftAdded(out, "commit_order", "splices two writes to stdout and orders neither against the write that was already there");
    const ConsoleWrite print = findPrint(*program);
    if (print.graph == nullptr) {
        out << "commit_order: nothing in the program prints, so nothing to conflict with\n";
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    auto &transaction = session.edit(const_cast<GCGraph *>(print.graph));
    const SplicedPair spliced = spliceUnorderedPrintPair(transaction);
    out << "commit_order: graph '" << print.graph->name() << "' already writes stdout at node "
        << print.node << " and now writes it again at " << spliced.first << " and "
        << spliced.second << ", with no edge ordering either before the other\n"
        << std::flush;
    try {
        const Program *committed = session.commit();
        out << "commit_order: the commit published it, which is the defect\n";
        return committed;
    } catch (const GraphCommitRefused &refused) {
        out << "commit_order: refused, check=" << commitCheckName(refused.violation().check)
            << '\n'
            << std::flush;
        throw;
    }
}

const camel::runtime::Program *
CommitEffectTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr) {
        return program;
    }
    sayWhatTheDraftAdded(
        out, "commit_effect", "splices a write to stdout into a graph whose type declares nothing");
    // The check the containment half is about, written where the ruling of 2026-11-18 asks it: the
    // draft adds an access, so the commit is asked in every build, and what it is judged against is
    // the type the graph publishes. This is the same defect `test::fold_function_type` leaves behind
    // (a printing body under a type that declares nothing), reached by the door a rewrite actually
    // uses: a spliced node, not a replaced type.
    const GCGraph *quiet = nullptr;
    for (const GCGraph *graph : program->graphs()) {
        if (graph != nullptr && graph->nodeCount() > 1 && declaresNothing(*graph)) {
            quiet = graph;
            break;
        }
    }
    if (quiet == nullptr) {
        out << "commit_effect: no graph of the program declares nothing, so there is nothing to "
               "reach past\n";
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    auto &transaction = session.edit(const_cast<GCGraph *>(quiet));
    const gc_node_ref_t spliced = splicePrint(transaction, kInvalidNodeRef);
    out << "commit_effect: graph '" << quiet->name()
        << "' declares nothing and its body now writes stdout at node " << spliced << '\n'
        << std::flush;
    try {
        const Program *committed = session.commit();
        out << "commit_effect: the commit published it, which is the defect\n";
        return committed;
    } catch (const GraphCommitRefused &refused) {
        out << "commit_effect: refused, check=" << commitCheckName(refused.violation().check) << '\n'
            << std::flush;
        throw;
    }
}

const camel::runtime::Program *
CommitLegalSpliceTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr) {
        return program;
    }
    sayWhatTheDraftAdded(
        out,
        "commit_legal_splice",
        "splices a write to stdout ordered after the write that was already there, in a graph whose "
        "type declares it");
    const ConsoleWrite print = findPrint(*program);
    if (print.graph == nullptr) {
        out << "commit_legal_splice: nothing in the program prints, so there is nothing to order "
               "after\n";
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    auto &transaction = session.edit(const_cast<GCGraph *>(print.graph));
    // The same splice as `test::commit_order`, ordered after the write that was already there: the
    // graph publishes a type that declares the write, and the two writes are ordered, so there is
    // nothing for either check to refuse. This is the half that makes the refusals readings.
    const gc_node_ref_t spliced = splicePrint(transaction, print.node);
    out << "commit_legal_splice: graph '" << print.graph->name() << "' writes stdout at node "
        << print.node << " and again at " << spliced << ", ordered after it\n"
        << std::flush;
    const Program *committed = session.commit();
    out << "commit_legal_splice: the commit published it, and nothing was refused\n" << std::flush;
    return committed;
}

const camel::runtime::Program *
CommitExitRegionTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr) {
        return program;
    }
    out << "commit_exit_region: the seal asks it (`validateRuntimeGraphPayloadImpl`, "
           "src/runtime/graph.cpp), whatever the draft added\n"
        << std::flush;
    const BranchArm arm = findBranchArm(*program);
    if (arm.graph == nullptr) {
        out << "commit_exit_region: no branch in the program, so no region an exit could land in\n";
        return program;
    }
    if (arm.member == kInvalidNodeRef) {
        out << "commit_exit_region: arm " << arm.region << " of the branch at node " << arm.owner
            << " holds no node to read a value from\n";
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    auto &draft = session.edit(const_cast<GCGraph *>(arm.graph)).edit();
    // The value the graph hands on is read at its exit; the rewrite leaves the exit where an arm's
    // value is, which is the shape the calculus's counter-example (`f(b) = let y = if b then {not(b)}
    // else {false}; not(y)`, with the identity inlined onto the exit) lands in. The arm's own node is
    // the exit now, so nothing is moved but the reading.
    draft.setExitNode(arm.member);
    out << "commit_exit_region: graph '" << arm.graph->name() << "' now reads its value at node "
        << arm.member << ", which is in arm " << arm.region << " of the branch at node " << arm.owner
        << " and not in the function body\n"
        << std::flush;
    try {
        const Program *committed = session.commit();
        out << "commit_exit_region: the commit published it, which is the defect\n";
        return committed;
    } catch (const GraphCommitRefused &refused) {
        out << "commit_exit_region: refused by the commit checks, check="
            << commitCheckName(refused.violation().check) << '\n'
            << std::flush;
        return program;
    } catch (const std::runtime_error &refused) {
        // The seal's refusal, which is where this check lives: one reading, in the place every
        // commit path runs whatever it changed.
        out << "commit_exit_region: refused at the commit: " << refused.what() << '\n' << std::flush;
        return program;
    }
}

const camel::runtime::Program *
CommitCycleTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr) {
        return program;
    }
    out << "commit_cycle: the draft's input setters do not look for a cycle, the commit does\n"
        << std::flush;
    const GCGraph *graph = nullptr;
    gc_node_ref_t node   = kInvalidNodeRef;
    for (const GCGraph *candidate : program->graphs()) {
        if (candidate == nullptr) {
            continue;
        }
        for (gc_node_ref_t ref = 0; ref < candidate->nodeCount(); ++ref) {
            if (candidate->node(ref) != nullptr && !candidate->normInputsOf(ref).empty()) {
                graph = candidate;
                node  = ref;
                break;
            }
        }
        if (graph != nullptr) {
            break;
        }
    }
    if (graph == nullptr) {
        out << "commit_cycle: no node of the program reads anything\n";
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    auto &draft = session.edit(const_cast<GCGraph *>(graph)).edit();
    // One input set to the node that reads it: the shortest cycle there is. No setter refuses it --
    // one rewrite cannot see a cycle another one closes -- so the whole graph is what can be asked,
    // and the commit is where it is asked.
    draft.setNormInputs(node, std::span<const gc_node_ref_t>(&node, 1));
    out << "commit_cycle: graph '" << graph->name() << "' node " << node << " now reads itself\n"
        << std::flush;
    try {
        const Program *committed = session.commit();
        out << "commit_cycle: the commit published it, which is the defect\n";
        return committed;
    } catch (const GraphCommitRefused &refused) {
        out << "commit_cycle: refused by the commit checks, check="
            << commitCheckName(refused.violation().check) << '\n'
            << std::flush;
        return program;
    } catch (const std::runtime_error &refused) {
        out << "commit_cycle: refused at the commit: " << refused.what() << '\n' << std::flush;
        return program;
    }
}

const camel::runtime::Program *
CommitAncestorTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr) {
        return program;
    }
    out << "commit_ancestor: the regions a commit answers for are the ones it touched and every "
           "region around them\n"
        << std::flush;
    const BranchArm arm = findBranchArm(*program);
    if (arm.graph == nullptr) {
        out << "commit_ancestor: no branch in the program, so no region with a region under it\n";
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    auto &transaction = session.edit(const_cast<GCGraph *>(arm.graph));
    auto &draft       = transaction.edit();
    // The body's write waits for the branch that came before it, and that wait is the only thing
    // ordering the two. A pass that re-sequences the body's write takes it out of the chain, so what
    // is left in the body is a write and a branch that waits for nothing from it and is waited on by
    // nothing of it.
    gc_node_ref_t bodyWrite = kInvalidNodeRef;
    for (gc_node_ref_t ref = 0; ref < arm.graph->nodeCount(); ++ref) {
        const GCNode *node = arm.graph->node(ref);
        if (node == nullptr || node->kind != GCNodeKind::Oper ||
            arm.graph->nodeRegion(ref) != 0) {
            continue;
        }
        const auto *body = arm.graph->nodeBodyAs<GCOperBody>(ref);
        if (body != nullptr && std::string_view(body->uri()) == kPrintUri) {
            bodyWrite = ref;
            break;
        }
    }
    if (bodyWrite == kInvalidNodeRef) {
        out << "commit_ancestor: the body of graph '" << arm.graph->name()
            << "' writes nothing outside the branch, so nothing is there to be unordered\n";
        return program;
    }
    std::string waited;
    for (gc_node_ref_t wait : arm.graph->waitInputsOf(bodyWrite)) {
        waited += " ";
        waited += std::to_string(wait);
    }
    const gc_node_ref_t noChain[] = {};
    draft.setCtrlInputs(bodyWrite, std::span<const gc_node_ref_t>(noChain, 0));
    draft.setSyncInputs(bodyWrite, std::span<const gc_node_ref_t>(noChain, 0));
    // A print written inside the arm, on a value written inside the arm: the arm is the region it
    // landed in, and the branch node is what stands for it in the region outside. Nothing this commit
    // made is in that outer region, which is the pair the commit has to judge anyway.
    const gc_node_ref_t value = draft.materializeStaticValue(
        camel::core::rtdata::toSlot<float>(7.0f), camel::core::type::Type::Float32());
    const gc_node_ref_t spliced =
        draft.addOperNode(camel::core::type::Type::Float32(), nullptr, kPrintUri);
    draft.setNodeRegion(value, arm.region);
    draft.setNodeRegion(spliced, arm.region);
    draft.setNormInputs(spliced, std::span<const gc_node_ref_t>(&value, 1));
    out << "commit_ancestor: graph '" << arm.graph->name() << "' writes stdout at node " << bodyWrite
        << ", which waited for" << (waited.empty() ? " nothing" : waited)
        << " and now waits for nothing\n"
        << "commit_ancestor: arm " << arm.region << " of the branch at node " << arm.owner
        << " now writes stdout at node " << spliced << '\n'
        << "commit_ancestor: neither the branch node nor the write left in the body is a node this "
           "commit made\n"
        << std::flush;
    try {
        const Program *committed = session.commit();
        out << "commit_ancestor: the commit published it, which is the defect\n";
        return committed;
    } catch (const GraphCommitRefused &refused) {
        out << "commit_ancestor: refused, check=" << commitCheckName(refused.violation().check)
            << '\n'
            << std::flush;
        throw;
    }
}

const camel::runtime::Program *
CommitTierTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr) {
        return program;
    }
    const SameShapePair pair = findSameShapePair(*program);
    if (pair.noisy == nullptr) {
        out << "commit_tier: no two functions of one shape to tell apart\n";
        return program;
    }
    const bool wasAsked = camel::runtime::commitChecksEnabled();
    // The cell is the one answer a build can give. It is set to each of its two values here, and the
    // two drafts -- one that adds an access and one that adds none -- are read under both: what the
    // commit asks follows the draft and does not move when the cell does. Under the ruling of
    // 2026-11-16 the first draft would have been published with the cell off; that is the reading
    // this pass exists to make impossible.
    int refusedWithCellOff    = 0;
    int publishedWithCellOn   = 0;
    camel::runtime::setCommitChecksEnabled(false);
    {
        RuntimeGraphDraftSession session(context_, program);
        auto &transaction = session.edit(const_cast<GCGraph *>(pair.noisy));
        const SplicedPair spliced = spliceUnorderedPrintPair(transaction);
        out << "commit_tier: cell=0, draft adds an access (nodes " << spliced.first << " and "
            << spliced.second << " write stdout)\n"
            << std::flush;
        try {
            session.commit();
            out << "commit_tier: cell=0, the commit published it, so the tier followed the build\n";
        } catch (const GraphCommitRefused &refused) {
            refusedWithCellOff = 1;
            out << "commit_tier: cell=0, refused, check="
                << commitCheckName(refused.violation().check)
                << " -- the tier is the draft's, not the build's\n"
                << std::flush;
        }
    }
    camel::runtime::setCommitChecksEnabled(true);
    {
        // The other draft makes no node at all: it republishes one graph under the other's type,
        // which is a body that reaches past the type it is published under. The commit is not asked
        // about it, because the draft added no access -- so the commit publishes it, and that is the
        // reading, not a defect of this pass.
        RuntimeGraphDraftSession session(context_, program);
        session.edit(const_cast<GCGraph *>(pair.noisy)).edit().setFuncType(pair.quiet->funcType());
        out << "commit_tier: cell=1, draft adds no access (no node made; graph '"
            << pair.noisy->name() << "' takes the type of graph '" << pair.quiet->name() << "')\n"
            << std::flush;
        try {
            session.commit();
            publishedWithCellOn = 1;
            out << "commit_tier: cell=1, published, and nothing was asked -- the tier is the "
                   "draft's, not the build's\n"
                << std::flush;
        } catch (const GraphCommitRefused &refused) {
            out << "commit_tier: cell=1, refused, check="
                << commitCheckName(refused.violation().check)
                << " -- the tier followed the build, not the draft\n"
                << std::flush;
        }
    }
    camel::runtime::setCommitChecksEnabled(wasAsked);
    out << "commit_tier: cell=0 add-an-access -> refused=" << refusedWithCellOff
        << ", cell=1 adds-nothing -> published=" << publishedWithCellOn
        << "; the same two drafts answer the same under both builds\n"
        << std::flush;
    // The program is handed back unchanged: the two commits above are the reading, and what the
    // boundary check reads next must be the fixture, not the graph they published.
    return program;
}
