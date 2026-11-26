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
 * in Camel is exempt by the theorem, and the builds that
 * can afford it read the theorem anyway. A reading of that rule needs a program the check
 * refuses and a pass the exemption applies to, which is what the two halves of this file are.
 */

#include "camel/passes/test/commit_checks.h"

#include "../std/cxx/opt/generic/rewrite.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/base.h"
#include "camel/execute/pass/base.h"
#include "camel/execute/pass/registration.h"
#include "camel/runtime/control_regions.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/graph_txn.h"
#include "camel/runtime/resources.h"
#include "camel/runtime/type_agreement.h"

#include "graph/graph_view.h"

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

using namespace camel::runtime;

namespace {

/// The Camel pass the exemption is read on: `opt::cse`, the library's common-subexpression sweep
/// (`stdlib/opt.cml`), whose only rewriting interface is the graph module's. Its native twin is
/// `std::cxx::cse`, which is a C++ pass and is checked in every build.
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
bool sameShape(const camel::graph::GraphView &a, const camel::graph::GraphView &b) {
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
    for (const auto *graph : program.graphs()) {
        if (graph == nullptr) {
            continue;
        }
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            const GCNode *node = graph->node(ref);
            if (node == nullptr || node->kind != GCNodeKind::Func) {
                continue;
            }
            const auto *callee    = program.calleeOf(*graph, ref);
            if (callee == nullptr || callee == graph) {
                continue;
            }
            (writesOutside(program, callee) ? noisy : quiet).push_back({graph, ref, callee});
        }
    }
    for (const Call &loud : noisy) {
        for (const Call &soft : quiet) {
            if (loud.callee == soft.callee ||
                !sameShape(loud.callee->view(), soft.callee->view())) {
                continue;
            }
            return SameShapePair{loud.callee, soft.callee};
        }
    }
    return {};
}

/// The fold, in one commit, on the **call site** rather than on the graph's own signature: the value
/// a call runs through is given the quiet function's type, and the printing body behind it stays
/// where it is. The commit is not the reader that can refuse this: the graph it changed is `main`,
/// whose own clause still covers the print it reaches through that call, so nothing the commit
/// compares is out of bounds and it publishes. The call site is left declaring `pure` over a body
/// that writes the console -- which is the shape the pass boundary reads, at the one place the
/// commit's comparison does not look: the clause written at the call node against the footprint of
/// the callee behind it (`effectContainmentViolation`). Returns the program the commit made.
///
/// The session is opened over the whole program and only the caller is edited: naming a graph as the
/// session's root would make that graph the program's entry when the commit replaces it, and the
/// fixture would be a program whose entry takes a parameter.
const Program *foldTypeOf(
    const camel::core::context::context_ptr_t &context, const Program *program,
    const SameShapePair &pair, std::ostream &out) {
    gc_node_ref_t chosenGraph = kInvalidNodeRef;
    gc_node_ref_t chosenValue = kInvalidNodeRef;
    const GCGraph *chosenAt   = nullptr;
    for (const auto *graph : program->graphs()) {
        if (graph == nullptr) {
            continue;
        }
        for (gc_cnt_t i = 0; i < graph->nodeCount(); ++i) {
            const gc_node_ref_t ref = static_cast<gc_node_ref_t>(i);
            const GCNode *node      = graph->node(ref);
            if (node == nullptr || node->kind != GCNodeKind::Call) {
                continue;
            }
            const auto with = graph->withInputsOf(ref);
            if (with.empty() || graph->node(with[0]) == nullptr) {
                continue;
            }
            // The call that runs the printing function: its value carries that graph's type. The
            // first call found is the fallback, for a fixture whose value type is a copy.
            if (chosenAt == nullptr) {
                chosenAt    = graph;
                chosenGraph = ref;
                chosenValue = with[0];
            }
            if (graph->node(with[0])->dataType() == pair.noisy->funcType()) {
                chosenAt    = graph;
                chosenGraph = ref;
                chosenValue = with[0];
                break;
            }
        }
    }
    if (chosenAt != nullptr) {
        RuntimeGraphDraftSession session(context, program);
        auto &transaction = session.edit(const_cast<GCGraph *>(chosenAt));
        transaction.edit().setNodeDataType(chosenValue, pair.quiet->funcType());
        out << "fold_function_type: the call site at node " << chosenGraph << " of graph '"
            << chosenAt->name() << "' now declares '" << pair.quiet->funcType()->toString()
            << "', the type of graph '" << pair.quiet->name()
            << "', while the body it runs is '" << pair.noisy->name() << "'\n";
        return session.commit();
    }
    out << "fold_function_type: no call through a value to fold\n";
    return program;
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

/// Whether `ref` is a node of `graph` that writes the console. `println` in the prelude is
/// `#op(':io/println')` (stdlib/prelude.cml), so a node that names that operator writes `stdout`,
/// and a node this file adds with the same uri writes it too: one resource, two writers. The
/// question is the node's operator, asked of the graph's face, rather than a payload read.
bool writesTheConsole(const camel::graph::GraphView &graph, gc_node_ref_t ref) {
    const camel::core::OperId oper = graph.operatorOf(ref);
    return oper != camel::core::kNoOper &&
           camel::core::OperatorTable::instance().uri(oper) == kPrintUri;
}

ConsoleWrite findPrint(const Program &program) {
    for (const auto *graph : program.graphs()) {
        if (graph == nullptr) {
            continue;
        }
        // The walk is over the nodes the face answers, so a place that is not a node -- a removed
        // one, a draft's placeholder, a branch's JOIN -- is not visited: none of them writes.
        const camel::graph::GraphView &face = graph->view();
        for (const auto ref : face.nodes()) {
            if (writesTheConsole(face, ref)) {
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
/// what the draft added and not what the build is ( `commitAddsAccess` in
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
bool declaresNothing(const camel::graph::GraphView &graph) {
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
    for (const auto *graph : program.graphs()) {
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

/// One branch of one graph whose two arms compute a value each: the node that owns the branch, its
/// join, and the two arms the edit goes between. A constant written in an arm is hoisted to the body
/// (it has no region of its own to be read out of), so an arm whose result is a DATA node is not an
/// arm this reading can use.
struct SiblingArms {
    const GCGraph *graph        = nullptr;
    gc_node_ref_t brch          = kInvalidNodeRef;
    gc_node_ref_t join          = kInvalidNodeRef;
    uint32_t firstArm           = 0; // the arm the copy lands in
    uint32_t secondArm          = 0; // the arm the value is read from
    gc_node_ref_t firstResult   = kInvalidNodeRef; // the first arm's own value
    gc_node_ref_t secondResult  = kInvalidNodeRef; // the second arm's value
};

SiblingArms findSiblingArms(const Program &program) {
    for (const auto *graph : program.graphs()) {
        if (graph == nullptr) {
            continue;
        }
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            const GCNode *node = graph->node(ref);
            if (node == nullptr || node->kind != GCNodeKind::Brch) {
                continue;
            }
            const auto *body = graph->nodeBodyAs<GCBrchBody>(ref);
            if (body == nullptr || body->join == kInvalidNodeRef) {
                continue;
            }
            SiblingArms found;
            found.graph = graph;
            found.brch  = ref;
            found.join  = body->join;
            for (uint32_t region = 1; region < graph->regionCount(); ++region) {
                const GCRegion *record = graph->region(region);
                if (record == nullptr || record->kind != GCRegionKind::Arm || record->owner != ref) {
                    continue;
                }
                // The arm has to end in a node the arm itself holds: one hoisted to the body is not
                // a value only this arm writes.
                if (record->result == kInvalidNodeRef ||
                    graph->nodeRegion(record->result) != region) {
                    continue;
                }
                if (found.firstArm == 0) {
                    found.firstArm    = region;
                    found.firstResult = record->result;
                    continue;
                }
                found.secondArm    = region;
                found.secondResult = record->result;
                return found;
            }
        }
    }
    return {};
}

/// A COPY in the first arm that reads `from`, and the branch's join reading the copy where the
/// first arm's own value stood, so the arm ends in the copy and the read is on the arm's edge.
/// Nothing is spliced that no execution reaches: a value nothing reads is a question nobody asks.
gc_node_ref_t spliceCopyBetweenArms(
    GraphTransaction &transaction, const SiblingArms &arms, gc_node_ref_t from) {
    auto &draft                  = transaction.edit();
    const GCNode *replaced       = arms.graph->node(arms.firstResult);
    camel::core::type::Type *type = replaced != nullptr ? replaced->dataType() : nullptr;
    const gc_node_ref_t copy     = draft.addOperNode(type, nullptr, ":op/copy");
    draft.setNormInputs(copy, std::span<const gc_node_ref_t>(&from, 1));
    draft.setNodeRegion(copy, arms.firstArm);
    std::vector<gc_node_ref_t> joinInputs(
        arms.graph->withInputsOf(arms.join).begin(),
        arms.graph->withInputsOf(arms.join).end());
    for (gc_node_ref_t &slot : joinInputs) {
        if (slot == arms.firstResult) {
            slot = copy;
        }
    }
    draft.setWithInputs(
        arms.join, std::span<const gc_node_ref_t>(joinInputs.data(), joinInputs.size()));
    draft.setRegionResult(arms.firstArm, copy);
    return copy;
}

/// A loop whose body computes a value, and the three nodes a read of that value can be written
/// against: the value itself, a value the function body computed before the loop, and the node of
/// the body that reads that value. The third is what makes the read live: the splice puts the copy
/// where the read stood, so the same node reads the copy instead -- a read nothing executes is a
/// question nobody asks (`validateRuntimeBranchArms` walks only what the exit depends on).
struct LoopBodyRead {
    const GCGraph *graph     = nullptr;
    gc_node_ref_t loop       = kInvalidNodeRef;
    uint32_t bodyRegion      = 0;
    gc_node_ref_t bodyValue  = kInvalidNodeRef; // a value the loop's body computes
    gc_node_ref_t outerValue = kInvalidNodeRef; // a value the function body computed, before the loop
    gc_node_ref_t place      = kInvalidNodeRef; // a node of the body that reads outerValue
};

LoopBodyRead findLoopBodyRead(const Program &program) {
    for (const auto *graph : program.graphs()) {
        if (graph == nullptr || !graph->hasNodePayload()) {
            continue;
        }
        const ControlRegions regions(*graph);
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            if (!*it || (*it)->kind != GCNodeKind::Loop) {
                continue;
            }
            const auto parts = regions.loopPartsOf(it.ref());
            if (parts[1] == 0) {
                continue;
            }
            LoopBodyRead found;
            found.graph      = graph;
            found.loop       = it.ref();
            found.bodyRegion = parts[1];
            // The value the body computes: an operator's result inside the body. A port is what a
            // region is handed or hands on, not a value a node of the body computes, so ports are
            // skipped throughout.
            for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
                const GCNode *node = graph->node(ref);
                if (node == nullptr || node->kind != GCNodeKind::Oper ||
                    node->dataType() == nullptr || graph->nodeRegion(ref) != parts[1]) {
                    continue;
                }
                found.bodyValue = ref; // the last one: the body's own computation
            }
            // A value the function body computed, and a node of the loop's body that reads it: the
            // direction every loop in the language writes, and where the splice stands. The value is
            // an operator's result (`:op/...`) or a copy of one -- not the cell a `var` lives in and
            // not a write, because a cell is not a value a copy can stand for.
            for (gc_node_ref_t ref = 0; ref < graph->nodeCount() && found.place == kInvalidNodeRef;
                 ++ref) {
                if (graph->node(ref) == nullptr || graph->nodeRegion(ref) != parts[1]) {
                    continue;
                }
                for (gc_node_ref_t input : graph->normInputsOf(ref)) {
                    const GCNode *read = graph->node(input);
                    if (read == nullptr || graph->nodeRegion(input) != 0 ||
                        read->dataType() == nullptr) {
                        continue;
                    }
                    if (read->kind != GCNodeKind::Oper) {
                        continue;
                    }
                    const auto *oper = graph->nodeBodyAs<GCOperBody>(input);
                    if (oper != nullptr && std::string_view(oper->uri()).rfind(":op/", 0) == 0) {
                        found.place      = ref;
                        found.outerValue = input;
                        break;
                    }
                }
            }
            if (found.bodyValue == kInvalidNodeRef || found.place == kInvalidNodeRef ||
                found.outerValue == kInvalidNodeRef) {
                continue;
            }
            return found;
        }
    }
    return {};
}

/// A COPY that reads `from`, put in `region`, standing where `replaced` stood in `place`'s inputs.
/// The region is written *after* the input: a value edge pulls the reader into the region the value
/// is in (`GraphDraft::inheritRegion`), which is exactly the move this fixture must not let happen,
/// so the region the read is written in is the last word.
gc_node_ref_t spliceRead(
    GraphTransaction &transaction, const LoopBodyRead &found, gc_node_ref_t from, uint32_t region) {
    auto &draft = transaction.edit();
    const GCNode *read = found.graph->node(from);
    if (read == nullptr || read->dataType() == nullptr) {
        return kInvalidNodeRef;
    }
    const gc_node_ref_t copy = draft.addOperNode(read->dataType(), nullptr, ":op/copy");
    draft.setNormInputs(copy, std::span<const gc_node_ref_t>(&from, 1));
    draft.setNodeRegion(copy, region);
    std::vector<gc_node_ref_t> inputs(
        found.graph->normInputsOf(found.place).begin(),
        found.graph->normInputsOf(found.place).end());
    for (gc_node_ref_t &slot : inputs) {
        if (slot == found.outerValue) {
            slot = copy;
        }
    }
    draft.setNormInputs(found.place, std::span<const gc_node_ref_t>(inputs.data(), inputs.size()));
    return copy;
}

/// The three functions `test::commit_port_type` reads: one whose signature a draft replaces, one
/// whose signature *declares another parameter type* than that one's, and one whose parameter types
/// are the same. Every one of them is called: a graph nobody calls is not in the program a pass is
/// handed, and the reading has to be of graphs the fixture really has.
struct SignatureTrio {
    const GCGraph *taken     = nullptr; // the graph the two drafts republish
    const GCGraph *different = nullptr; // same arity, other parameter types
    const GCGraph *same      = nullptr; // same arity, the same parameter types
};

/// The parameter lists, and not the exit types: a draft that writes a new signature writes both
/// halves at once, and the half this pass is about is the one a call site is checked against.
bool sameParameterCount(const camel::graph::GraphView &a, const camel::graph::GraphView &b) {
    const auto *left  = a.funcType();
    const auto *right = b.funcType();
    return left != nullptr && right != nullptr &&
           left->normTypesCount() == right->normTypesCount() &&
           left->withTypesCount() == right->withTypesCount();
}

bool sameParameterTypes(const camel::graph::GraphView &a, const camel::graph::GraphView &b) {
    const auto *left  = a.funcType();
    const auto *right = b.funcType();
    if (!sameParameterCount(a, b)) {
        return false;
    }
    for (size_t i = 0; i < left->normTypesCount(); ++i) {
        if (!typesCompatible(left->normTypeAt(i), right->normTypeAt(i))) {
            return false;
        }
    }
    for (size_t i = 0; i < left->withTypesCount(); ++i) {
        if (!typesCompatible(left->withTypeAt(i), right->withTypeAt(i))) {
            return false;
        }
    }
    return true;
}

SignatureTrio findSignatureTrio(const Program &program) {
    std::vector<const GCGraph *> called;
    for (const auto *graph : program.graphs()) {
        if (graph == nullptr) {
            continue;
        }
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            const GCNode *node = graph->node(ref);
            if (node == nullptr || node->kind != GCNodeKind::Func) {
                continue;
            }
            const auto *callee    = program.calleeOf(*graph, ref);
            if (callee == nullptr || callee == graph) {
                continue;
            }
            if (std::find(called.begin(), called.end(), callee) == called.end()) {
                called.push_back(callee);
            }
        }
    }
    for (const auto *taken : called) {
        const GCGraph *different = nullptr;
        const GCGraph *same      = nullptr;
        for (const auto *other : called) {
            if (other == taken ||
                !sameParameterCount(taken->view(), other->view())) {
                continue;
            }
            if (sameParameterTypes(taken->view(), other->view())) {
                if (same == nullptr) {
                    same = other;
                }
            } else if (different == nullptr) {
                different = other;
            }
        }
        if (different != nullptr && same != nullptr) {
            return SignatureTrio{taken, different, same};
        }
    }
    return {};
}

} // namespace

const camel::runtime::Program *
CommitPortTypeTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr) {
        return program;
    }
    out << "commit_port_type: a graph publishes one statement about one parameter twice -- as a port "
           "node a call site is checked against and as an entry of the signature the value and the "
           "entry arguments are read through -- and the commit asks whether the two agree of every "
           "graph it encodes, so a graph that says two things about one parameter is refused\n"
        << std::flush;
    const SignatureTrio trio = findSignatureTrio(*program);
    if (trio.taken == nullptr) {
        out << "commit_port_type: no three called functions whose parameter types tell the two "
               "drafts apart\n"
            << std::flush;
        return program;
    }
    out << "commit_port_type: graph '" << trio.taken->name() << "' declares '"
        << trio.taken->funcType()->toString() << "', graph '" << trio.different->name()
        << "' declares '" << trio.different->funcType()->toString() << "' (other parameter types), "
           "graph '"
        << trio.same->name() << "' declares '" << trio.same->funcType()->toString()
        << "' (the same parameter types)\n"
        << std::flush;
    int refused   = 0;
    int published = 0;
    // The draft that must be refused: another function's signature over this function's port nodes,
    // which is what a fold that decides two bodies are one function writes.
    {
        RuntimeGraphDraftSession session(context_, program);
        auto &draft = session.edit(const_cast<GCGraph *>(trio.taken)).edit();
        draft.setFuncType(trio.different->funcType());
        out << "commit_port_type: graph '" << trio.taken->name() << "' takes the signature of graph '"
            << trio.different->name() << "' and keeps its port nodes\n"
            << std::flush;
        try {
            (void)session.commit();
            out << "commit_port_type: the commit published it, which is the defect\n" << std::flush;
        } catch (const std::runtime_error &refusedAtCommit) {
            ++refused;
            out << "commit_port_type: refused at the commit: " << refusedAtCommit.what() << '\n'
                << std::flush;
        }
    }
    // The draft that must not be refused: the same replacement with a signature whose parameter
    // types are the same. Without it, "it was refused" would mean only "a signature was replaced",
    // and every legitimately retyped graph would be unpublishable.
    {
        RuntimeGraphDraftSession session(context_, program);
        auto &draft = session.edit(const_cast<GCGraph *>(trio.taken)).edit();
        draft.setFuncType(trio.same->funcType());
        out << "commit_port_type: graph '" << trio.taken->name() << "' takes the signature of graph '"
            << trio.same->name() << "', whose parameter types are the same\n"
            << std::flush;
        try {
            (void)session.commit();
            ++published;
            out << "commit_port_type: the commit published it, and nothing was refused -- a "
                   "signature replacement is not what the check refuses\n"
                << std::flush;
        } catch (const std::runtime_error &refusedAtCommit) {
            out << "commit_port_type: refused at the commit: " << refusedAtCommit.what() << '\n'
                << std::flush;
        }
    }
    out << "commit_port_type: signature of other parameter types -> refused=" << refused
        << ", signature of the same parameter types -> published=" << published << '\n'
        << std::flush;
    // The program is handed back unchanged: the two commits above are the reading, and what runs
    // next must be the fixture and not either graph they would have published.
    return program;
}

const camel::runtime::Program *
CommitLoopBodyReadTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr) {
        return program;
    }
    out << "commit_loop_body_read: the arm of a branch and the body of a loop are both regions of "
           "the graph, so a read across either is the same question -- a node reads only what runs "
           "whenever it runs\n"
        << std::flush;
    const LoopBodyRead found = findLoopBodyRead(*program);
    if (found.graph == nullptr) {
        out << "commit_loop_body_read: no loop whose body computes a value with no branch around it, "
               "so there is no read to write\n"
            << std::flush;
        return program;
    }
    out << "commit_loop_body_read: graph '" << found.graph->name() << "' has LOOP " << found.loop
        << " with its body at region " << found.bodyRegion
        << " and no BRCH anywhere; the body computes node " << found.bodyValue
        << ", the function body computed node " << found.outerValue << ", and node " << found.place
        << " of the body reads it\n"
        << std::flush;
    // The read that must be refused: a COPY in the function body reading a value of the loop's body,
    // standing where the body's own read of the function body's value stood. The value the copy
    // reads exists only while a round runs, and the copy does not run in the loop.
    {
        RuntimeGraphDraftSession session(context_, program);
        auto &transaction = session.edit(const_cast<GCGraph *>(found.graph));
        const gc_node_ref_t copy = spliceRead(transaction, found, found.bodyValue, 0);
        out << "commit_loop_body_read: graph '" << found.graph->name() << "' now has COPY " << copy
            << " in the body (region 0) reading node " << found.bodyValue << " of the body of LOOP "
            << found.loop << " (region " << found.bodyRegion << ")\n"
            << std::flush;
        try {
            (void)session.commit();
            out << "commit_loop_body_read: the commit published it, which is the defect\n"
                << std::flush;
        } catch (const GraphCommitRefused &refused) {
            out << "commit_loop_body_read: refused by the commit checks, check="
                << commitCheckName(refused.violation().check) << '\n'
                << std::flush;
        } catch (const std::runtime_error &refused) {
            out << "commit_loop_body_read: refused at the commit: " << refused.what() << '\n'
                << std::flush;
        }
    }
    // The two reads that must not be refused. Without them, "it was refused" would mean only "a
    // read across regions is always refused", which is a reading of neither rule.
    //
    // (a) the same COPY, in the function body, reading a value of the region it runs in.
    {
        RuntimeGraphDraftSession session(context_, program);
        auto &transaction = session.edit(const_cast<GCGraph *>(found.graph));
        const gc_node_ref_t copy = spliceRead(transaction, found, found.outerValue, 0);
        out << "commit_loop_body_read: graph '" << found.graph->name() << "' now has COPY " << copy
            << " in the body (region 0) reading node " << found.outerValue
            << " of the same region\n"
            << std::flush;
        const Program *committed = session.commit();
        out << "commit_loop_body_read: the copy that reads a value of its own region was published\n"
            << std::flush;
        program = committed;
    }
    // (b) a COPY inside the loop's body reading a value the function body computed -- the direction
    // of every loop body in the language, and the one a check that judged the region pair by the
    // wrong end would refuse.
    {
        const LoopBodyRead again = findLoopBodyRead(*program);
        RuntimeGraphDraftSession session(context_, program);
        auto &transaction = session.edit(const_cast<GCGraph *>(again.graph));
        const gc_node_ref_t copy =
            spliceRead(transaction, again, again.outerValue, again.bodyRegion);
        out << "commit_loop_body_read: graph '" << again.graph->name() << "' now has COPY " << copy
            << " in the body of LOOP " << again.loop << " (region " << again.bodyRegion
            << ") reading node " << again.outerValue << " of the function body\n"
            << std::flush;
        const Program *committed = session.commit();
        out << "commit_loop_body_read: the copy in the loop's body that reads a value of the "
               "function body was published\n"
            << std::flush;
        program = committed;
    }
    return program;
}

const camel::runtime::Program *
CommitSiblingArmTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr) {
        return program;
    }
    out << "commit_sibling_arm: the seal asks whether a node reads only what runs when it runs "
           "(`validateRuntimeBranchArms`, src/runtime/branch_arms.cpp), not only whether it reads a "
           "port of a region it is not in (`checkGraphCrossReferences`, src/runtime/graph_edges.cpp)\n"
        << std::flush;
    const SiblingArms arms = findSiblingArms(*program);
    if (arms.graph == nullptr) {
        out << "commit_sibling_arm: no branch of two arms that compute a value each\n";
        return program;
    }
    // The half that must be refused: a copy in the first arm reading the second arm's value. The
    // arms are regions of one branch, so only one of them runs: the read never happens, and the
    // value it would read was never written.
    {
        RuntimeGraphDraftSession session(context_, program);
        auto &transaction = session.edit(const_cast<GCGraph *>(arms.graph));
        const gc_node_ref_t copy = spliceCopyBetweenArms(transaction, arms, arms.secondResult);
        out << "commit_sibling_arm: graph '" << arms.graph->name() << "' arm " << arms.firstArm
            << " of BRCH " << arms.brch << " now ends in node " << copy
            << ", which reads node " << arms.secondResult << " of arm " << arms.secondArm
            << "\n"
            << std::flush;
        try {
            const Program *committed = session.commit();
            out << "commit_sibling_arm: the commit published it, which is the defect\n";
            (void)committed;
        } catch (const GraphCommitRefused &refused) {
            out << "commit_sibling_arm: refused by the commit checks, check="
                << commitCheckName(refused.violation().check) << '\n'
                << std::flush;
        } catch (const std::runtime_error &refused) {
            out << "commit_sibling_arm: refused at the commit: " << refused.what() << '\n'
                << std::flush;
        }
    }
    // The half that must publish: the same copy reading a value of the arm it sits in. Without it,
    // "it was refused" would mean only "a copy across arms is always refused", which is a reading
    // of neither rule.
    RuntimeGraphDraftSession session(context_, program);
    auto &transaction = session.edit(const_cast<GCGraph *>(arms.graph));
    const gc_node_ref_t copy = spliceCopyBetweenArms(transaction, arms, arms.firstResult);
    out << "commit_sibling_arm: graph '" << arms.graph->name() << "' arm " << arms.firstArm
        << " of BRCH " << arms.brch << " now ends in node " << copy << ", which reads node "
        << arms.firstResult << " of the same arm\n"
        << std::flush;
    const Program *committed = session.commit();
    out << "commit_sibling_arm: the copy that reads a value of its own arm was published\n"
        << std::flush;
    return committed;
}

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
    // The check the containment half is about, written where the commit asks it: the
    // draft adds an access, so the commit is asked in every build, and what it is judged against is
    // the type the graph publishes. This is the same defect `test::fold_function_type` leaves behind
    // (a printing body under a type that declares nothing), reached by the door a rewrite actually
    // uses: a spliced node, not a replaced type.
    const GCGraph *quiet = nullptr;
    for (const auto *graph : program->graphs()) {
        if (graph != nullptr && graph->nodeCount() > 1 && declaresNothing(graph->view())) {
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
    for (const auto *candidate : program->graphs()) {
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
    {
        const camel::graph::GraphView &face = arm.graph->view();
        for (const auto ref : face.nodes()) {
            if (face.regionOf(ref) != 0) {
                continue;
            }
            if (writesTheConsole(face, ref)) {
                bodyWrite = ref;
                break;
            }
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
    // two drafts -- one that adds an access and one that moves the signature -- are read under both:
    // what the commit asks follows the draft and does not move when the cell does. Were it to follow
    // the cell, the first draft would have been published with the cell off; that is the reading
    // this pass exists to make impossible.
    int refusedWithCellOff = 0;
    int narrowedWasRefused = 0;
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
        // which is a body that reaches past the type it is published under. Nothing was added and
        // no node was changed, so neither list the commit keeps can see it -- but the signature is
        // the other side of the comparison the containment check makes, and it moved. The commit is
        // asked, and refuses the body the new clause no longer covers.
        RuntimeGraphDraftSession session(context_, program);
        session.edit(const_cast<GCGraph *>(pair.noisy)).edit().setFuncType(pair.quiet->funcType());
        out << "commit_tier: cell=1, draft adds no access (no node made; graph '"
            << pair.noisy->name() << "' takes the type of graph '" << pair.quiet->name() << "')\n"
            << std::flush;
        try {
            session.commit();
            out << "commit_tier: cell=1, the commit published it, so a signature that narrowed was "
                   "not read\n"
                << std::flush;
        } catch (const GraphCommitRefused &refused) {
            narrowedWasRefused = 1;
            out << "commit_tier: cell=1, refused, check="
                << commitCheckName(refused.violation().check)
                << " -- the signature moved and the body was read against the new one\n"
                << "commit_tier: cell=1, the refusal says: " << refused.violation().text << "\n"
                << std::flush;
        }
    }
    camel::runtime::setCommitChecksEnabled(wasAsked);
    out << "commit_tier: cell=0 add-an-access -> refused=" << refusedWithCellOff
        << ", cell=1 narrows-the-signature -> refused=" << narrowedWasRefused
        << "; the tier is the draft's, and the signature is the other side of the same comparison\n"
        << std::flush;
    // The program is handed back unchanged: the two commits above are the reading, and what the
    // boundary check reads next must be the fixture, not the graph they published.
    return program;
}

/// Two writes to the console in the body of one graph, written as two statements of one `sync`
/// block: `second` waits for `first`, and `readsFirst` says whether the value of `first` is what
/// `second` prints. The two fixtures the reading needs differ only there -- a pair whose whole order
/// is the chain between them (which an edit can take away) and a pair a data edge orders as well
/// (which it cannot).
struct ChainedPrints {
    const GCGraph *graph = nullptr;
    gc_node_ref_t first  = kInvalidNodeRef;
    gc_node_ref_t second = kInvalidNodeRef;
    bool readsFirst      = false;
};

/// Whether a node reads a value: the ordinary inputs of the reader. The question is the graph's
/// shape, so it is asked of its face.
bool readsValue(
    const camel::graph::GraphView &graph, gc_node_ref_t reader, gc_node_ref_t value) {
    for (const gc_node_ref_t input : graph.inputs(reader, camel::graph::PortGroup::Normal)) {
        if (input == value) {
            return true;
        }
    }
    return false;
}

/// The first chained pair of prints in the program whose second write reads its first the given way.
/// The walk is over the stored places because it asks `waitInputsOf` -- a block's chain is control
/// inputs, which the graph's face does not answer as edges -- so the frozen graph is the face's
/// owner here and the predicate is asked of the face.
ChainedPrints findChainedPrints(const Program &program, bool readsFirst) {
    for (const auto *graph : program.graphs()) {
        if (graph == nullptr) {
            continue;
        }
        const camel::graph::GraphView &face = graph->view();
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            if (!writesTheConsole(face, ref)) {
                continue;
            }
            for (gc_node_ref_t wait : graph->waitInputsOf(ref)) {
                if (!writesTheConsole(face, wait)) {
                    continue;
                }
                const bool reads = readsValue(face, ref, wait);
                if (reads == readsFirst) {
                    return ChainedPrints{graph, wait, ref, reads};
                }
            }
        }
    }
    return {};
}

const camel::runtime::Program *
CommitInPlaceTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr) {
        return program;
    }
    out << "commit_inplace: the tier is which edit the draft used, not whether it made a node\n"
        << std::flush;
    const ChainedPrints byChain   = findChainedPrints(*program, false);
    const ChainedPrints byValue   = findChainedPrints(*program, true);
    if (byChain.graph == nullptr || byValue.graph == nullptr) {
        out << "commit_inplace: the program has no pair of console writes chained in one sync block "
               "and ordered only by that chain, or none ordered by a value, so nothing is there to "
               "be unordered\n";
        return program;
    }
    const std::span<const gc_node_ref_t> none;
    // The edit, and it is the same one in both drafts: the second write is taken out of its block's
    // chain -- no control input, no sync input. No node is made, none is erased, no operator is
    // swapped. Under a tier read off `made` this commit was asked nothing at all.
    const auto clearTheChain = [&](const ChainedPrints &pair) -> bool {
        RuntimeGraphDraftSession session(context_, program);
        auto &draft = session.edit(const_cast<GCGraph *>(pair.graph)).edit();
        draft.setCtrlInputs(pair.second, none);
        draft.setSyncInputs(pair.second, none);
        draft.setOrderInputs(pair.second, none);
        out << "commit_inplace: graph '" << pair.graph->name() << "': node " << pair.second
            << " waited for node " << pair.first << " and now waits for nothing"
            << "; the draft makes no node and records " << draft.lowLevelEdits().size()
            << " node(s) changed in place\n"
            << std::flush;
        try {
            session.commit();
            out << "commit_inplace: asked the effect and the order checks (the table's last row: an "
                   "edge taken away owes all six), and published\n"
                << std::flush;
            return true;
        } catch (const GraphCommitRefused &refused) {
            out << "commit_inplace: asked the effect and the order checks (the table's last row: an "
                   "edge taken away owes all six), and refused, check="
                << commitCheckName(refused.violation().check) << '\n'
                << std::flush;
            return false;
        }
    };
    // The pair whose whole order was the chain: the two writes are now ordered by nothing, and the
    // commit that publishes that is the defect.
    const bool publishedUnordered = clearTheChain(byChain);
    out << "commit_inplace: two writes to stdout with no edge ordering either before the other --"
        << (publishedUnordered ? " the commit published it, which is the defect\n"
                               : " refused\n")
        << std::flush;
    // The pair a data edge orders as well: the same edit, the same question, and nothing broken.
    const bool publishedOrdered = clearTheChain(byValue);
    out << "commit_inplace: the same edit on a pair the second write reads the first one's value "
           "from --"
        << (publishedOrdered ? " published, which is the answer: the question was asked and the "
                               "graph did not break it\n"
                             : " refused, which would mean the tier refuses an in-place edit as "
                               "such\n")
        << std::flush;
    out << "commit_inplace: unordered-published=" << publishedUnordered
        << " ordered-published=" << publishedOrdered << '\n'
        << std::flush;
    // The same two writes with the order turned round: the chain is taken away in all three kinds, and
    // an order edge now says the second runs before the first. A commit that required the order the
    // program was written in would refuse this; the check asks only that an order exist, because
    // which of the two runs first is a meaning a pass may change (design principles 1) and not a
    // promise this check fixes. All three kinds have to go: the block's chain reaches its statement
    // as an order edge as well as a wait, and an edge left behind is the old direction -- writing
    // the opposite one over it is a cycle, which the seal reports before the commit is asked.
    const auto reverseTheOrder = [&](const ChainedPrints &pair) -> bool {
        RuntimeGraphDraftSession session(context_, program);
        auto &draft = session.edit(const_cast<GCGraph *>(pair.graph)).edit();
        const gc_node_ref_t waits = pair.second;
        draft.setCtrlInputs(pair.second, none);
        draft.setSyncInputs(pair.second, none);
        draft.setOrderInputs(pair.second, none);
        draft.setOrderInputs(pair.first, std::span<const gc_node_ref_t>(&waits, 1));
        out << "commit_inplace: graph '" << pair.graph->name() << "': node " << pair.first
            << " now runs after node " << pair.second << ", the order turned round\n"
            << std::flush;
        try {
            session.commit();
            out << "commit_inplace: the same edit with the order turned round -- published, which "
                   "is the answer: the check asks for an order, not for the one the program wrote\n"
                << std::flush;
            return true;
        } catch (const GraphCommitRefused &refused) {
            out << "commit_inplace: the same edit with the order turned round -- refused, check="
                << commitCheckName(refused.violation().check)
                << ", which would mean the check fixes the direction\n"
                << std::flush;
            return false;
        }
    };
    const bool reversedPublished = reverseTheOrder(byChain);
    out << "commit_inplace: reversed-published=" << reversedPublished << '\n' << std::flush;
    // The program is handed back unchanged: the two commits above are the reading.
    return program;
}

/// A graph the transaction **makes**, out of nothing: one ordinary parameter, whose port node carries
/// `carried` while the signature declares it `declared`, and the graph hands that value on -- so the
/// signature and the port node are the two statements about parameter 0 that a made graph writes.
/// Nothing calls it and no program holds it; `encode` is the only way it becomes a graph at all, and
/// the name is the one the diagnostic quotes.
GCGraph *makeGraphWithOnePort(
    const camel::core::context::context_ptr_t &context, camel::core::type::Type *declared,
    camel::core::type::Type *carried, const char *mangledName, const char *name) {
    using camel::core::type::FunctionType;
    using camel::core::type::param_vec_t;
    const param_vec_t withTypes;
    param_vec_t normTypes;
    normTypes.emplace_back(declared, false);
    FunctionType *signature =
        FunctionType::create(withTypes, normTypes, /*returnType*/ carried);
    auto transaction         = GraphTransaction::open(context->graphIds().allocate(), signature);
    auto &draft              = transaction->edit();
    const gc_node_ref_t port = draft.addPortNode(carried, 0);
    draft.appendNormPort(port);
    // The value the graph hands on is the one its parameter carries: the exit is not the question
    // here (`checkReturnValueType` reads the exit against the signature), so it is written to agree
    // and only the port can disagree.
    draft.setExitNode(port);
    return draft.encode(mangledName, name);
}

const camel::runtime::Program *
CommitMadeGraphPortsTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr || program->root() == nullptr) {
        out << "commit_made_graph_ports: the program has no root, so there is no session to make a "
               "graph in\n";
        return program;
    }
    out << "commit_made_graph_ports: a made graph has no signature the program held, so the port "
           "question is owed to it because it was made -- not because a signature it had was "
           "replaced\n"
        << std::flush;
    int refused   = 0;
    int published = 0;
    const Program *current = program;
    // The made graph whose port node carries another type than the signature declares it as: the
    // commit must refuse it, and the diagnostic must name the parameter.
    {
        RuntimeGraphDraftSession session(context_, current);
        auto *made    = makeGraphWithOnePort(
            context_,
            camel::core::type::Type::Int32(),
            camel::core::type::Type::Float32(),
            "test$made_port_mismatch",
            "made$mismatch");
        session.noteMadeGraph(made);
        out << "commit_made_graph_ports: made graph '" << made->name()
            << "' declares parameter 0 as 'i32' and its port node carries an 'f32'\n"
            << std::flush;
        try {
            current = session.commit();
            out << "commit_made_graph_ports: the commit published it, which is the defect\n"
                << std::flush;
        } catch (const std::runtime_error &refusedAtCommit) {
            ++refused;
            out << "commit_made_graph_ports: refused at the commit: " << refusedAtCommit.what()
                << '\n'
                << std::flush;
        }
    }
    // The positive half: the same graph, made the same way, with the port node carrying the type the
    // signature declares. Without it, "it was refused" would mean only "a made graph is always
    // refused", which is a reading of neither rule.
    {
        RuntimeGraphDraftSession session(context_, current);
        auto *made    = makeGraphWithOnePort(
            context_,
            camel::core::type::Type::Int32(),
            camel::core::type::Type::Int32(),
            "test$made_port_match",
            "made$match");
        session.noteMadeGraph(made);
        out << "commit_made_graph_ports: made graph '" << made->name()
            << "' declares parameter 0 as 'i32' and its port node carries an 'i32'\n"
            << std::flush;
        try {
            current = session.commit();
            ++published;
            out << "commit_made_graph_ports: the commit published it, and nothing was refused\n"
                << std::flush;
        } catch (const std::runtime_error &refusedAtCommit) {
            out << "commit_made_graph_ports: refused at the commit: " << refusedAtCommit.what()
                << '\n'
                << std::flush;
        }
    }
    out << "commit_made_graph_ports: made graph, port of another type -> refused=" << refused
        << ", port of the declared type -> published=" << published << '\n'
        << std::flush;
    return current;
}

/// A graph the transaction **makes**, with two writes to the console in its body and, in one half,
/// nothing ordering them: no edge, no gate, no region. Nothing in the program holds it, so no list of
/// nodes a draft edited can name it -- the whole graph is what the commit answers for.
GCGraph *makeGraphWithTwoPrints(
    const camel::core::context::context_ptr_t &context, bool ordered, const char *mangledName,
    const char *name) {
    using camel::core::type::FunctionType;
    using camel::core::type::param_vec_t;
    const param_vec_t withTypes;
    param_vec_t normTypes;
    normTypes.emplace_back(camel::core::type::Type::Float32(), false);
    FunctionType *signature =
        FunctionType::create(withTypes, normTypes, /*returnType*/ camel::core::type::Type::Float32());
    auto transaction         = GraphTransaction::open(context->graphIds().allocate(), signature);
    auto &draft              = transaction->edit();
    const gc_node_ref_t port = draft.addPortNode(camel::core::type::Type::Float32(), 0);
    draft.appendNormPort(port);
    const gc_node_ref_t first =
        draft.addOperNode(camel::core::type::Type::Float32(), nullptr, kPrintUri);
    const gc_node_ref_t second =
        draft.addOperNode(camel::core::type::Type::Float32(), nullptr, kPrintUri);
    const gc_node_ref_t input[1] = {port};
    draft.setNormInputs(first, input);
    draft.setNormInputs(second, input);
    if (ordered) {
        const gc_node_ref_t waits[1] = {first};
        draft.setSyncInputs(second, waits);
    }
    draft.setExitNode(port);
    return draft.encode(mangledName, name);
}

const camel::runtime::Program *
CommitMadeGraphOrderTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr || program->root() == nullptr) {
        out << "commit_made_order: the program has no root, so there is no session to make a graph "
               "in\n";
        return program;
    }
    out << "commit_made_order: a made graph has no version the program held, so every node in it "
           "is one this commit put there\n"
        << std::flush;
    int refused          = 0;
    int published        = 0;
    const Program *current = program;
    // Two writes to stdout in one body, ordered against nothing: the commit must be asked the order
    // check (the graph is new, so the whole graph is the scope) and must refuse the pair.
    {
        RuntimeGraphDraftSession session(context_, current);
        auto *made    = makeGraphWithTwoPrints(
            context_, /*ordered=*/false, "test$made_order_unordered", "made$unordered");
        session.noteMadeGraph(made);
        out << "commit_made_order: made graph '" << made->name()
            << "' writes stdout twice and nothing orders the two\n"
            << std::flush;
        try {
            current = session.commit();
            out << "commit_made_order: the commit published it, which is the defect\n" << std::flush;
        } catch (const GraphCommitRefused &refusedAtCommit) {
            ++refused;
            out << "commit_made_order: refused, check="
                << commitCheckName(refusedAtCommit.violation().check) << ": "
                << refusedAtCommit.violation().text << '\n'
                << std::flush;
        }
    }
    // The positive half: the same graph with an edge between the two writes. Without it, "it was
    // refused" would mean only "a made graph is always refused", which is a reading of neither rule.
    {
        RuntimeGraphDraftSession session(context_, current);
        auto *made =
            makeGraphWithTwoPrints(context_, /*ordered=*/true, "test$made_order_ordered", "made$ordered");
        session.noteMadeGraph(made);
        out << "commit_made_order: made graph '" << made->name()
            << "' writes stdout twice with the second waiting for the first\n"
            << std::flush;
        try {
            current = session.commit();
            ++published;
            out << "commit_made_order: the same graph with the two ordered -- published\n"
                << std::flush;
        } catch (const GraphCommitRefused &refusedAtCommit) {
            out << "commit_made_order: refused, check="
                << commitCheckName(refusedAtCommit.violation().check) << ": "
                << refusedAtCommit.violation().text << '\n'
                << std::flush;
        }
    }
    out << "commit_made_order: made graph, two writes unordered -> refused=" << refused
        << ", the same two ordered -> published=" << published << '\n'
        << std::flush;
    return current;
}

// Retyping one call site twice: the reading that says whether the second `materializeStaticValue`
// is the type the encoded program carries. A pass that widens a callee's clause widens what its
// callers declare, round after round, and a value that keeps the type of the round before it is a
// call site that says something its callee no longer does -- which is what the pass boundary of
// `feat/modules/tiles` refuses a published program for.
const camel::runtime::Program *
ValueRetypeTwiceTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    if (program == nullptr) {
        return program;
    }
    // The call site: a data node holding a function value whose graph this program holds.
    const GCGraph *caller = nullptr;
    gc_node_ref_t site    = kInvalidNodeRef;
    size_t slot           = 0;
    GraphId calleeId{};
    for (const auto *graph : program->graphs()) {
        if (graph == nullptr) {
            continue;
        }
        for (gc_cnt_t i = 0; i < graph->nodeCount(); ++i) {
            const GCNode *node = graph->node(static_cast<gc_node_ref_t>(i));
            if (node == nullptr || node->kind != GCNodeKind::Data || node->dataIndex >= 0) {
                continue;
            }
            const auto *type = node->dataType();
            if (type == nullptr || type->code() != camel::core::type::TypeCode::Function) {
                continue;
            }
            const size_t index = static_cast<size_t>(-node->dataIndex);
            if (index >= graph->staticSlots().size()) {
                continue;
            }
            const auto *function = camel::core::rtdata::fromSlot<Function *>(graph->staticSlots()[index]);
            if (function == nullptr || program->graph(function->graphId()) == nullptr) {
                continue;
            }
            caller   = graph;
            site     = static_cast<gc_node_ref_t>(i);
            slot     = index;
            calleeId = function->graphId();
            break;
        }
        if (caller != nullptr) {
            break;
        }
    }
    if (caller == nullptr) {
        out << "value_retype_twice: no data node of this program holds a function whose graph it "
               "also holds, so there is no call site to retype\n"
            << std::flush;
        return program;
    }
    const auto *callee    = program->graph(calleeId);
    const auto *base      = callee->funcType();
    if (base == nullptr) {
        out << "value_retype_twice: graph '" << callee->name() << "' carries no type, so there is "
               "nothing to retype it to\n"
            << std::flush;
        return program;
    }
    // Two clauses the callee's type can carry: the body's own, and that plus a write of the console.
    // Neither is the reading under test; the reading is which one the value names after the second
    // materialization and an encode.
    sem::ResourceSignature first = bodySignature(*program, *callee);
    if (first.mode != sem::ResourceSignature::Mode::Concrete) {
        out << "value_retype_twice: graph '" << callee->name()
            << "' has no concrete body signature, so there is no clause to widen\n"
            << std::flush;
        return program;
    }
    sem::ResourceSignature second = first;
    second.ambient.push_back(sem::ResourceSignature::AmbientUse{
        .name = "retype_twice", .mode = sem::AccessMode::Write, .unordered = false});
    const bool differs = !(first == second);

    RuntimeGraphDraftSession session(context_, program);
    auto &draft = session.edit(const_cast<GCGraph *>(caller)).edit();
    camel::core::type::FunctionType *firstType  = base->withResources(first);
    camel::core::type::FunctionType *secondType = base->withResources(second);

    const gc_node_ref_t once = draft.materializeStaticValue(draft.staticSlots()[slot], firstType);
    draft.setNodeOrigin(once, draft.nodeOrigin(site));
    draft.replaceAllValueUses(site, once);
    draft.eraseNode(site);
    const size_t onceSlot = static_cast<size_t>(-draft.header(once)->dataIndex);
    const gc_node_ref_t twice =
        draft.materializeStaticValue(draft.staticSlots()[onceSlot], secondType);
    draft.setNodeOrigin(twice, draft.nodeOrigin(once));
    draft.replaceAllValueUses(once, twice);
    draft.eraseNode(once);

    out << "value_retype_twice: graph '" << caller->name() << "' names '" << callee->name()
        << "'; the two clauses differ: " << (differs ? "yes" : "no") << ", first "
        << sem::toString(first) << ", second " << sem::toString(second) << '\n'
        << std::flush;

    // Read back from the encode and, when the commit publishes it, from the program it published.
    // `prepared()` is the encode with nothing judged and nothing delivered.
    const Program *staged = session.prepared();
    const auto readBack    = [](const Program &held, const GCGraph *graph, GraphId wanted) {
        std::string said = "<no value>";
        const auto *copy    = held.graph(graph->id());
        if (copy == nullptr) {
            return std::string("<no copy>");
        }
        for (gc_cnt_t i = 0; i < copy->nodeCount(); ++i) {
            const GCNode *node = copy->node(static_cast<gc_node_ref_t>(i));
            if (node == nullptr || node->kind != GCNodeKind::Data || node->dataIndex >= 0) {
                continue;
            }
            const auto *type = node->dataType();
            if (type == nullptr || type->code() != camel::core::type::TypeCode::Function) {
                continue;
            }
            const auto *function = static_cast<const camel::core::type::FunctionType *>(type);
            said = sem::toString(function->resources());
            break;
        }
        (void)wanted;
        return said;
    };
    out << "value_retype_twice: the encode says " << readBack(*staged, caller, calleeId) << '\n'
        << std::flush;
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The declaration a graph's signature becomes when it gains one parameter of `type` at the end: the
/// second of the two statements a parameter is, which `add_param` does not write (`draft.h,
/// addParameterPort`). A `with` list and an exit type are carried over as they are -- adding a
/// parameter to a body that already computes with its others changes neither.
camel::core::type::FunctionType *withOneParameter(
    camel::core::type::FunctionType *type, camel::core::type::Type *parameter) {
    using camel::core::type::FunctionType;
    using camel::core::type::param_vec_t;
    camel::core::type::param_vec_t withTypes;
    withTypes.reserve(type->withTypesCount());
    for (size_t i = 0; i < type->withTypesCount(); ++i) {
        withTypes.emplace_back(type->withTypeAt(i), type->withIsVarAt(i));
    }
    param_vec_t normTypes;
    normTypes.reserve(type->normTypesCount() + 1);
    for (size_t i = 0; i < type->normTypesCount(); ++i) {
        normTypes.emplace_back(type->normTypeAt(i), type->normIsVarAt(i));
    }
    normTypes.emplace_back(parameter, false);
    return FunctionType::create(withTypes, normTypes, type->exitType());
}

} // namespace

const camel::runtime::Program *
AddParamTestPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    using camel::core::type::Type;
    GCGraph *const graph = program != nullptr ? program->root() : nullptr;
    if (graph == nullptr) {
        out << "add_param: the program has no root graph, so there is no graph to add a parameter "
               "to\n"
            << std::flush;
        return program;
    }
    out << "add_param: graph '" << graph->name() << "' declares "
        << (graph->funcType() != nullptr ? graph->funcType()->normTypesCount() : 0)
        << " parameter(s) and holds " << graph->normPorts().size() << " port node(s)\n"
        << std::flush;
    // The negative half: the port node of a new parameter, and nothing declaring it. The commit
    // must refuse and name the port, which is what says the declaration is the other half and not
    // an optional remark.
    {
        RuntimeGraphDraftSession session(context_, program);
        auto &draft = session.edit(graph).edit();
        gc_node_ref_t port = kInvalidNodeRef;
        {
            const GraphDraft::NamedEdit named(draft, EditKind::ParamPort);
            port = camel::runtime::addParameterPort(draft, Type::Int32());
        }
        out << "add_param: added port node " << port << " of type 'int'; the graph now holds "
            << draft.normPorts().size() << " port node(s) and declares "
            << (draft.funcType() != nullptr ? draft.funcType()->normTypesCount() : 0)
            << " parameter(s)\n"
            << std::flush;
        try {
            const Program *published = session.commit();
            out << "add_param: the commit published it, which is the defect (no parameter was "
                   "declared for port node "
                << port << ")\n"
                << std::flush;
            (void)published;
        } catch (const std::runtime_error &refused) {
            out << "add_param: refused at the commit: " << refused.what() << '\n' << std::flush;
        }
    }
    // The positive half: the same port node, and the signature rewritten to declare it. Without
    // this, "it was refused" would only say "a graph with a new port is always refused".
    {
        RuntimeGraphDraftSession session(context_, program);
        auto &draft = session.edit(graph).edit();
        gc_node_ref_t port = kInvalidNodeRef;
        {
            const GraphDraft::NamedEdit named(draft, EditKind::ParamPort);
            port = camel::runtime::addParameterPort(draft, Type::Int32());
        }
        draft.setFuncType(withOneParameter(graph->funcType(), Type::Int32()));
        out << "add_param: port node " << port << " added and the signature now declares "
            << draft.funcType()->normTypesCount() << " parameter(s)\n"
            << std::flush;
        try {
            const Program *published = session.commit();
            out << "add_param: the commit published it, and nothing was refused (declared="
                << published->root()->funcType()->normTypesCount() << ")\n"
                << std::flush;
        } catch (const std::runtime_error &refused) {
            out << "add_param: refused at the commit: " << refused.what() << '\n' << std::flush;
        }
    }
    // The run half: publishing is not running. The two halves above show a graph to the commit's
    // checks; this one has it executed. A port node no execution reaches is a parameter no caller's
    // value arrives at, and such an operator differs from one that does nothing only by the checks it
    // can fail.
    //
    // The value a parameter is given has one place to come from at run time: the caller's argument
    // list, written into the port's slot when the call frame is laid out
    // (`bindMarkedFunctionFrame`, stdlib/std/nvm/nodevm.cpp). The command line is not that place for
    // a parameter a pass adds -- `--arg` binds the entry's parameters *before* the first pass, and
    // each bound port is replaced by a constant and erased
    // (`entry_args.cpp#bindEntryArguments`), so a parameter a pass adds to the entry can never be
    // given one. So this half widens a graph `main` calls together with the call that passes its
    // values:
    //
    //   * the graph `pair` gains a third parameter of type `int` (`addParameterPort`, the operator's
    //     own edit) and a signature that declares it (`setFuncType`, the other statement);
    //   * its body's reads of its second parameter become reads of the new port, so the number it
    //     answers is the value that arrived in the new parameter's slot -- a new parameter nothing
    //     read would let a wrong slot pass, which is the one thing this reading exists to catch;
    //   * the call gains the value for it.
    //
    // `addParameterPort` *appends* the slot (it is the last one the graph lays out), so a caller's
    // third argument is the shape the appended slot has to receive.
    {
        RuntimeGraphDraftSession session(context_, program);
        const std::vector<GCGraph *> reachable = session.collectReachableRuntimeGraphs();
        GCGraph *callee                        = nullptr;
        for (GCGraph *candidate : reachable) {
            if (candidate == graph || candidate->funcType() == nullptr) {
                continue;
            }
            if (candidate->normPorts().size() != 2 || candidate->funcType()->normTypesCount() != 2) {
                continue;
            }
            callee = candidate;
            break;
        }
        // The call site: the FUNC node that names `callee`. A call is a FUNC node and its norm inputs
        // are its arguments (`calleeIdOf` answers for exactly that node, graph.h).
        GCGraph *caller    = nullptr;
        gc_node_ref_t call = kInvalidNodeRef;
        if (callee != nullptr) {
            for (GCGraph *candidate : reachable) {
                auto &transaction = session.edit(candidate);
                auto &draft       = transaction.edit();
                for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
                    if (!draft.alive(id) || draft.header(id)->kind != GCNodeKind::Func) {
                        continue;
                    }
                    if (draft.calleeGraphOf(id) != callee) {
                        continue;
                    }
                    caller = candidate;
                    call   = id;
                    break;
                }
                if (call != kInvalidNodeRef) {
                    break;
                }
            }
        }
        if (callee == nullptr || call == kInvalidNodeRef) {
            out << "add_param: run: no graph with two parameters that `main` calls is reachable, so "
                   "there is nothing to pass a value to\n"
                << std::flush;
            return program;
        }
        out << "add_param: run: graph '" << callee->name() << "' declares "
            << callee->funcType()->normTypesCount() << " parameter(s) and holds "
            << callee->normPorts().size() << " port node(s)\n"
            << std::flush;
        auto &calleeDraft  = session.edit(callee).edit();
        gc_node_ref_t port = kInvalidNodeRef;
        {
            const GraphDraft::NamedEdit named(calleeDraft, EditKind::ParamPort);
            port = camel::runtime::addParameterPort(calleeDraft, Type::Int32());
        }
        calleeDraft.setFuncType(withOneParameter(callee->funcType(), Type::Int32()));
        // The second parameter's readers become readers of the new port. `replaceAllValueUses` and not
        // `replaceNode`: the latter *erases* the node it replaces, and a parameter erased takes its
        // place in the port list with it -- the graph would declare three parameters and hold two, and
        // the caller's second argument would then be judged against the new port. Here the second
        // parameter keeps its place and only the reads of it move, which is what the value the caller
        // passes for the *new* parameter is asked to answer.
        calleeDraft.replaceAllValueUses(calleeDraft.normPorts()[1], port);
        auto &callerTransaction = session.edit(caller);
        auto &callerDraft       = callerTransaction.edit();
        std::vector<gc_node_ref_t> arguments(
            callerDraft.normInputsOf(call).begin(), callerDraft.normInputsOf(call).end());
        arguments.push_back(callerDraft.materializeStaticValue(
            camel::core::rtdata::toSlot<int32_t>(7), Type::Int32()));
        callerDraft.setNormInputs(call, arguments);
        out << "add_param: run: port node " << port << " added, the signature declares "
            << calleeDraft.funcType()->normTypesCount() << " parameter(s), the call passes "
            << callerDraft.normInputsOf(call).size() << " argument(s)\n"
            << std::flush;
        try {
            const Program *published = session.commit();
            out << "add_param: run: the commit published it and the program runs\n" << std::flush;
            return published;
        } catch (const std::runtime_error &refused) {
            out << "add_param: run: refused at the commit: " << refused.what() << '\n' << std::flush;
        }
    }
    // The program it was given: the reading is the two commits, and a root function that declares a
    // parameter nothing passes is not a program the pipeline can run at the end of this pass.
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The test-only passes this file implements. Each name is the constant its own header declares, so
/// the name a pipeline writes and the class that answers it are one fact.
const PassKernel foldFunctionTypeKernel(kFoldFunctionTypeTestPass, makePass<FoldFunctionTypeTestPass>());
const PassKernel camelCommitChecksKernel(kCamelCommitChecksTestPass, makePass<CamelCommitChecksTestPass>());
const PassKernel commitOrderKernel(kCommitOrderTestPass, makePass<CommitOrderTestPass>());
const PassKernel commitEffectKernel(kCommitEffectTestPass, makePass<CommitEffectTestPass>());
const PassKernel commitLegalSpliceKernel(kCommitLegalSpliceTestPass, makePass<CommitLegalSpliceTestPass>());
const PassKernel valueRetypeTwiceKernel(kValueRetypeTwiceTestPass, makePass<ValueRetypeTwiceTestPass>());
const PassKernel commitExitRegionKernel(kCommitExitRegionTestPass, makePass<CommitExitRegionTestPass>());
const PassKernel commitCycleKernel(kCommitCycleTestPass, makePass<CommitCycleTestPass>());
const PassKernel commitAncestorKernel(kCommitAncestorTestPass, makePass<CommitAncestorTestPass>());
const PassKernel commitTierKernel(kCommitTierTestPass, makePass<CommitTierTestPass>());
const PassKernel commitInPlaceKernel(kCommitInPlaceTestPass, makePass<CommitInPlaceTestPass>());
const PassKernel commitSiblingArmKernel(kCommitSiblingArmTestPass, makePass<CommitSiblingArmTestPass>());
const PassKernel commitLoopBodyReadKernel(kCommitLoopBodyReadTestPass, makePass<CommitLoopBodyReadTestPass>());
const PassKernel commitPortTypeKernel(kCommitPortTypeTestPass, makePass<CommitPortTypeTestPass>());
const PassKernel commitMadeGraphPortsKernel(kCommitMadeGraphPortsTestPass, makePass<CommitMadeGraphPortsTestPass>());
const PassKernel commitMadeGraphOrderKernel(kCommitMadeGraphOrderTestPass, makePass<CommitMadeGraphOrderTestPass>());
const PassKernel addParamKernel(kAddParamTestPass, makePass<AddParamTestPass>());

} // namespace
