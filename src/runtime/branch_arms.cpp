/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 17, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The branch-arm invariant of an encoded graph, checked at every commit
 * (docs/technical/17_graph_rewrite_api_contracts.md, section 4.1).
 *
 * Every back end runs a branch the same way: the execution order (ExecPlan::topo) lays out,
 * between a BRCH and its JOIN, one range per arm, the BRCH jumps to the range of the arm it
 * selects and the end of the range jumps to the JOIN. The nodes of an arm are those of its Arm
 * region. That is an execution of the graph when the regions say what runs when: a node reads
 * only nodes that run whenever it runs (those of its own region or of the regions around it;
 * the JOIN also reads the results of its arms), and every node that waits on the BRCH is in an
 * arm. A rewrite that breaks this leaves a graph that still looks well formed and silently
 * computes something else (code of an arm that is not taken runs, an effect or a value of the
 * taken arm is skipped). This file turns that into an error at the commit that produced the
 * graph.
 */

#include "runtime/graph_build.h"

#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <array>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace camel::runtime {

namespace {

std::string_view kindName(GCNodeKind kind) {
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
    case GCNodeKind::Func:
        return "FUNC";
    case GCNodeKind::Oper:
        return "OPER";
    case GCNodeKind::Sync:
        return "SYNC";
    case GCNodeKind::Gate:
        return "GATE";
    case GCNodeKind::Dref:
        return "DREF";
    case GCNodeKind::Loop:
        return "LOOP";
    default:
        return "node";
    }
}

/// "OPER 17 (:io/println)", "FUNC 9 (calls 'f')", "GATE 12".
std::string describe(const Program &program, const GCGraph *graph, gc_node_ref_t ref) {
    const GCNode *node = graph->node(ref);
    if (!node) {
        return std::format("node {}", ref);
    }
    std::string text = std::format("{} {}", kindName(node->kind), ref);
    if (node->kind == GCNodeKind::Oper) {
        text += std::format(" ({})", graph->nodeBodyAs<GCOperBody>(ref)->uri());
    } else if (node->kind == GCNodeKind::Func) {
        if (const GCGraph *callee = program.calleeOf(*graph, ref)) {
            text += std::format(" (calls '{}')", callee->name());
        }
    }
    return text;
}

[[noreturn]] void fail(const GCGraph *graph, const std::string &what) {
    throw std::runtime_error(
        std::format(
            "Runtime graph '{}' breaks the branch-arm invariant: {} (see "
            "docs/technical/17_graph_rewrite_api_contracts.md, 4.1).",
            graph->name(),
            what));
}

bool isControl(GCRegionKind kind) {
    return kind == GCRegionKind::Arm || kind == GCRegionKind::LoopCond ||
           kind == GCRegionKind::LoopBody;
}

} // namespace

void validateRuntimeBranchArms(const Program &program, const GCGraph *graph) {
    if (!graph || !graph->hasNodePayload()) {
        return;
    }
    const size_t slots = graph->nodeCount();
    std::vector<gc_node_ref_t> branches;
    for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
        if (*it && (*it)->kind == GCNodeKind::Brch) {
            branches.push_back(it.ref());
        }
    }
    if (branches.empty()) {
        return;
    }

    const auto inputsOf = [graph](gc_node_ref_t ref) {
        return std::array<std::span<const gc_node_ref_t>, 3>{
            graph->normInputsOf(ref),
            graph->withInputsOf(ref),
            graph->ctrlInputsOf(ref)};
    };

    // Only what the exit depends on is ever laid out and run. The rest (a node a rewrite left
    // without users) is not part of any execution and is ignored throughout.
    std::vector<uint8_t> live(slots, 0);
    std::vector<gc_node_ref_t> work;
    work.push_back(
        graph->exitNodeRef() != kInvalidNodeRef ? graph->exitNodeRef() : graph->returnNodeRef());
    while (!work.empty()) {
        const gc_node_ref_t ref = work.back();
        work.pop_back();
        if (ref == kInvalidNodeRef || ref >= slots || live[ref]) {
            continue;
        }
        live[ref] = 1;
        for (const auto inputs : inputsOf(ref)) {
            work.insert(work.end(), inputs.begin(), inputs.end());
        }
    }

    // The innermost control region around each region (itself when it is one).
    const uint32_t regionCount = static_cast<uint32_t>(graph->regionCount());
    std::vector<uint32_t> control(regionCount, 0);
    for (uint32_t r = 1; r < regionCount; ++r) {
        uint32_t at = r;
        while (at != 0 && at < regionCount && !isControl(graph->region(at)->kind)) {
            at = graph->region(at)->parent;
        }
        control[r] = at < regionCount && at != 0 && isControl(graph->region(at)->kind) ? at : 0;
    }
    const auto controlOf = [&](gc_node_ref_t ref) {
        const uint32_t r = graph->nodeRegion(ref);
        return r == 0 || r >= regionCount ? 0u : control[r];
    };
    const auto parentControl = [&](uint32_t c) {
        const uint32_t parent = graph->region(c)->parent;
        return parent == 0 || parent >= regionCount ? 0u : control[parent];
    };
    // `inner` is `outer`, or lies in it.
    const auto within = [&](uint32_t inner, uint32_t outer) {
        for (; inner != 0; inner = parentControl(inner)) {
            if (inner == outer) {
                return true;
            }
        }
        return outer == 0;
    };
    // The control region `c` lies in an arm of the BRCH `brch`.
    const auto inArmOf = [&](uint32_t c, gc_node_ref_t brch) {
        for (; c != 0; c = parentControl(c)) {
            const GCRegion &region = *graph->region(c);
            if (region.kind == GCRegionKind::Arm && region.owner == brch) {
                return true;
            }
        }
        return false;
    };
    // The arm of `brch` that the control region `c` lies in, -1 for none.
    const auto armOfControl = [&](uint32_t c, gc_node_ref_t brch) {
        for (; c != 0; c = parentControl(c)) {
            const GCRegion &region = *graph->region(c);
            if (region.kind == GCRegionKind::Arm && region.owner == brch) {
                return static_cast<int64_t>(region.index);
            }
        }
        return int64_t{-1};
    };

    for (const gc_node_ref_t brch : branches) {
        if (!live[brch]) {
            continue;
        }
        const auto *body         = graph->nodeBodyAs<GCBrchBody>(brch);
        const gc_node_ref_t join = body->join;
        const GCNode *joinNode   = join != kInvalidNodeRef ? graph->node(join) : nullptr;
        if (!joinNode || joinNode->kind != GCNodeKind::Join ||
            graph->normInputsOf(join).empty() || graph->normInputsOf(join).front() != brch) {
            fail(
                graph,
                std::format(
                    "BRCH {} names node {} as its JOIN, which is not a JOIN reading it",
                    brch,
                    join));
        }
        // Several JOINs over one BRCH is the export-only shape of devirtualizeBranchedCallInDraft
        // (OptimizeRewriteConfig::enableBranchedCallSplit): a value DAG for a translator, which
        // no back end executes (the execution order reports a cycle for it). Not checked.
        size_t joinsReading = 0;
        for (gc_node_ref_t user : graph->normOutputsOf(brch)) {
            const GCNode *userNode = graph->node(user);
            joinsReading += userNode && userNode->kind == GCNodeKind::Join ? 1 : 0;
        }
        if (joinsReading > 1) {
            continue;
        }
        if (!live[join]) {
            fail(
                graph,
                std::format(
                    "BRCH {} is run but its JOIN {} is not: nothing that runs depends on the "
                    "branch finishing",
                    brch,
                    join));
        }
        const size_t arms = graph->armCountOf(brch);
        std::vector<uint8_t> hasRegion(arms, 0);
        for (uint32_t r = 1; r < regionCount; ++r) {
            const GCRegion &region = *graph->region(r);
            if (region.kind == GCRegionKind::Arm && region.owner == brch && region.index < arms) {
                hasRegion[region.index] = 1;
            }
        }
        for (size_t k = 0; k < arms; ++k) {
            if (!hasRegion[k]) {
                fail(graph, std::format("arm {} of BRCH {} has no region", k, brch));
            }
        }
        // What waits on the BRCH is in one of its arms.
        for (gc_node_ref_t user : graph->ctrlOutputsOf(brch)) {
            if (!live[user] || user == join) {
                continue;
            }
            if (!inArmOf(controlOf(user), brch)) {
                fail(
                    graph,
                    std::format(
                        "{} runs between BRCH {} and its JOIN {} and belongs to no arm: it waits "
                        "on the BRCH and no arm's result depends on it",
                        describe(program, graph, user),
                        brch,
                        join));
            }
        }
    }

    // A node reads only nodes that run whenever it runs.
    for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
        const gc_node_ref_t ref = it.ref();
        if (!live[ref]) {
            continue;
        }
        const uint32_t here = controlOf(ref);
        const bool isJoin   = graph->node(ref)->kind == GCNodeKind::Join;
        const gc_node_ref_t joinOf =
            isJoin && !graph->normInputsOf(ref).empty() ? graph->normInputsOf(ref).front()
                                                         : kInvalidNodeRef;
        if (isJoin && joinOf != kInvalidNodeRef) {
            // One of the several JOINs over a BRCH (see above): a value DAG for a translator.
            size_t joinsReading = 0;
            for (gc_node_ref_t user : graph->normOutputsOf(joinOf)) {
                const GCNode *userNode = graph->node(user);
                joinsReading += userNode && userNode->kind == GCNodeKind::Join ? 1 : 0;
            }
            if (joinsReading > 1) {
                continue;
            }
        }
        for (const auto inputs : inputsOf(ref)) {
            for (gc_node_ref_t input : inputs) {
                if (input == kInvalidNodeRef || input >= slots || !live[input]) {
                    continue;
                }
                const uint32_t there = controlOf(input);
                if (there == 0 || within(here, there)) {
                    continue;
                }
                // The JOIN reads the results of its arms (and what waits inside them).
                if (isJoin && inArmOf(there, joinOf)) {
                    continue;
                }
                const GCRegion &region = *graph->region(there);
                const int64_t theirArm = region.kind == GCRegionKind::Arm
                                             ? static_cast<int64_t>(region.index)
                                             : int64_t{-1};
                const int64_t ownArm   = here != 0 && graph->region(here)->kind == GCRegionKind::Arm
                                             ? armOfControl(here, region.owner)
                                             : int64_t{-1};
                if (ownArm >= 0 && ownArm != theirArm) {
                    fail(
                        graph,
                        std::format(
                            "{} runs between BRCH {} and its JOIN {} and belongs to two arms, {} and "
                            "{}: it would run only when arm {} is taken, and arm {} reads it",
                            describe(program, graph, input),
                            region.owner,
                            graph->nodeBodyAs<GCBrchBody>(region.owner)->join,
                            theirArm,
                            ownArm,
                            theirArm,
                            ownArm));
                }
                fail(
                    graph,
                    std::format(
                        "{} reads {}, which lies in an arm of BRCH {} that may not be taken when "
                        "{} runs",
                        describe(program, graph, ref),
                        describe(program, graph, input),
                        region.owner,
                        describe(program, graph, ref)));
            }
        }
    }
}

} // namespace camel::runtime
