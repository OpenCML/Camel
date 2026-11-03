// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Puts nodes of the largest graph of the program in two nested regions, commits, decodes the new
 * version and commits again, and prints what it finds for the test plan:
 *   REGIONS count=3 in_inner=2 in_outer=1 result=ok ports=1 within=1
 *   REGIONS recommit same=1
 * The regions mean nothing to the back ends yet; the program runs on as before.
 */

#include "camel/passes/test/regions.h"

#include "camel/runtime/draft_session.h"

#include <exception>
#include <format>
#include <string>

const camel::runtime::Program *
RegionsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    // A refusal is worth reading only if it is the refusal the case is about: the check's own text
    // ends by naming what the read would have had to be ("... nor the exit of a region it owns"), so
    // a reading that finds those words says the rule fired and not a neighbour of it -- an encoder
    // that refused the same draft for another reason would fail this part of the line.
    const auto saysTheRule = [](const std::string &refusal) {
        return refusal.find("nor the exit of a region it owns") != std::string::npos;
    };
    // The shape the commit refuses: a node of one arm reading a port the graph gives to the other
    // arm of the same branch. Nothing in the tree builds it, so the only program that asks the
    // commit to refuse it is this one; without it the check (graph.h, regionIsVisibleFrom) would
    // pass every graph it is asked about for reasons no test could tell from a check that never
    // runs. It goes first, over the program the pass was handed: a refused commit publishes
    // nothing, so the program is still the one the blocks are built on below.
    // The reader is a node of one arm, and the port is the arm's own parameter: a port of the body
    // (`nodeRegion` 0), which the builder writes for every function and an arm reads everywhere.
    // Giving it to a sibling arm of the same branch is the move a rewrite would make and the read
    // the commit has to refuse.
    const GCGraph *branchy = nullptr;
    uint32_t readerArm     = 0;
    uint32_t portArm       = 0;
    gc_node_ref_t reader   = kInvalidNodeRef;
    gc_node_ref_t port     = kInvalidNodeRef;
    for (GCGraph *graph : program->graphs()) {
        for (uint32_t r = 1; branchy == nullptr && r < graph->regionCount(); ++r) {
            const GCRegion *arm = graph->region(r);
            if (arm == nullptr || arm->kind != GCRegionKind::Arm) {
                continue;
            }
            uint32_t sibling = 0;
            for (uint32_t s = 1; s < graph->regionCount() && sibling == 0; ++s) {
                const GCRegion *other = graph->region(s);
                if (s != r && other != nullptr && other->kind == GCRegionKind::Arm &&
                    other->owner == arm->owner) {
                    sibling = s;
                }
            }
            if (sibling == 0) {
                continue;
            }
            for (gc_node_ref_t ref = 0; ref < graph->nodeCount() && port == kInvalidNodeRef; ++ref) {
                if (graph->nodeRegion(ref) != r) {
                    continue;
                }
                for (const std::span<const gc_node_ref_t> group :
                     {graph->normInputsOf(ref),
                      graph->withInputsOf(ref),
                      graph->ctrlInputsOf(ref)}) {
                    for (gc_node_ref_t input : group) {
                        const GCNode *node = graph->node(input);
                        if (node == nullptr || node->kind != GCNodeKind::Port ||
                            graph->nodeRegion(input) != 0) {
                            continue;
                        }
                        branchy   = graph;
                        readerArm = r;
                        portArm   = sibling;
                        reader    = ref;
                        port      = input;
                        break;
                    }
                    if (port != kInvalidNodeRef) {
                        break;
                    }
                }
            }
        }
    }
    if (reader == kInvalidNodeRef) {
        // No branch of two arms, or no arm node reading a port of the body: the program cannot ask
        // the question, and the reading says so instead of passing for a refusal.
        os << "REGIONS sibling_refused=none\n";
    } else {
        RuntimeGraphDraftSession refused(context_, program);
        auto &draft = refused.edit(const_cast<GCGraph *>(branchy)).edit();
        draft.setNodeRegion(port, portArm);
        const gc_node_ref_t moved[1] = {port};
        draft.setRegionPorts(portArm, std::span<const gc_node_ref_t>(moved, 1));
        std::string refusal;
        bool wasRefused = false;
        try {
            (void)refused.commit();
        } catch (const std::exception &e) {
            wasRefused = true;
            refusal    = e.what();
        }
        // Named, not just refused: the commit has to say which port and which reader, or a reader
        // of the diagnostic cannot tell this refusal from the next one.
        const bool namedPort   = refusal.find(std::format("reads port {}", port)) != std::string::npos;
        const bool namedReader =
            refusal.find(std::format("node {} in region {}", reader, readerArm)) != std::string::npos;
        os << "REGIONS sibling_refused=" << (wasRefused ? 1 : 0) << " port=" << port
           << " reader=" << reader << " arm=" << readerArm << " sibling=" << portArm
           << " rule=" << (saysTheRule(refusal) ? 1 : 0) << " named_port=" << (namedPort ? 1 : 0) << " named_reader=" << (namedReader ? 1 : 0)
           << "\n";
    }
    // The shape the owner's exception is drawn around: the node that owns a region reading a port of
    // it that is **not** its exit. The tree has one owner read -- a LOOP consuming its body's exit
    // port, `loop_parts.cml` / `through_match`, `g6n10` reading `g6n9` of `g6r2` -- so the shape is
    // built by taking that exit away: the body's result becomes invalid, and the loop's read of the
    // port is then a read of a port the region no longer ends in. Under the wider exception (the
    // owner reads any port of its region) this draft would commit; a rule that asks "is this port
    // the region's exit" refuses it.
    const GCGraph *looped = nullptr;
    uint32_t bodyRegion   = 0;
    gc_node_ref_t loopNode = kInvalidNodeRef;
    gc_node_ref_t bodyExit = kInvalidNodeRef;
    for (GCGraph *graph : program->graphs()) {
        for (uint32_t r = 1; looped == nullptr && r < graph->regionCount(); ++r) {
            const GCRegion *body = graph->region(r);
            if (body == nullptr || body->kind != GCRegionKind::LoopBody) {
                continue;
            }
            const GCNode *exit = graph->node(body->result);
            if (exit == nullptr || exit->kind != GCNodeKind::Port || body->owner == kInvalidNodeRef) {
                continue;
            }
            // The check walks the readers that are in a region; an owner in the body is not one of
            // them, so the shape has to be a loop that is itself inside a region (a loop in an arm,
            // which is where `loop_parts.cml` and `zero_leaf_state.cml` put theirs).
            if (graph->nodeRegion(body->owner) == 0) {
                continue;
            }
            // The owner has to read it, or there is nothing to take away.
            bool ownerReads = false;
            for (const std::span<const gc_node_ref_t> group :
                 {graph->normInputsOf(body->owner),
                  graph->withInputsOf(body->owner),
                  graph->ctrlInputsOf(body->owner)}) {
                for (gc_node_ref_t input : group) {
                    ownerReads = ownerReads || input == body->result;
                }
            }
            if (!ownerReads) {
                continue;
            }
            looped    = graph;
            bodyRegion = r;
            loopNode  = body->owner;
            bodyExit  = body->result;
        }
        if (looped != nullptr) {
            break;
        }
    }
    if (looped == nullptr) {
        os << "REGIONS owner_refused=none\n";
    } else {
        RuntimeGraphDraftSession ownerRead(context_, program);
        auto &ownerDraft = ownerRead.edit(const_cast<GCGraph *>(looped)).edit();
        ownerDraft.setRegionResult(bodyRegion, kInvalidNodeRef);
        std::string ownerRefusal;
        bool ownerRefused = false;
        try {
            (void)ownerRead.commit();
        } catch (const std::exception &e) {
            ownerRefused = true;
            ownerRefusal = e.what();
        }
        const bool ownerNamesPort =
            ownerRefusal.find(std::format("reads port {}", bodyExit)) != std::string::npos;
        const bool ownerNamesReader =
            ownerRefusal.find(std::format("node {} in region {}", loopNode, looped->nodeRegion(loopNode))) !=
            std::string::npos;
        size_t violations = ownerRefused ? 1 : 0;
        for (size_t at = ownerRefusal.find("; "); at != std::string::npos;
             at        = ownerRefusal.find("; ", at + 2)) {
            ++violations;
        }
        os << "REGIONS owner_refused=" << (ownerRefused ? 1 : 0) << " port=" << bodyExit
           << " owner=" << loopNode << " region=" << bodyRegion
           << " rule=" << (saysTheRule(ownerRefusal) ? 1 : 0) << " named_port=" << (ownerNamesPort ? 1 : 0)
           << " named_reader=" << (ownerNamesReader ? 1 : 0) << " violations=" << violations
           << "\n";
    }
    // The largest graph that has arms, if any has, so that they are among the regions it keeps.
    GCGraph *largest = nullptr;
    const auto hasArms = [](const GCGraph *graph) { return graph->regionCount() > 1; };
    for (GCGraph *graph : program->graphs()) {
        if (largest == nullptr || (hasArms(graph) && !hasArms(largest)) ||
            (hasArms(graph) == hasArms(largest) && graph->nodeCount() > largest->nodeCount())) {
            largest = graph;
        }
    }
    if (largest == nullptr || largest->liveNodeCount() < 8) {
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    auto &draft = session.edit(largest).edit();
    std::vector<gc_node_ref_t> live;
    // What the blocks may take: a node of the body that no node in a region reads. A region reads
    // the ports of the regions on its own chain (checked at commit), so a node another branch reads
    // cannot be worded inside a block; a port words a value the region is handed and the blocks
    // word none; and a node that waits on a BRCH runs between a branch and its join and would
    // belong to no arm. Everything else is the blocks'.
    const GCUseIndex uses(*largest);
    const auto readFromARegion = [&](gc_node_ref_t id) {
        for (const std::span<const gc_node_ref_t> group :
             {uses.norm(id), uses.with(id), uses.ctrl(id)}) {
            for (gc_node_ref_t user : group) {
                if (largest->nodeRegion(user) != 0) {
                    return true;
                }
            }
        }
        return false;
    };
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount() && live.size() < 6; ++id) {
        const GCNode *existing = largest->node(id);
        if (!draft.alive(id) || existing == nullptr || existing->kind == GCNodeKind::Port ||
            largest->nodeRegion(id) != 0 || !largest->ctrlInputsOf(id).empty() ||
            readFromARegion(id)) {
            continue;
        }
        live.push_back(id);
    }
    // No early return here on a short list: a graph this pass cannot host the blocks on is a
    // program the case must not hand it (author's review of A9, 2026-11-03: "a small graph should
    // not be tested, so let the case not choose it"). A silent return made the case read as a pass
    // on a program it had not tested at all. The five nodes are taken with `at`, so a program that
    // cannot host the blocks fails here and says so instead of wordings blocks around whatever the
    // vector's memory held.
    const uint32_t outer = draft.addRegion(GCRegionKind::Block, 0);
    const uint32_t inner = draft.addRegion(GCRegionKind::Block, outer);
    draft.setNodeRegion(live.at(2), outer);
    draft.setNodeRegion(live.at(3), inner);
    draft.setNodeRegion(live.at(4), inner);
    draft.setRegionResult(inner, live.at(4));
    const gc_node_ref_t outerPort = live.at(1);
    draft.setRegionPorts(outer, std::span<const gc_node_ref_t>(&outerPort, 1));
    const Program *next = session.commit();

    const GCGraph *after = next->graph(largest->id());
    size_t inInner = 0, inOuter = 0;
    for (gc_node_ref_t id = 0; after && id < after->nodeCount(); ++id) {
        inInner += after->nodeRegion(id) == inner;
        inOuter += after->nodeRegion(id) == outer;
    }
    const bool resultOk = after && after->region(inner) && after->region(inner)->result == live.at(4);
    const bool portsOk  = after && after->regionPorts(outer).size() == 1 &&
                         after->regionPorts(outer)[0] == outerPort;
    os << "REGIONS count=" << (after ? after->regionCount() : 0) << " in_inner=" << inInner
       << " in_outer=" << inOuter << " result=" << (resultOk ? "ok" : "bad")
       << " ports=" << (portsOk ? 1 : 0) << " within=" << (inInner == 2 ? 1 : 0) << "\n";

    // A decode and an encode with no change in between give the same version.
    RuntimeGraphDraftSession again(context_, next);
    again.edit(const_cast<GCGraph *>(after)).edit();
    const Program *last = again.commit();
    os << "REGIONS recommit same=" << (last->graph(largest->id()) == after ? 1 : 0) << "\n";
    // The recommit installed `last`; a pass returns the program the pipeline is to use, and that
    // is the current one, not the version before the second commit. `last` has the same graph.
    return last;
}
