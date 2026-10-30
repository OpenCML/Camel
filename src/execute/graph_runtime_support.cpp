// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Shared GIR runtime execution helpers.
 *
 * The goal of this file is to consolidate execution semantics that should not
 * diverge between execution-oriented passes. It intentionally stays below pass
 * policy level and only provides graph-local mechanics.
 */

#include "camel/execute/graph_runtime_support.h"

#include "camel/core/context/context.h"
#include "camel/runtime/control_regions.h"
#include "camel/runtime/exec_plan.h"

#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/rtdata/view.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

using namespace camel::core::context;

namespace camel::execute {

void RunPrograms::begin(
    camel::core::context::Context &context, const camel::runtime::Program *main) {
    end();
    if (main != nullptr) {
        for (const camel::runtime::GCGraph *graph : main->graphs()) {
            if (!graph->executable()) {
                throw std::logic_error(
                    "Graph '" + graph->name() +
                    "' has values with several writings and cannot be run; extract first.");
            }
        }
    }
    std::lock_guard lock(mutex_);
    context_ = &context;
    main_    = main;
    others_.clear();
    context.enterRun();
    active_ = true;
    if (main != nullptr) {
        context.rootProgramForRun(main);
    }
}

void RunPrograms::end() {
    std::lock_guard lock(mutex_);
    if (context_ != nullptr) {
        if (main_ != nullptr) {
            context_->unrootProgramForRun(main_);
        }
        if (active_) {
            context_->leaveRun();
        }
    }
    active_ = false;
    others_.clear();
    main_    = nullptr;
    context_ = nullptr;
}

void RunPrograms::note(const camel::runtime::Program *program) {
    if (program == main_ || program == nullptr) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (std::find(others_.begin(), others_.end(), program) == others_.end()) {
        // Not rooted: the function value the program came with is in a frame while its graph
        // runs, and the collector of run-time values keeps what that value reaches.
        others_.push_back(program);
    }
}

namespace {

using camel::runtime::gc_node_ref_t;

std::string formatRefList(std::span<const gc_node_ref_t> refs) {
    std::string out = "[";
    for (size_t i = 0; i < refs.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += std::to_string(refs[i]);
    }
    out += "]";
    return out;
}

std::string describeNode(const camel::runtime::GCGraph *graph, gc_node_ref_t ref) {
    const auto *node = graph->node(ref);
    if (!node) {
        return std::format("{}(null)", ref);
    }
    std::string extra;
    switch (node->kind) {
    case camel::runtime::GCNodeKind::Oper:
        extra = std::format(",op={}", graph->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri());
        break;
    case camel::runtime::GCNodeKind::Func: {
        extra = std::format(",callee={}", camel::runtime::toText(graph->calleeIdOf(ref)));
    } break;
    default:
        break;
    }
    return std::format(
        "{}(k={}{} ctrl={} norm={} with={})",
        ref,
        static_cast<int>(node->kind),
        extra,
        formatRefList(graph->ctrlInputsOf(ref)),
        formatRefList(graph->normInputsOf(ref)),
        formatRefList(graph->withInputsOf(ref)));
}

} // namespace

std::vector<gc_node_ref_t> computeExecutionOrder(const camel::runtime::GCGraph *graph) {
    ASSERT(graph != nullptr, "Runtime graph is null in execution topo construction.");
    ASSERT(
        graph->hasNodePayload(),
        std::format("Runtime graph '{}' has no node payload.", graph->name()));

    std::vector<gc_node_ref_t> order;
    order.reserve(graph->nodeCount());
    std::unordered_map<gc_node_ref_t, uint8_t> state;
    state.reserve(graph->nodeCount());
    std::vector<gc_node_ref_t> stack;
    stack.reserve(graph->nodeCount());

    // Only what the exit depends on is laid out. A rewrite may leave nodes nothing uses (the
    // inner branch of an inlined callee whose result the caller's body ignores); they still
    // hang off live nodes as users. The branch handling below walks users, so it has to know
    // which of them are part of the execution.
    const gc_node_ref_t startRef = graph->exitNodeRef() != camel::runtime::kInvalidNodeRef
                                       ? graph->exitNodeRef()
                                       : graph->returnNodeRef();
    std::vector<uint8_t> live(graph->nodeCount(), 0);
    {
        std::vector<gc_node_ref_t> work{startRef};
        while (!work.empty()) {
            const gc_node_ref_t n = work.back();
            work.pop_back();
            if (!graph->containsNodeRef(n) || live[n]) {
                continue;
            }
            live[n] = 1;
            for (auto inputs :
                 {graph->ctrlInputsOf(n), graph->normInputsOf(n), graph->withInputsOf(n)}) {
                work.insert(work.end(), inputs.begin(), inputs.end());
            }
            // A sync edge carries "used" (docs/design/effects/edges.cn.md 1.5): the statement
            // written before this one in the same block is reachable exactly as a data port is.
            for (const camel::runtime::GCGraphSyncEdge &edge : graph->syncPredsOf(n)) {
                work.push_back(edge.pred);
            }
        }
    }

    // The live nodes of each arm region, nested branches included: the nodes whose control region
    // lies in a region are one range of the nodes sorted by the walk position of their control
    // region (in node order within a position).
    const camel::runtime::ControlRegions regions(*graph);
    std::vector<gc_node_ref_t> grouped;
    std::vector<uint32_t> groupStart;
    if (regions.any()) {
        groupStart.assign(regions.size() + 1, 0);
        for (gc_node_ref_t n = 0; n < live.size(); ++n) {
            if (live[n]) {
                ++groupStart[regions.tin(regions.controlOf(n)) + 1];
            }
        }
        for (size_t i = 1; i < groupStart.size(); ++i) {
            groupStart[i] += groupStart[i - 1];
        }
        grouped.resize(groupStart.back());
        std::vector<uint32_t> fill(groupStart.begin(), groupStart.end() - 1);
        for (gc_node_ref_t n = 0; n < live.size(); ++n) {
            if (live[n]) {
                grouped[fill[regions.tin(regions.controlOf(n))]++] = n;
            }
        }
    }
    const auto subtreeOf = [&](uint32_t region) {
        return std::span<const gc_node_ref_t>(grouped).subspan(
            groupStart[regions.tin(region)],
            groupStart[regions.last(region) + 1] - groupStart[regions.tin(region)]);
    };
    const auto ownOf = [&](uint32_t region) {
        return std::span<const gc_node_ref_t>(grouped).subspan(
            groupStart[regions.tin(region)],
            groupStart[regions.tin(region) + 1] - groupStart[regions.tin(region)]);
    };

    std::function<void(gc_node_ref_t)> visit = [&](gc_node_ref_t index) {
        if (!graph->containsNodeRef(index)) {
            return;
        }
        if (state[index] == 2) {
            return;
        }
        if (state[index] == 1) {
            const auto *cycleNode = graph->node(index);
            std::string stackDump;
            std::string detailDump;
            for (gc_node_ref_t ref : stack) {
                if (!stackDump.empty()) {
                    stackDump += " -> ";
                }
                stackDump += std::to_string(ref);
                if (const auto *node = graph->node(ref); node != nullptr) {
                    if (!detailDump.empty()) {
                        detailDump += " | ";
                    }
                    detailDump += describeNode(graph, ref);
                }
            }
            if (!stackDump.empty()) {
                stackDump += " -> ";
            }
            stackDump += std::to_string(index);
            throw std::runtime_error(
                std::format(
                    "Cycle detected while building runtime execution topo in graph '{}' at node "
                    "ref {}. kind={} ctrlInputs={} normInputs={} withInputs={} stack=[{}] "
                    "details=[{}].",
                    graph->name(),
                    index,
                    cycleNode ? static_cast<int>(cycleNode->kind) : -1,
                    cycleNode ? graph->ctrlInputsOf(index).size() : 0,
                    cycleNode ? graph->normInputsOf(index).size() : 0,
                    cycleNode ? graph->withInputsOf(index).size() : 0,
                    stackDump,
                    detailDump));
        }
        state[index] = 1;
        stack.push_back(index);

        // Schedulers run a taken arm as the range of its region and skip everything else between
        // the BRCH and the JOIN, so each arm is laid out as one piece, one after the other, and
        // what an arm reads from outside the branch comes before the BRCH: it runs whether the
        // arm is taken or not.
        if (const auto *node = graph->node(index);
            node && node->kind == camel::runtime::GCNodeKind::Join &&
            !graph->normInputsOf(index).empty()) {
            const gc_node_ref_t brch = graph->normInputsOf(index).front();
            const auto arms          = regions.armsOf(brch);
            for (uint32_t arm : arms) {
                if (arm == 0) {
                    continue;
                }
                for (gc_node_ref_t n : subtreeOf(arm)) {
                    for (auto inputs :
                         {graph->ctrlInputsOf(n), graph->normInputsOf(n), graph->withInputsOf(n)}) {
                        for (gc_node_ref_t input : inputs) {
                            if (input != brch && !regions.inBranch(input, brch)) {
                                visit(input);
                            }
                        }
                    }
                }
            }
            for (gc_node_ref_t input : graph->ctrlInputsOf(index)) {
                if (!regions.inBranch(input, brch)) {
                    visit(input);
                }
            }
            // The statement written directly before the branch is a statement of the block too, so
            // it is laid out before the arms (docs/design/effects/edges.cn.md 5.3).
            for (const camel::runtime::GCGraphSyncEdge &edge : graph->syncPredsOf(index)) {
                if (!regions.inBranch(edge.pred, brch)) {
                    visit(edge.pred);
                }
            }
            // A value an arm gives that is computed outside the branch is computed before it.
            const auto results = graph->withInputsOf(index);
            for (gc_node_ref_t result : results) {
                if (!regions.inBranch(result, brch)) {
                    visit(result);
                }
            }
            for (gc_node_ref_t input : graph->normInputsOf(index)) {
                visit(input);
            }
            for (size_t k = 0; k < arms.size(); ++k) {
                if (arms[k] == 0) {
                    continue;
                }
                if (k < results.size() && regions.inBranch(results[k], brch)) {
                    visit(results[k]);
                }
                // The nodes of the arm itself first, then those of the branches inside it.
                for (gc_node_ref_t n : ownOf(arms[k])) {
                    visit(n);
                }
                for (gc_node_ref_t n : subtreeOf(arms[k])) {
                    visit(n);
                }
            }
        }

        // A loop is laid out as one piece too: what its regions read from outside, the first
        // values of its state, then the nodes of the condition, then those of the body, then the
        // LOOP node, which ends a round.
        if (const auto *node = graph->node(index);
            node && node->kind == camel::runtime::GCNodeKind::Loop) {
            const auto parts = regions.loopPartsOf(index);
            const auto inLoop = [&](gc_node_ref_t input) {
                const uint32_t control = regions.controlOf(input);
                for (uint32_t part : parts) {
                    if (part != 0 && regions.within(control, part)) {
                        return true;
                    }
                }
                return false;
            };
            for (uint32_t part : parts) {
                if (part == 0) {
                    continue;
                }
                for (gc_node_ref_t n : subtreeOf(part)) {
                    for (auto inputs :
                         {graph->ctrlInputsOf(n), graph->normInputsOf(n), graph->withInputsOf(n)}) {
                        for (gc_node_ref_t input : inputs) {
                            if (!inLoop(input)) {
                                visit(input);
                            }
                        }
                    }
                }
            }
            // What a part ends with (the last write it makes) is ordered by the region.
            for (gc_node_ref_t input : graph->ctrlInputsOf(index)) {
                if (!inLoop(input)) {
                    visit(input);
                }
            }
            // The statement written directly before the loop is one of the block's statements, and
            // the loop reads it from outside: it belongs before the first round, not after it.
            for (const camel::runtime::GCGraphSyncEdge &edge : graph->syncPredsOf(index)) {
                if (!inLoop(edge.pred)) {
                    visit(edge.pred);
                }
            }
            for (gc_node_ref_t input : graph->normInputsOf(index)) {
                visit(input);
            }
            for (gc_node_ref_t input : graph->withInputsOf(index)) {
                if (!inLoop(input)) {
                    visit(input);
                }
            }
            for (uint32_t part : parts) {
                if (part == 0) {
                    continue;
                }
                for (gc_node_ref_t n : ownOf(part)) {
                    visit(n);
                }
                for (gc_node_ref_t n : subtreeOf(part)) {
                    visit(n);
                }
            }
        }

        for (gc_node_ref_t input : graph->ctrlInputsOf(index)) {
            visit(input);
        }
        for (gc_node_ref_t input : graph->normInputsOf(index)) {
            visit(input);
        }
        for (gc_node_ref_t input : graph->withInputsOf(index)) {
            visit(input);
        }
        // A sync edge is a use and an order at once (docs/design/effects/edges.cn.md 1.5): the
        // statement written before this one runs before it. It is live whenever the node is --
        // the liveness walk above follows it -- so it is visited whether or not the layout holds
        // it for another reason.
        for (const camel::runtime::GCGraphSyncEdge &edge : graph->syncPredsOf(index)) {
            visit(edge.pred);
        }
        // An order edge orders two nodes that both run; it does not make either of them run
        // (docs/design/effects/edges.cn.md 1.5 and 2). A predecessor that the layout does not
        // hold anyway is a node nothing reaches, so its order says nothing and is not visited:
        // no other edge is followed from it.
        for (const camel::runtime::GCGraphOrderEdge &edge : graph->orderPredsOf(index)) {
            if (live[edge.pred]) {
                visit(edge.pred);
            }
        }

        state[index] = 2;
        stack.pop_back();
        order.push_back(index);
    };

    // Execution reachability must start from the control-complete exit when it
    // exists. For sync/effectful graphs the returned value is often forwarded
    // from a trailing GATE whose norm input is the value and whose ctrl input
    // encodes the side-effect chain. Starting from returnNodeRef alone drops
    // that control suffix and causes schedulers to compile only the returned
    // constant while silently skipping the observable work.
    if (startRef != camel::runtime::kInvalidNodeRef) {
        visit(startRef);
    }
    return order;
}

gc_node_ref_t resolveRuntimeForwardedValueRef(camel::runtime::GCGraph *graph, gc_node_ref_t ref) {
    ASSERT(graph != nullptr, "Runtime forwarded-value resolution requires a graph.");

    gc_node_ref_t currentIndex = ref;
    while (currentIndex != camel::runtime::kInvalidNodeRef) {
        const auto *current = graph->node(currentIndex);
        ASSERT(current != nullptr, "Runtime forwarded-value chain resolved to null.");
        if (current->kind != camel::runtime::GCNodeKind::Gate) {
            return currentIndex;
        }
        const auto normInputs = graph->normInputsOf(currentIndex);
        if (!normInputs.empty()) {
            currentIndex = normInputs.back();
            continue;
        }
        const auto withInputs = graph->withInputsOf(currentIndex);
        if (!withInputs.empty()) {
            currentIndex = withInputs.back();
            continue;
        }
        return camel::runtime::kInvalidNodeRef;
    }
    return camel::runtime::kInvalidNodeRef;
}

gc_node_ref_t resolveRuntimeTailValueRef(camel::runtime::GCGraph *graph) {
    if (!graph) {
        return camel::runtime::kInvalidNodeRef;
    }
    auto current = graph->returnNodeRef();
    if (current == camel::runtime::kInvalidNodeRef) {
        current = graph->exitNodeRef();
    }
    while (current != camel::runtime::kInvalidNodeRef) {
        const auto *node = graph->node(current);
        ASSERT(node != nullptr, "Runtime tail-value lookup resolved to null.");
        if (node->kind != camel::runtime::GCNodeKind::Gate) {
            break;
        }
        const auto normInputs = graph->normInputsOf(current);
        if (!normInputs.empty()) {
            current = normInputs.back();
            continue;
        }
        // The value a GATE forwards is its data input; when it has none, what it waits on is all
        // there is to show, so the walk follows the wait instead: the sync predecessor carries it
        // (docs/design/effects/edges.cn.md 5.3).
        const auto syncInputs = graph->syncPredsOf(current);
        if (!syncInputs.empty()) {
            current = syncInputs.back().pred;
            continue;
        }
        // A legacy picture: a graph encoded before a GATE's wait was a sync predecessor carries
        // that wait as a control input and has no sync edge to follow, so the control kind is read
        // here and nowhere else. The fallback can go when no stored picture and no cached program
        // predates the sync form -- the same condition that lets the control kind leave the
        // encoder (docs/technical/gir-interchange.md records the version it arrives in).
        const auto ctrlInputs = graph->ctrlInputsOf(current);
        if (!ctrlInputs.empty()) {
            current = ctrlInputs.back();
            continue;
        }
        return camel::runtime::kInvalidNodeRef;
    }
    return current;
}

bool runtimeNodeOutputsContain(
    const camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, gc_node_ref_t targetRef) {
    if (!graph || targetRef == camel::runtime::kInvalidNodeRef) {
        return false;
    }
    auto contains = [targetRef](std::span<const gc_node_ref_t> refs) {
        return std::find(refs.begin(), refs.end(), targetRef) != refs.end();
    };
    return contains(graph->normOutputsOf(nodeRef)) || contains(graph->withOutputsOf(nodeRef)) ||
           contains(graph->ctrlOutputsOf(nodeRef));
}

bool runtimeArmEndsWithValue(
    camel::runtime::GCGraph *graph, gc_node_ref_t armTailRef, gc_node_ref_t valueRef) {
    ASSERT(graph != nullptr, "Runtime arm-tail check requires a graph.");
    if (armTailRef == valueRef) {
        return true;
    }
    const auto inputGroups = [graph](gc_node_ref_t ref) {
        return std::array<std::span<const gc_node_ref_t>, 3>{
            graph->normInputsOf(ref),
            graph->withInputsOf(ref),
            graph->ctrlInputsOf(ref)};
    };
    // Whether `ref` runs after the value: the value is among its transitive inputs.
    std::unordered_map<gc_node_ref_t, bool> afterValue;
    const auto runsAfterValue = [&](gc_node_ref_t ref) {
        if (auto it = afterValue.find(ref); it != afterValue.end()) {
            return it->second;
        }
        std::unordered_set<gc_node_ref_t> seen{ref};
        std::vector<gc_node_ref_t> work{ref};
        bool found = false;
        while (!work.empty() && !found) {
            const gc_node_ref_t current = work.back();
            work.pop_back();
            for (const auto inputs : inputGroups(current)) {
                for (gc_node_ref_t input : inputs) {
                    if (input == valueRef) {
                        found = true;
                    } else if (seen.insert(input).second) {
                        work.push_back(input);
                    }
                }
            }
        }
        afterValue.emplace(ref, found);
        return found;
    };

    std::unordered_set<gc_node_ref_t> seen{armTailRef};
    std::vector<gc_node_ref_t> work{armTailRef};
    while (!work.empty()) {
        const gc_node_ref_t current = work.back();
        work.pop_back();
        const auto *node = graph->node(current);
        ASSERT(node != nullptr, "Runtime arm-tail check resolved to null.");
        if (node->kind != camel::runtime::GCNodeKind::Gate) {
            if (runsAfterValue(current)) {
                return false; // something other than forwarding happens after the value
            }
            continue;
        }
        for (const auto inputs : inputGroups(current)) {
            for (gc_node_ref_t input : inputs) {
                if (input != valueRef && seen.insert(input).second) {
                    work.push_back(input);
                }
            }
        }
    }
    return true;
}

std::vector<gc_node_ref_t>
collectRuntimeTailJoinArmValues(camel::runtime::GCGraph *graph, gc_node_ref_t tailJoinRef) {
    ASSERT(graph != nullptr, "Runtime tail-join collection requires a graph.");
    std::vector<gc_node_ref_t> values;
    std::vector<gc_node_ref_t> joins{tailJoinRef};
    const camel::runtime::ControlRegions regions(*graph);
    while (!joins.empty()) {
        const gc_node_ref_t joinRef = joins.back();
        joins.pop_back();
        const auto branch = graph->normInputsOf(joinRef);
        for (gc_node_ref_t armRef : graph->withInputsOf(joinRef)) {
            const gc_node_ref_t value = resolveRuntimeForwardedValueRef(graph, armRef);
            const auto *node = value != camel::runtime::kInvalidNodeRef ? graph->node(value) : nullptr;
            if (!node || std::find(values.begin(), values.end(), value) != values.end()) {
                continue;
            }
            // A value computed outside the branch is not the arm's: a call there runs whichever
            // arm is taken.
            if (branch.empty() || !regions.inBranch(value, branch.front())) {
                continue;
            }
            // The arm does something after producing the value (an effect the forwarding
            // GATEs wait on): a call there is not a tail call, and a JOIN there is not in
            // tail position.
            if (!runtimeArmEndsWithValue(graph, armRef, value)) {
                continue;
            }
            values.push_back(value);
            if (node->kind == camel::runtime::GCNodeKind::Join) {
                joins.push_back(value);
            }
        }
    }
    return values;
}

bool hasOnlyTrivialRuntimeTailSuffixAfter(
    const camel::runtime::GCGraph *graph, std::span<const gc_node_ref_t> topoOrder,
    size_t anchorIndex) {
    ASSERT(graph != nullptr, "Runtime tail-suffix check requires a graph.");
    for (size_t i = anchorIndex + 1; i < topoOrder.size(); ++i) {
        const auto *node = graph->node(topoOrder[i]);
        ASSERT(node != nullptr, "Runtime tail-suffix lookup resolved to null.");
        if (node->kind != camel::runtime::GCNodeKind::Gate) {
            return false;
        }
    }
    return true;
}

RuntimeBranchArmRegion collectRuntimeBranchArmRegion(
    camel::runtime::GCGraph *graph, gc_node_ref_t brchIndex, size_t armIndex) {
    ASSERT(graph != nullptr, "Runtime branch region collection requires a graph.");
    ASSERT(graph->node(brchIndex) != nullptr, "Runtime BRCH record is missing.");
    ASSERT(
        graph->node(brchIndex)->kind == camel::runtime::GCNodeKind::Brch,
        "Runtime branch region collection requires a BRCH node.");

    const camel::runtime::ControlRegions regions(*graph);
    const auto arms = regions.armsOf(brchIndex);
    ASSERT(
        armIndex < arms.size() && arms[armIndex] != 0,
        std::format(
            "Graph '{}': arm {} of branch {} has no region.",
            graph->name(),
            armIndex,
            brchIndex));
    const uint32_t armRegion = arms[armIndex];
    const auto *brchBody     = graph->nodeBodyAs<camel::runtime::GCBrchBody>(brchIndex);
    ASSERT(brchBody != nullptr, "Runtime BRCH body is missing.");

    std::vector<uint8_t> member(graph->nodeCount(), 0);
    for (gc_node_ref_t n = 0; n < graph->nodeCount(); ++n) {
        if (!graph->containsNodeRef(n)) {
            continue;
        }
        member[n] = regions.within(regions.controlOf(n), armRegion);
    }
    const auto joinInputs = graph->withInputsOf(brchBody->join);
    RuntimeBranchArmRegion arm{
        .tailIndex = armIndex < joinInputs.size() ? joinInputs[armIndex]
                                                  : graph->region(armRegion)->result,
        .joinIndex = brchBody->join,
        .topoIndices = {},
    };
    // In the order the nodes depend on one another: from the tail, then the nodes nothing in the
    // arm depends on (effects).
    std::vector<uint8_t> visited(graph->nodeCount(), 0);
    std::function<void(gc_node_ref_t)> visit = [&](gc_node_ref_t n) {
        if (n >= member.size() || !member[n] || visited[n]) {
            return;
        }
        visited[n] = 1;
        for (auto inputs :
             {graph->ctrlInputsOf(n), graph->normInputsOf(n), graph->withInputsOf(n)}) {
            for (gc_node_ref_t input : inputs) {
                visit(input);
            }
        }
        arm.topoIndices.push_back(n);
    };
    visit(arm.tailIndex);
    for (gc_node_ref_t n = 0; n < member.size(); ++n) {
        visit(n);
    }
    return arm;
}

BranchArmEntries::BranchArmEntries(
    const camel::runtime::GCGraph &graph, std::span<const gc_node_ref_t> topoOrder) {
    const camel::runtime::ControlRegions regions(graph);
    if (!regions.any()) {
        return;
    }
    // The first node of each arm region in the order of the plan, whatever it does: an
    // interpreter resumes the arm at this position and runs the events of the loops that start
    // there before the node, so the first round of a loop can begin at a position whose node is
    // only a port or a constant that does nothing by itself (a loop in an arm). The nodes of
    // the regions inside an arm are its nodes too, so what is found for a region is handed to the
    // region around it. A node of an arm laid out before its BRCH was needed by code outside the
    // branch and runs whether the arm is taken or not: it is no part of the range the BRCH jumps
    // over.
    constexpr size_t kNone = std::numeric_limits<size_t>::max();
    std::unordered_map<gc_node_ref_t, size_t> position;
    position.reserve(topoOrder.size());
    for (size_t i = 0; i < topoOrder.size(); ++i) {
        position.emplace(topoOrder[i], i);
    }
    std::vector<size_t> first(regions.size(), kNone), last(regions.size(), kNone);
    for (size_t i = 0; i < topoOrder.size(); ++i) {
        const gc_node_ref_t ref = topoOrder[i];
        const uint32_t control  = regions.controlOf(ref);
        if (control == 0) {
            continue;
        }
        const camel::runtime::GCRegion &region = *graph.region(control);
        if (region.kind == camel::runtime::GCRegionKind::Arm) {
            const auto owner = position.find(region.owner);
            if (owner == position.end() || owner->second >= i) {
                continue;
            }
        }
        const uint32_t at = regions.tin(control);
        if (first[at] == kNone) {
            first[at] = i;
        }
        last[at] = i;
    }
    for (size_t at = regions.size(); at-- > 1;) {
        const uint32_t parent = regions.parentControl(regions.regionAt(static_cast<uint32_t>(at)));
        if (parent == 0) {
            continue;
        }
        const size_t into = regions.tin(parent);
        if (first[at] != kNone && (first[into] == kNone || first[at] < first[into])) {
            first[into] = first[at];
        }
        if (last[at] != kNone && (last[into] == kNone || last[at] > last[into])) {
            last[into] = last[at];
        }
    }
    for (gc_node_ref_t ref : topoOrder) {
        if (graph.node(ref)->kind != camel::runtime::GCNodeKind::Brch) {
            continue;
        }
        const auto *body = graph.nodeBodyAs<camel::runtime::GCBrchBody>(ref);
        const auto arms  = regions.armsOf(ref);
        ASSERT(
            arms.size() >= graph.armCountOf(ref),
            std::format("Graph '{}': branch {} has arms without a region.", graph.name(), ref));
        auto &entries = entries_[ref];
        auto &tails   = tails_[ref];
        for (size_t k = 0; k < graph.armCountOf(ref); ++k) {
            const size_t start = arms[k] != 0 ? first[regions.tin(arms[k])] : kNone;
            const size_t end   = arms[k] != 0 ? last[regions.tin(arms[k])] : kNone;
            entries.push_back(start != kNone ? topoOrder[start] : body->join);
            tails.push_back(end != kNone ? topoOrder[end] : body->join);
        }
    }
}

namespace {

/// The arm a BRCH takes: `norm` and `with` are its inputs, `slotOf` gives the data slot of a node.
template <typename SlotOf>
size_t selectBranchArm(
    std::span<const gc_node_ref_t> norm, std::span<const gc_node_ref_t> with, SlotOf slotOf,
    Frame *frame) {
    ASSERT(frame != nullptr, "Runtime branch selection requires a frame.");
    ASSERT(norm.size() == 1, "Runtime BRCH must have exactly one norm input.");

    const auto condIndex = slotOf(norm.front());
    if (with.empty()) {
        return frame->get<bool>(condIndex) ? 0 : 1;
    }

    const camel::core::type::TypeCode condType = frame->codeAt(condIndex);
    if (camel::core::type::isGCTraced(condType)) {
        camel::core::type::Type *condTypePtr = frame->typeAt<camel::core::type::Type>(condIndex);
        camel::core::rtdata::Object *condData =
            frame->get<camel::core::rtdata::Object *>(condIndex);
        for (size_t i = 0; i < with.size(); ++i) {
            camel::core::rtdata::Object *caseData =
                frame->get<camel::core::rtdata::Object *>(slotOf(with[i]));
            if (condData->equals(caseData, condTypePtr, false)) {
                return i;
            }
        }
        return with.size();
    }

    const slot_t condData = frame->get<slot_t>(condIndex);
    for (size_t i = 0; i < with.size(); ++i) {
        if (condData == frame->get<slot_t>(slotOf(with[i]))) {
            return i;
        }
    }
    return with.size();
}

} // namespace

size_t selectRuntimeBranchArm(camel::runtime::GCGraph *graph, gc_node_ref_t brchRef, Frame *frame) {
    ASSERT(graph != nullptr, "Runtime branch selection requires a graph.");
    return selectBranchArm(
        graph->normInputsOf(brchRef),
        graph->withInputsOf(brchRef),
        [&](gc_node_ref_t ref) {
            const auto *node = graph->node(ref);
            ASSERT(node != nullptr, "Runtime BRCH operand node is missing.");
            return node->dataIndex;
        },
        frame);
}

size_t selectRuntimeBranchArm(
    const camel::runtime::ExecPlan &plan, const camel::runtime::ExecNode &brch, Frame *frame) {
    return selectBranchArm(
        brch.normInputs(),
        brch.withInputs(),
        [&](gc_node_ref_t ref) { return plan.dataIndexByRef[ref]; },
        frame);
}

slot_t readRuntimeGraphReturn(camel::runtime::GCGraph *graph, Frame *frame) {
    ASSERT(graph != nullptr, "Runtime graph return read requires a graph.");
    ASSERT(frame != nullptr, "Runtime graph return read requires a frame.");

    if (const auto *funcType = graph->funcType();
        funcType == nullptr || !funcType->hasExitType() ||
        funcType->exitType() == camel::core::type::Type::Void()) {
        return NullSlot;
    }

    const auto returnKind    = graph->returnKind();
    const auto returnNodeRef = graph->returnNodeRef();
    const auto *returnNode   = graph->returnNode();
    if (returnKind == camel::runtime::GCReturnKind::None ||
        returnNodeRef == camel::runtime::kInvalidNodeRef || returnNode == nullptr) {
        return NullSlot;
    }

    if (returnKind == camel::runtime::GCReturnKind::Self) {
        ASSERT(returnNode->dataIndex != 0, "Runtime graph return slot resolved to slot 0.");
        return frame->get<slot_t>(returnNode->dataIndex);
    }

    gc_node_ref_t currentIndex = resolveRuntimeForwardedValueRef(graph, returnNodeRef);
    if (currentIndex == camel::runtime::kInvalidNodeRef) {
        return NullSlot;
    }

    const auto *current = graph->node(currentIndex);
    ASSERT(current != nullptr, "Runtime graph return forwarding chain resolved to null.");
    ASSERT(current->dataIndex != 0, "Runtime graph return forwarding chain resolved to slot 0.");
    return frame->get<slot_t>(current->dataIndex);
}

void writeRuntimeFillSlots(
    camel::core::rtdata::Object *target, camel::core::type::Type *targetType,
    const camel::runtime::GCFillBody *fillBody, std::span<const slot_t> values) {
    ASSERT(target != nullptr, "Runtime FILL target object is null.");
    ASSERT(targetType != nullptr, "Runtime FILL target type is null.");
    ASSERT(fillBody != nullptr, "Runtime FILL payload is missing.");

    const auto slots = fillBody->slots();
    ASSERT(
        slots.size() == values.size(),
        "Runtime FILL slot mapping must match the number of with-input values.");

    switch (targetType->code()) {
    case camel::core::type::TypeCode::Tuple: {
        auto *tuple     = tt::as_ptr<Tuple>(target);
        auto *tupleType = tt::as_ptr<camel::core::type::TupleType>(targetType);
        for (size_t i = 0; i < values.size(); ++i) {
            CAMEL_ASSERT_NOT_VIEW(
                values[i], tupleType->typeAt(slots[i]), "a FILL into a tuple");
            tuple->set<slot_t>(slots[i], values[i], tupleType);
        }
    } break;
    case camel::core::type::TypeCode::Array: {
        auto *array     = tt::as_ptr<Array>(target);
        auto *arrayType = tt::as_ptr<camel::core::type::ArrayType>(targetType);
        for (size_t i = 0; i < values.size(); ++i) {
            CAMEL_ASSERT_NOT_VIEW(
                values[i], arrayType->elemType(), "a FILL into an array");
            array->set<slot_t>(slots[i], values[i], arrayType);
        }
    } break;
    case camel::core::type::TypeCode::Struct: {
        auto *str        = tt::as_ptr<Struct>(target);
        auto *structType = tt::as_ptr<camel::core::type::StructType>(targetType);
        for (size_t i = 0; i < values.size(); ++i) {
            CAMEL_ASSERT_NOT_VIEW(
                values[i], structType->typeAt(slots[i]), "a FILL into a struct");
            str->set<slot_t>(slots[i], values[i], structType);
        }
    } break;
    case camel::core::type::TypeCode::Function: {
        auto *func         = tt::as_ptr<Function>(target);
        Tuple *closureData = func->tuple();
        ASSERT(closureData != nullptr, "Runtime FILL function closure storage is null.");
        const auto *tupleType = func->tupleType();
        for (size_t i = 0; i < values.size(); ++i) {
            // A closure capture is a stored value: a view must have been
            // materialised before it is captured.
            CAMEL_ASSERT_NOT_VIEW(
                values[i], tupleType->typeAt(slots[i]), "a closure capture");
            closureData->set<slot_t>(slots[i], values[i], tupleType);
        }
    } break;
    default:
        ASSERT(false, "Runtime FILL target type is not a supported GC container.");
    }
}

slot_t runtimeFuncValue(
    camel::core::type::Type *valueType, camel::runtime::GCGraph *calleeGraph, uint32_t written,
    std::span<const slot_t> withValues, slot_t result) {
    if (written == 0) {
        // A call that writes no captured variable hands on the callee's return value alone.
        return result;
    }
    // The versions are the call's With inputs at the callee's capture ports: the declared `with`
    // arguments come first, then the captures in the order the callee made them.
    camel::core::type::FunctionType *calleeType = calleeGraph->funcType();
    const size_t declaredWith =
        calleeType != nullptr ? calleeType->withTypesCount() : size_t{0};
    const bool hasExit =
        calleeType != nullptr && calleeType->hasExitType() &&
        calleeType->exitType() != nullptr &&
        calleeType->exitType()->code() != camel::core::type::TypeCode::Void;
    std::vector<slot_t> parts;
    if (hasExit) {
        parts.push_back(result);
    }
    for (size_t k = 0; k < 32; ++k) {
        if ((written & (uint32_t{1} << k)) == 0) {
            continue;
        }
        const size_t at = declaredWith + k;
        // A graph whose call carries a version it does not pass is a defect of the compiler or of
        // a pass, and it is reported here: reading past the end of the `with` arguments would put
        // a slot that is not a value into the tuple the call hands on (design principles 5.1, and
        // a bad graph never crashes the process).
        if (at >= withValues.size()) {
            throw std::runtime_error(
                std::format(
                    "Runtime graph '{}' writes captured variable {} but its call passes only {} "
                    "`with` arguments and declares {} of them: the call's value states a version "
                    "it is not given.",
                    calleeGraph->name(),
                    k,
                    withValues.size(),
                    declaredWith));
        }
        parts.push_back(withValues[at]);
    }
    if (parts.size() <= 1) {
        // A `void` callee writing one variable: the value is that version itself.
        return parts.empty() ? result : parts.front();
    }
    // More than one part: the value is the tuple the builder typed the call node with, in the
    // order the parts were just gathered (the return value first, then the written versions).
    auto *tupleType =
        valueType != nullptr ? tt::as_ptr<camel::core::type::TupleType>(valueType) : nullptr;
    if (tupleType == nullptr || tupleType->size() != parts.size()) {
        throw std::runtime_error(
            std::format(
                "Runtime graph '{}' hands on {} parts but its call's value type states {}: the "
                "versions a call of it produces do not match the type it was given.",
                calleeGraph->name(),
                parts.size(),
                tupleType != nullptr ? tupleType->size() : size_t{0}));
    }
    auto *tuple = Tuple::create(parts.size(), camel::core::mm::autoSpace());
    for (size_t i = 0; i < parts.size(); ++i) {
        CAMEL_ASSERT_NOT_VIEW(parts[i], tupleType->typeAt(i), "the value of a call");
        tuple->set<slot_t>(i, parts[i], tupleType);
    }
    return camel::core::rtdata::toSlot(tuple);
}

void fillFrameForDirectInvoke(
    Frame *from, Frame *dest, camel::runtime::GCGraph *callerGraph, gc_node_ref_t callNodeIndex) {
    if (from == nullptr || dest == nullptr) {
        throw std::runtime_error("Frame pointer is null in runtime direct invoke binding.");
    }
    if (callerGraph == nullptr) {
        throw std::runtime_error("Caller runtime graph is null in runtime direct invoke binding.");
    }
    auto *targetGraph = dest->graph();
    if (targetGraph == nullptr) {
        throw std::runtime_error(
            "Destination frame is missing runtime graph in runtime direct invoke binding.");
    }
    if (callerGraph->node(callNodeIndex) == nullptr) {
        throw std::runtime_error("Direct invoke call node record is null.");
    }

    const auto callerNormInputs = callerGraph->normInputsOf(callNodeIndex);
    const auto targetNormPorts  = targetGraph->normPorts();
    CAMEL_LOG_INFO_S(
        "GraphExec",
        "Direct invoke bind: caller='{}' node={} target='{}' callerNorm={} callerWith={} "
        "targetNorm={} targetWith={} targetClosure={}",
        callerGraph->name(),
        callNodeIndex,
        targetGraph->name(),
        callerNormInputs.size(),
        callerGraph->withInputsOf(callNodeIndex).size(),
        targetNormPorts.size(),
        targetGraph->withPorts().size(),
        targetGraph->closureNodes().size());
    if (callerNormInputs.size() != targetNormPorts.size()) {
        throw std::runtime_error(
            std::format(
                "Runtime direct invoke norm-arity mismatch: caller graph '{}' node {} has {} args, "
                "target graph '{}' has {} norm ports.",
                callerGraph->name(),
                callNodeIndex,
                callerNormInputs.size(),
                targetGraph->name(),
                targetNormPorts.size()));
    }
    const auto callerWithInputs = callerGraph->withInputsOf(callNodeIndex);
    const auto targetWithPorts  = targetGraph->withPorts();
    if (callerWithInputs.size() != targetWithPorts.size()) {
        throw std::runtime_error(
            std::format(
                "Runtime direct invoke with-arity mismatch: caller graph '{}' node {} has {} args, "
                "target graph '{}' has {} with ports.",
                callerGraph->name(),
                callNodeIndex,
                callerWithInputs.size(),
                targetGraph->name(),
                targetWithPorts.size()));
    }

    // All arguments are read before the first parameter is written. A self tail call refills
    // the frame it runs in (from == dest), and an argument may be a parameter of that frame, or
    // a GATE forwarding one, which shares the parameter's slot: `r(a, b) = ... r(0, a)` binds
    // b to the old a. Writing in argument order stored 0 into a first and then read it for b.
    static constexpr size_t kInlineArgs = 16;
    const size_t argCount               = callerNormInputs.size() + callerWithInputs.size();
    using Staged = std::pair<decltype(camel::runtime::GCNode::dataIndex), slot_t>;
    std::array<Staged, kInlineArgs> inlineStaged;
    std::vector<Staged> heapStaged;
    Staged *staged = inlineStaged.data();
    if (argCount > kInlineArgs) {
        heapStaged.resize(argCount);
        staged = heapStaged.data();
    }
    size_t stagedCount = 0;
    const auto stage   = [&](std::span<const gc_node_ref_t> args,
                           std::span<const gc_node_ref_t> ports,
                           const char *what) {
        for (size_t i = 0; i < args.size(); ++i) {
            const auto *argRecord  = callerGraph->node(args[i]);
            const auto *portRecord = targetGraph->node(ports[i]);
            if (argRecord == nullptr || portRecord == nullptr) {
                throw std::runtime_error(what);
            }
            staged[stagedCount++] = {
                portRecord->dataIndex,
                from->get<slot_t>(argRecord->dataIndex)};
        }
    };
    stage(
        callerNormInputs,
        targetNormPorts,
        "Runtime direct invoke norm argument/port record is null.");
    stage(
        callerWithInputs,
        targetWithPorts,
        "Runtime direct invoke with argument/port record is null.");
    for (size_t i = 0; i < stagedCount; ++i) {
        dest->set(staged[i].first, staged[i].second);
    }
}

void fillFrameForIndirectCall(
    Frame *from, Frame *dest, camel::runtime::GCGraph *callerGraph, gc_node_ref_t callNodeIndex) {
    if (from == nullptr || dest == nullptr) {
        throw std::runtime_error("Frame pointer is null in runtime indirect call binding.");
    }
    if (callerGraph == nullptr) {
        throw std::runtime_error("Caller runtime graph is null in runtime indirect call binding.");
    }
    auto *targetGraph = dest->graph();
    if (targetGraph == nullptr) {
        throw std::runtime_error(
            "Destination frame is missing runtime graph in runtime indirect call binding.");
    }
    if (callerGraph->node(callNodeIndex) == nullptr) {
        throw std::runtime_error("Indirect call node record is null.");
    }

    const auto callerNormInputs = callerGraph->normInputsOf(callNodeIndex);
    const auto targetNormPorts  = targetGraph->normPorts();
    if (callerNormInputs.size() != targetNormPorts.size()) {
        throw std::runtime_error(
            std::format(
                "Runtime indirect call norm-arity mismatch: caller graph '{}' node {} has {} args, "
                "target graph '{}' has {} norm ports.",
                callerGraph->name(),
                callNodeIndex,
                callerNormInputs.size(),
                targetGraph->name(),
                targetNormPorts.size()));
    }
    for (size_t i = 0; i < callerNormInputs.size(); ++i) {
        const auto *argRecord  = callerGraph->node(callerNormInputs[i]);
        const auto *portRecord = targetGraph->node(targetNormPorts[i]);
        if (argRecord == nullptr || portRecord == nullptr) {
            throw std::runtime_error("Runtime indirect call norm argument/port record is null.");
        }
        dest->set(portRecord->dataIndex, from->get<slot_t>(argRecord->dataIndex));
    }

    const auto callerWithInputs = callerGraph->withInputsOf(callNodeIndex);
    if (callerWithInputs.empty()) {
        throw std::runtime_error(
            "CALL node must have function input in runtime indirect call binding.");
    }
    const auto *calleeRecord = callerGraph->node(callerWithInputs.front());
    if (calleeRecord == nullptr) {
        throw std::runtime_error("Indirect call callee record is null.");
    }
    Function *func = from->get<Function *>(calleeRecord->dataIndex);
    if (func == nullptr) {
        throw std::runtime_error("Indirect call callee slot resolved to null Function.");
    }
    CAMEL_LOG_INFO_S(
        "GraphExec",
        "Indirect call bind: caller='{}' node={} target='{}' callerNorm={} callerWith={} "
        "targetNorm={} targetWith={} targetClosure={}",
        callerGraph->name(),
        callNodeIndex,
        targetGraph->name(),
        callerNormInputs.size(),
        callerWithInputs.size(),
        targetNormPorts.size(),
        targetGraph->withPorts().size(),
        targetGraph->closureNodes().size());

    // Indirect calls reserve with-input slot 0 for the callee function object itself.
    // Any remaining caller with-inputs are explicit with-arguments and must be copied
    // into the target graph's with-ports before closure tuple materialization.
    const auto targetWithPorts        = targetGraph->withPorts();
    const size_t explicitWithArgCount = callerWithInputs.empty() ? 0 : callerWithInputs.size() - 1;
    if (explicitWithArgCount != targetWithPorts.size()) {
        throw std::runtime_error(
            std::format(
                "Runtime indirect call with-arity mismatch: caller graph '{}' node {} has {} "
                "explicit with args, target graph '{}' has {} with ports.",
                callerGraph->name(),
                callNodeIndex,
                explicitWithArgCount,
                targetGraph->name(),
                targetWithPorts.size()));
    }
    for (size_t i = 0; i < targetWithPorts.size(); ++i) {
        const auto *argRecord  = callerGraph->node(callerWithInputs[i + 1]);
        const auto *portRecord = targetGraph->node(targetWithPorts[i]);
        if (argRecord == nullptr || portRecord == nullptr) {
            throw std::runtime_error("Runtime indirect call with argument/port record is null.");
        }
        dest->set(portRecord->dataIndex, from->get<slot_t>(argRecord->dataIndex));
    }

    Tuple *closure                = func ? func->tuple() : nullptr;
    const auto targetClosureNodes = targetGraph->closureNodes();
    if (targetClosureNodes.empty()) {
        return;
    }
    if (closure == nullptr) {
        throw std::runtime_error("Indirect call closure tuple is null.");
    }
    if (closure->size() != targetClosureNodes.size()) {
        throw std::runtime_error(
            std::format(
                "Runtime indirect call closure-arity mismatch: target graph '{}' expects {}, "
                "callee closure provides {}.",
                targetGraph->name(),
                targetClosureNodes.size(),
                closure->size()));
    }
    for (size_t i = 0; i < targetClosureNodes.size(); ++i) {
        const auto *closureRecord = targetGraph->node(targetClosureNodes[i]);
        if (closureRecord == nullptr) {
            throw std::runtime_error("Runtime indirect call closure node record is null.");
        }
        dest->set(closureRecord->dataIndex, closure->get<slot_t>(i));
    }
}

} // namespace camel::execute
