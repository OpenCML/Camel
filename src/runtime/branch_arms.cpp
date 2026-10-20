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
 * Created: Oct. 12, 2026
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The branch-arm invariant of an encoded graph, checked at every commit
 * (docs/technical/17_graph_rewrite_api_contracts.md, section 4.1).
 *
 * Every back end runs a branch the same way: the execution order
 * (buildReachableExecutionTopoIndices) lays out, between a BRCH and its JOIN, one range per arm,
 * each ending at the arm's tail; the BRCH jumps to the range of the arm it selects and the tail
 * jumps to the JOIN. That is only an execution of the graph when the nodes that run because of
 * the BRCH split into arms: each of them feeds exactly one tail. A rewrite that breaks this
 * leaves a graph that still looks well formed and silently computes something else (code of an
 * arm that is not taken runs, an effect or a value of the taken arm is skipped, a range is
 * entered twice). This file turns that into an error at the commit that produced the graph.
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
    const auto outputsOf = [graph](gc_node_ref_t ref) {
        return std::array<std::span<const gc_node_ref_t>, 3>{
            graph->normOutputsOf(ref),
            graph->withOutputsOf(ref),
            graph->ctrlOutputsOf(ref)};
    };

    // Only what the exit depends on is ever laid out and run. The rest (a node a rewrite left
    // without users, kept because a branch still names it as an arm's head) is not part of any
    // execution and is ignored throughout.
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

    constexpr int32_t kNoArm = -1;
    // inside[n] == stamp: n is in the interior of the branch being checked.
    std::vector<uint32_t> inside(slots, 0), walked(slots, 0);
    std::vector<int32_t> owner(slots, kNoArm);
    std::vector<gc_node_ref_t> interior;
    uint32_t stamp = 0, walk = 0;

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
                    "BRCH {} is executed but its JOIN {} is not: nothing ends its arms",
                    brch,
                    join));
        }

        const auto arms  = graph->branchArmsOf(brch);
        const auto tails = graph->withInputsOf(join);
        if (arms.size() != tails.size()) {
            fail(
                graph,
                std::format(
                    "BRCH {} has {} arms and its JOIN {} reads {} arm results",
                    brch,
                    arms.size(),
                    join,
                    tails.size()));
        }

        // The interior: what runs because of the BRCH, i.e. what is reached from it along edges
        // of any kind without passing the JOIN.
        ++stamp;
        interior.clear();
        for (const auto outputs : outputsOf(brch)) {
            work.insert(work.end(), outputs.begin(), outputs.end());
        }
        while (!work.empty()) {
            const gc_node_ref_t ref = work.back();
            work.pop_back();
            if (ref == join || ref == brch || ref >= slots || !live[ref] || inside[ref] == stamp) {
                continue;
            }
            inside[ref] = stamp;
            owner[ref]  = kNoArm;
            interior.push_back(ref);
            for (const auto outputs : outputsOf(ref)) {
                work.insert(work.end(), outputs.begin(), outputs.end());
            }
        }
        const auto isInside = [&](gc_node_ref_t ref) {
            return ref < slots && inside[ref] == stamp;
        };

        // An arm is what its tail depends on inside the interior. Every interior node must be
        // in exactly one arm.
        for (size_t arm = 0; arm < tails.size(); ++arm) {
            if (arms[arm].tail != tails[arm]) {
                fail(
                    graph,
                    std::format(
                        "arm {} of BRCH {} is recorded to end at node {}, and JOIN {} reads its "
                        "result from node {}",
                        arm,
                        brch,
                        arms[arm].tail,
                        join,
                        tails[arm]));
            }
            if (!isInside(tails[arm])) {
                continue; // a value from outside the branch: the arm has no code
            }
            work.push_back(tails[arm]);
            while (!work.empty()) {
                const gc_node_ref_t ref = work.back();
                work.pop_back();
                if (owner[ref] == static_cast<int32_t>(arm)) {
                    continue;
                }
                if (owner[ref] != kNoArm) {
                    fail(
                        graph,
                        std::format(
                            "{} runs between BRCH {} and its JOIN {} and belongs to two arms, {} "
                            "and {}: it would run only when arm {} is taken, and arm {} reads it",
                            describe(program, graph, ref),
                            brch,
                            join,
                            owner[ref],
                            arm,
                            owner[ref],
                            arm));
                }
                owner[ref] = static_cast<int32_t>(arm);
                for (const auto inputs : inputsOf(ref)) {
                    for (gc_node_ref_t input : inputs) {
                        if (isInside(input)) {
                            work.push_back(input);
                        }
                    }
                }
            }
        }
        for (const gc_node_ref_t ref : interior) {
            if (owner[ref] == kNoArm) {
                fail(
                    graph,
                    std::format(
                        "{} runs between BRCH {} and its JOIN {} and belongs to no arm (no arm's "
                        "result depends on it): no back end would run it",
                        describe(program, graph, ref),
                        brch,
                        join));
            }
            // A branch inside an arm lies in it as a whole.
            const GCNode *node = graph->node(ref);
            if (node->kind == GCNodeKind::Join && !graph->normInputsOf(ref).empty() &&
                !isInside(graph->normInputsOf(ref).front())) {
                fail(
                    graph,
                    std::format(
                        "JOIN {} lies in arm {} of BRCH {} and its BRCH {} lies outside that "
                        "branch: the two branches cross",
                        ref,
                        owner[ref],
                        brch,
                        graph->normInputsOf(ref).front()));
            }
        }

        // The recorded head of an arm is where the back ends start looking for the arm's code
        // (resolveRuntimeBranchArmEntry): what follows it inside the branch must be that arm's.
        for (size_t arm = 0; arm < arms.size(); ++arm) {
            const gc_node_ref_t head = arms[arm].head;
            if (head == kInvalidNodeRef || head >= slots || !live[head]) {
                continue;
            }
            ++walk;
            work.push_back(head);
            while (!work.empty()) {
                const gc_node_ref_t ref = work.back();
                work.pop_back();
                if (ref == join || ref == brch || ref >= slots || !live[ref] ||
                    walked[ref] == walk) {
                    continue;
                }
                walked[ref] = walk;
                if (isInside(ref)) {
                    if (owner[ref] != static_cast<int32_t>(arm)) {
                        fail(
                            graph,
                            std::format(
                                "{} follows the head {} of arm {} of BRCH {} and belongs to arm "
                                "{}",
                                describe(program, graph, ref),
                                head,
                                arm,
                                brch,
                                owner[ref]));
                    }
                    // An arm is closed under "runs after" inside the branch: no need to go on.
                    continue;
                }
                for (const auto outputs : outputsOf(ref)) {
                    work.insert(work.end(), outputs.begin(), outputs.end());
                }
            }
        }
    }
}

} // namespace camel::runtime
