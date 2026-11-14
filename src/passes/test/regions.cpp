// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Puts nodes of the largest graph of the program in two nested regions, commits, decodes the new
 * version and commits again, and prints what it finds for the test plan:
 *   REGIONS count=3 in_inner=2 in_outer=1 result=ok ports=1 within=1
 *   REGIONS recommit same=1
 * It also builds the two shapes a region may not take -- a control region whose owner is gone, and
 * a node whose region is gone -- and reports whether the commit refused each and named it:
 *   REGIONS orphan_owner_refused=1 region=1 gone=1 rule=1 named_region=1
 *   REGIONS orphan_member_refused=1 region=2 rule=1
 * A value edge pulls the node that reads it into the region the value comes from, an order edge
 * does not, and an order edge that reaches into a region with no owner stays where it is and makes
 * the commit refuse the draft:
 *   REGIONS order_moved=0 value_moved=1 rehome_kept=1 refused=1 rule=1
 * The regions mean nothing to the back ends yet; the program runs on as before.
 */

#include "camel/passes/test/regions.h"

#include "camel/core/type/base.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/draft_session.h"
#include "runtime/graph_build.h"

#include <exception>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

const camel::runtime::Program *
RegionsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    // A refusal is worth reading only if it is the refusal the case is about: the check's own text
    // ends by naming what the read would have had to be ("... nor the exit of a region it owns"),
    // so a reading that finds those words says the rule fired and not a neighbour of it -- an
    // encoder that refused the same draft for another reason would fail this part of the line.
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
            for (gc_node_ref_t ref = 0; ref < graph->nodeCount() && port == kInvalidNodeRef;
                 ++ref) {
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
        const bool namedPort =
            refusal.find(std::format("reads port {}", port)) != std::string::npos;
        const bool namedReader =
            refusal.find(std::format("node {} in region {}", reader, readerArm)) !=
            std::string::npos;
        os << "REGIONS sibling_refused=" << (wasRefused ? 1 : 0) << " port=" << port
           << " reader=" << reader << " arm=" << readerArm << " sibling=" << portArm
           << " rule=" << (saysTheRule(refusal) ? 1 : 0) << " named_port=" << (namedPort ? 1 : 0)
           << " named_reader=" << (namedReader ? 1 : 0) << "\n";
    }
    // The shape the owner's exception is drawn around: the node that owns a region reading a port
    // of it that is **not** its exit. The tree has one owner read -- a LOOP consuming its body's
    // exit port, `loop_parts.cml` / `through_match`, `g6n10` reading `g6n9` of `g6r2` -- so the
    // shape is built by taking that exit away: the body's result becomes invalid, and the loop's
    // read of the port is then a read of a port the region no longer ends in. Under the wider
    // exception (the owner reads any port of its region) this draft would commit; a rule that asks
    // "is this port the region's exit" refuses it.
    const GCGraph *looped  = nullptr;
    uint32_t bodyRegion    = 0;
    gc_node_ref_t loopNode = kInvalidNodeRef;
    gc_node_ref_t bodyExit = kInvalidNodeRef;
    for (GCGraph *graph : program->graphs()) {
        for (uint32_t r = 1; looped == nullptr && r < graph->regionCount(); ++r) {
            const GCRegion *body = graph->region(r);
            if (body == nullptr || body->kind != GCRegionKind::LoopBody) {
                continue;
            }
            const GCNode *exit = graph->node(body->result);
            if (exit == nullptr || exit->kind != GCNodeKind::Port ||
                body->owner == kInvalidNodeRef) {
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
            looped     = graph;
            bodyRegion = r;
            loopNode   = body->owner;
            bodyExit   = body->result;
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
            ownerRefusal.find(
                std::format("node {} in region {}", loopNode, looped->nodeRegion(loopNode))) !=
            std::string::npos;
        size_t violations = ownerRefused ? 1 : 0;
        for (size_t at = ownerRefusal.find("; "); at != std::string::npos;
             at        = ownerRefusal.find("; ", at + 2)) {
            ++violations;
        }
        os << "REGIONS owner_refused=" << (ownerRefused ? 1 : 0) << " port=" << bodyExit
           << " owner=" << loopNode << " region=" << bodyRegion
           << " rule=" << (saysTheRule(ownerRefusal) ? 1 : 0)
           << " named_port=" << (ownerNamesPort ? 1 : 0)
           << " named_reader=" << (ownerNamesReader ? 1 : 0) << " violations=" << violations
           << "\n";
    }
    // The two shapes a rewrite leaves behind when it removes the node a region belonged to, in the
    // two directions the commit's region check reads them (`validateRuntimeRegions`,
    // src/runtime/graph.cpp): a control region whose owner the graph no longer has, and a node
    // whose region the graph no longer has. Neither is reachable through the draft's own writers --
    // erasing a BRCH or a LOOP dissolves its regions and hands what they held to the region around
    // them (`dissolveRegionsOf`, on the draft), and erasing any other node takes no region with it
    // -- so the only program that asks the commit to judge them is this pass. That is the point of
    // asking: with no case that asks, a check that had stopped reading either direction would look
    // exactly like one that reads it, and the pruning that leaves a region behind would publish.
    //
    // The owner is a node the draft makes and then erases, so the region's record is what a pruning
    // pass leaves when it takes the loop or the branch away and forgets the region: the region
    // still names a node, and the graph no longer has it. The refusal has to name the region --
    // nobody can act on "a region is wrong" -- and it has to be the owner's own wording and not a
    // neighbour's: the port and member rules refuse other shapes of the same draft.
    const GCGraph *lostRegionGraph = nullptr;
    uint32_t lostRegion            = 0;
    for (GCGraph *graph : program->graphs()) {
        for (uint32_t r = 1; r < graph->regionCount(); ++r) {
            const GCRegion *region = graph->region(r);
            if (region == nullptr || region->owner == kInvalidNodeRef) {
                continue;
            }
            if (region->kind != GCRegionKind::Arm && region->kind != GCRegionKind::LoopCond &&
                region->kind != GCRegionKind::LoopBody) {
                continue;
            }
            lostRegionGraph = graph;
            lostRegion      = r;
            break;
        }
        if (lostRegionGraph != nullptr) {
            break;
        }
    }
    if (lostRegionGraph == nullptr) {
        os << "REGIONS orphan_owner_refused=none\n";
    } else {
        RuntimeGraphDraftSession orphanOwner(context_, program);
        auto &wrongOwner          = orphanOwner.edit(const_cast<GCGraph *>(lostRegionGraph)).edit();
        const gc_node_ref_t owner = wrongOwner.addCopyNode(camel::core::type::Type::Int32());
        std::string refusal;
        bool refused = false;
        if (owner != kInvalidNodeRef) {
            wrongOwner.setRegionOwner(lostRegion, owner, 0);
            wrongOwner.eraseNode(owner);
            try {
                (void)orphanOwner.commit();
            } catch (const std::exception &e) {
                refused = true;
                refusal = e.what();
            }
        }
        // The two wordings the owner's own direction of the check has: the node the region names is
        // not in the graph, or a control region has no owner at all. A refusal that names a port or
        // a member instead answers a different question and says nothing about this one.
        const bool ownerRule =
            (refusal.find("that names a node the graph does not have") != std::string::npos ||
             refusal.find("whose owner does not fit its kind") != std::string::npos) &&
            !refusal.empty();
        const bool namedRegion =
            refusal.find(std::format("has region {} ", lostRegion)) != std::string::npos;
        os << "REGIONS orphan_owner_refused=" << (refused ? 1 : 0) << " region=" << lostRegion
           << " gone=" << (wrongOwner.alive(owner) ? 0 : 1) << " rule=" << (ownerRule ? 1 : 0)
           << " named_region=" << (namedRegion ? 1 : 0);
        if (refused && !(ownerRule && namedRegion)) {
            os << " said=\"" << refusal.substr(0, 140) << "\"";
        }
        os << "\n";
    }
    // The other direction: a node the graph has, in a region the graph does not. A rewrite that
    // retires a region and leaves a node pointing at it asks the same question from the node's
    // side, and only that side can refuse it -- a region has no member table to be short of one.
    // The node is one the draft makes, so nothing reads it and no region names it: the one thing
    // wrong with this graph is that the node's region is not there, which is what the refusal has
    // to name. A node of the program moved into a retired region would say the same thing and, if
    // an arm read it, would be refused by the port rule first -- a different question.
    const GCGraph *strandedGraph = lostRegionGraph;
    if (strandedGraph == nullptr && !program->graphs().empty()) {
        strandedGraph = *program->graphs().begin();
    }
    if (strandedGraph == nullptr) {
        os << "REGIONS orphan_member_refused=none\n";
    } else {
        RuntimeGraphDraftSession stranded(context_, program);
        auto &wrongRegion         = stranded.edit(const_cast<GCGraph *>(strandedGraph)).edit();
        const uint32_t goneRegion = wrongRegion.addRegion(GCRegionKind::Block, 0);
        wrongRegion.retireRegion(goneRegion, false);
        const gc_node_ref_t strandedNode =
            wrongRegion.addCopyNode(camel::core::type::Type::Int32());
        std::string refusal;
        bool refused = false;
        if (strandedNode != kInvalidNodeRef) {
            wrongRegion.setNodeRegion(strandedNode, goneRegion);
            try {
                (void)stranded.commit();
            } catch (const std::exception &e) {
                refused = true;
                refusal = e.what();
            }
        }
        // The node is named by the number the graph gave it, which is not the draft's number, so
        // the reading anchors on the region: region ids are the table's own and are not renumbered.
        const bool namedRegion =
            refusal.find(std::format("in region {}, which is not there", goneRegion)) !=
            std::string::npos;
        const bool namesNode = refusal.find("has node ") != std::string::npos;
        os << "REGIONS orphan_member_refused=" << (refused ? 1 : 0) << " region=" << goneRegion
           << " rule=" << (namedRegion && namesNode ? 1 : 0);
        if (refused && !(namedRegion && namesNode)) {
            os << " said=\"" << refusal.substr(0, 140) << "\"";
        }
        os << "\n";
    }

    // The rest of the region table a commit reads (src/runtime/graph.cpp#validateRuntimeRegions).
    // Each rule below is checked on every commit and, before this, none of them had a case that
    // would tell a check that runs from a check that never runs. Five of them cannot be written
    // through the draft at all, and that is why the shapes are built on the frozen record: the
    // draft's `addRegion` asserts `parent < regions_.size()` and hands ids out in the order it
    // makes the regions, so a parent always names a smaller id -- a draft's table has no parent
    // past its end and no cycle -- `setRegionPorts` appends the ports it is given, so a region's
    // span is always inside the table it was written into, the body is made as a `GCRegion{}`
    // with no setter that changes a kind once the region is made, and `setNodeRegion` asserts
    // `region < regions_.size()`, so a node's region is never past the count either. The graph a
    // commit validates is the one the encoder writes, and a rule that guards the table's shape is
    // therefore pinned by the record it refuses, not by a draft that could not make it -- the same
    // reading `test::edges`' EdgesOrderPureTestPass takes of the order edge on a pure node
    // (src/passes/test/edges.cpp#EdgesOrderPureTestPass).
    // A reading is 1 only when the refusal carries that rule's own words, so a graph refused by a
    // neighbouring rule reads 0 instead of counting for a check it did not exercise; when a
    // reading is 0 the line says what came back instead, which is what a loosened check prints.
    const auto frozenCase = [&](const GCGraphPayloadShape &shape,
                                const std::function<void(GCGraphPayloadArena &)> &writeTable) {
        std::vector<GraphId> noGraphs;
        return GCGraphBuildAccess::create(
            context_->graphIds().allocate(),
            GCGraphDerivation{},
            createGraphDebugRecord("test$region_table", "test$region_table"),
            camel::core::type::FunctionType::create(),
            std::nullopt,
            nullptr,
            nullptr,
            kInvalidGraphId,
            noGraphs,
            noGraphs,
            noGraphs,
            noGraphs,
            noGraphs,
            shape,
            [&](GCGraphPayloadArena &arena) {
                // Two Data nodes: the least a graph the validator reads can hold. Everything the
                // validator checks besides the regions is sound on them, so a graph the case reads
                // 0 on is one the region rule alone would have refused.
                for (gc_node_ref_t ref = 0; ref < static_cast<gc_node_ref_t>(shape.nodeCount);
                     ++ref) {
                    GCNode node;
                    node.kind        = GCNodeKind::Data;
                    node.typeId      = camel::core::type::Type::Float32()->typeId();
                    arena.nodes[ref] = node;
                }
                arena.nodes[shape.nodeCount] = GCNode{};
                arena.entryNode              = 0;
                arena.exitNode               = 1;
                arena.outputNode             = 0;
                // A record of the table is zeroed when it is made, and zero names node 0 as the
                // owner, which reads as "a control region that owns a node" on a region whose
                // kind is not one of the three. The filler is "no node", and a case that means a
                // ref sets it after this.
                for (uint32_t r = 0; r < static_cast<uint32_t>(shape.regionCount); ++r) {
                    arena.regionTable[r].owner    = kInvalidNodeRef;
                    arena.regionTable[r].result   = kInvalidNodeRef;
                    arena.regionTable[r].callSite = kInvalidNodeRef;
                }
                writeTable(arena);
            },
            std::span<const slot_t>{});
    };
    const auto reading = [&](std::string_view name, const GCGraph *graph, std::string_view words) {
        std::string refusal;
        try {
            validateRuntimeGraphPayload(*program, graph);
        } catch (const std::exception &e) {
            refusal = e.what();
        }
        const bool saysIt = refusal.find(words) != std::string::npos;
        os << "REGIONS " << name << "=" << (saysIt ? 1 : 0);
        if (!saysIt) {
            const std::string_view instead =
                refusal.empty() ? std::string_view{"accepted"} : std::string_view(refusal);
            os << " why=" << instead;
        }
        os << "\n";
    };

    // 1. `count != 0 && graph->region(0)->kind != GCRegionKind::Root` ->
    //    "has region {} as its body, which is not a root."
    {
        GCGraphPayloadShape shape;
        shape.nodeCount    = 2;
        shape.regionCount  = 1;
        const GCGraph *bad = frozenCase(shape, [](GCGraphPayloadArena &arena) {
            arena.regionTable[0].kind = GCRegionKind::Block;
        });
        reading("bad_body_kind_refused", bad, "as its body, which is not a root");
    }
    // 2. `region.kind == GCRegionKind::Root || region.parent >= count` ->
    //    "has region {} with a parent that is not a region." Both arms of that one condition are
    //    read here, because either one alone is a table that is not a tree.
    {
        GCGraphPayloadShape shape;
        shape.nodeCount           = 2;
        shape.regionCount         = 2;
        const GCGraph *pastTheEnd = frozenCase(shape, [](GCGraphPayloadArena &arena) {
            arena.regionTable[1].kind   = GCRegionKind::Block;
            arena.regionTable[1].parent = 2; // the table holds two regions, 0 and 1
        });
        reading("parent_out_of_range_refused", pastTheEnd, "with a parent that is not a region");
        const GCGraph *secondRoot = frozenCase(shape, [](GCGraphPayloadArena &arena) {
            arena.regionTable[1].kind = GCRegionKind::Root; // a body that is not region 0
        });
        reading("root_outside_body_refused", secondRoot, "with a parent that is not a region");
    }
    // 3. The walk up the parents: `if (hops > count) bad("that is in a cycle of regions", r)`.
    //    Two regions that are each other's parent: the walk never reaches the body.
    {
        GCGraphPayloadShape shape;
        shape.nodeCount      = 2;
        shape.regionCount    = 3;
        const GCGraph *cycle = frozenCase(shape, [](GCGraphPayloadArena &arena) {
            arena.regionTable[1].kind   = GCRegionKind::Block;
            arena.regionTable[1].parent = 2;
            arena.regionTable[2].kind   = GCRegionKind::Block;
            arena.regionTable[2].parent = 1;
        });
        reading("parent_cycle_refused", cycle, "that is in a cycle of regions");
    }
    // 4. `region.portBegin + region.portCount > graph->nodePayload()->regionPortCount` ->
    //    "has region {} with ports outside the table." The span starts one past a table of two.
    {
        GCGraphPayloadShape shape;
        shape.nodeCount        = 2;
        shape.regionCount      = 2;
        shape.regionPortCount  = 2;
        const GCGraph *outside = frozenCase(shape, [](GCGraphPayloadArena &arena) {
            arena.regionTable[1].kind      = GCRegionKind::Block;
            arena.regionTable[1].portBegin = 2;
            arena.regionTable[1].portCount = 1;
            arena.regionPorts[0]           = kInvalidNodeRef;
            arena.regionPorts[1]           = kInvalidNodeRef;
        });
        reading("ports_outside_table_refused", outside, "with ports outside the table");
    }
    // 5. `!inGraph(port) || (control && port == kInvalidNodeRef)` ->
    //    "has region {} with a port the graph does not have." An annotation region may keep a port
    //    a rewrite removed; a control region may not, because it is executed as a whole. This one
    //    the draft can ask, so it is asked of a commit: the arm keeps every port it had and gains
    //    one that names nothing, so the refusal is the port rule and not a read of the arm's ports
    //    (which commit reports as "reads port ...", the rule the two cases above pin).
    const GCGraph *armed = nullptr;
    uint32_t armRegion   = 0;
    for (GCGraph *graph : program->graphs()) {
        for (uint32_t r = 1; armed == nullptr && r < graph->regionCount(); ++r) {
            const GCRegion *region = graph->region(r);
            if (region != nullptr && region->kind == GCRegionKind::Arm) {
                armed     = graph;
                armRegion = r;
            }
        }
        if (armed != nullptr) {
            break;
        }
    }
    if (armed == nullptr) {
        // No arm: the program cannot ask the question, and the reading says so instead of
        // passing for a refusal.
        os << "REGIONS control_port_missing_refused=none\n";
    } else {
        RuntimeGraphDraftSession portSession(context_, program);
        auto &portDraft = portSession.edit(const_cast<GCGraph *>(armed)).edit();
        std::vector<gc_node_ref_t> armPorts(
            armed->regionPorts(armRegion).begin(),
            armed->regionPorts(armRegion).end());
        armPorts.push_back(kInvalidNodeRef);
        portDraft.setRegionPorts(armRegion, std::span<const gc_node_ref_t>(armPorts));
        std::string portRefusal;
        try {
            (void)portSession.commit();
        } catch (const std::exception &e) {
            portRefusal = e.what();
        }
        const bool portRefused =
            portRefusal.find("with a port the graph does not have") != std::string::npos;
        os << "REGIONS control_port_missing_refused=" << (portRefused ? 1 : 0);
        if (!portRefused) {
            const std::string_view instead =
                portRefusal.empty() ? std::string_view{"accepted"} : std::string_view(portRefusal);
            os << " why=" << instead;
        }
        os << "\n";
    }
    // 6. The node column: `region != 0 && (region >= count || region(kind) == Dead)` ->
    //    "has node {} in region {}, which is not there." The Dead half is pinned where a rewrite
    //    retires a region; this is the half no rewrite can reach, a node in a region past the end.
    //    (`setNodeRegion` asserts the id is below the count, so the draft cannot write it either.)
    {
        GCGraphPayloadShape shape;
        shape.nodeCount          = 2;
        shape.regionCount        = 1;
        shape.regions            = true; // the graph carries a region column
        const GCGraph *pastCount = frozenCase(shape, [](GCGraphPayloadArena &arena) {
            arena.regions[0] = 5; // a region the table does not have
            arena.regions[1] = 0;
        });
        reading("node_region_out_of_range_refused", pastCount, "which is not there");
    }
    // 6b. The refs a region names, besides its owner: `!inGraph(region.result) ||
    //     !inGraph(region.callSite)` -> "has region {} that names a node the graph does not have."
    //     A result and a call site that name a node the graph does not hold.
    {
        GCGraphPayloadShape shape;
        shape.nodeCount          = 2;
        shape.regionCount        = 2;
        const GCGraph *badResult = frozenCase(shape, [](GCGraphPayloadArena &arena) {
            arena.regionTable[1].kind   = GCRegionKind::Block;
            arena.regionTable[1].result = 2; // two nodes, so 2 names none
        });
        reading(
            "result_ref_missing_refused",
            badResult,
            "that names a node the graph does not have");
        const GCGraph *badCallSite = frozenCase(shape, [](GCGraphPayloadArena &arena) {
            arena.regionTable[1].kind     = GCRegionKind::Inline;
            arena.regionTable[1].callSite = 2;
        });
        reading(
            "callsite_ref_missing_refused",
            badCallSite,
            "that names a node the graph does not have");
    }

    // The largest graph that has arms, if any has, so that they are among the regions it keeps.
    GCGraph *largest   = nullptr;
    const auto hasArms = [](const GCGraph *graph) { return graph->regionCount() > 1; };
    for (GCGraph *graph : program->graphs()) {
        if (largest == nullptr || (hasArms(graph) && !hasArms(largest)) ||
            (hasArms(graph) == hasArms(largest) && graph->nodeCount() > largest->nodeCount())) {
            largest = graph;
        }
    }
    if (largest == nullptr || largest->storedNodeCount() < 8) {
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
    const bool resultOk =
        after && after->region(inner) && after->region(inner)->result == live.at(4);
    const bool portsOk =
        after && after->regionPorts(outer).size() == 1 && after->regionPorts(outer)[0] == outerPort;
    os << "REGIONS count=" << (after ? after->regionCount() : 0) << " in_inner=" << inInner
       << " in_outer=" << inOuter << " result=" << (resultOk ? "ok" : "bad")
       << " ports=" << (portsOk ? 1 : 0) << " within=" << (inInner == 2 ? 1 : 0) << "\n";

    // A decode and an encode with no change in between give the same version.
    RuntimeGraphDraftSession again(context_, next);
    again.edit(const_cast<GCGraph *>(after)).edit();
    const Program *last = again.commit();
    os << "REGIONS recommit same=" << (last->graph(largest->id()) == after ? 1 : 0) << "\n";

    // What a move into a region is for, and what it is not (docs/design/effects/edges.cn.md 4.6).
    // `GraphDraft::inheritRegion` keeps a read legal: a node that reads a value an arm or a part of
    // a loop computes has to run there, so a value edge pulls the reader in (`value_moved=1`). An
    // order edge says when the reader runs, not what it reads, so it leaves the reader where it was
    // written (`order_moved=0`): moving it would run it once per pass of a loop it was never
    // written in, and it would leave the edges the reader already carried pointing out of the
    // region it was moved into. What such an edge needs instead is the node that owns the region
    // (`GraphDraft::rehomeOrderEdges`), and where there is no such node -- the region here has no
    // owner -- the edge is left exactly as it was (`kept=1`) for the seal check to refuse, never
    // dropped: an order edge that is dropped is an order that is lost (design principles 3.1). The
    // refused commit publishes nothing, so the program the pass hands on is `last`.
    RuntimeGraphDraftSession moved(context_, last);
    auto &subject = moved.edit(const_cast<GCGraph *>(last->graph(largest->id()))).edit();
    const uint32_t part = subject.addRegion(GCRegionKind::LoopBody, 0);
    const gc_node_ref_t inside =
        subject.addGateNode(camel::core::type::Type::Float32());
    subject.setNodeRegion(inside, part);
    const gc_node_ref_t ordered = subject.addGateNode(camel::core::type::Type::Float32());
    (void)subject.appendOrderDependency(ordered, inside);
    os << "REGIONS order_moved=" << (subject.nodeRegion(ordered) == part ? 1 : 0);
    const gc_node_ref_t read[1] = {inside};
    subject.setNormInputs(ordered, read);
    os << " value_moved=" << (subject.nodeRegion(ordered) == part ? 1 : 0);
    const uint32_t ownerless = subject.addRegion(GCRegionKind::Arm, 0);
    const gc_node_ref_t unowned =
        subject.addGateNode(camel::core::type::Type::Float32());
    subject.setNodeRegion(unowned, ownerless);
    const gc_node_ref_t waiting = subject.addGateNode(camel::core::type::Type::Float32());
    (void)subject.appendOrderDependency(waiting, unowned);
    (void)subject.rehomeOrderEdges();
    bool kept = false;
    for (gc_node_ref_t pred : subject.orderInputsOf(waiting)) {
        kept = kept || pred == unowned;
    }
    os << " rehome_kept=" << (kept ? 1 : 0);
    bool refused = false;
    std::string refusal;
    try {
        (void)moved.commit();
    } catch (const std::exception &e) {
        refused = true;
        refusal = e.what();
    }
    // The refusal that names the region without an owner, not a neighbouring rule about the same
    // draft: the graph is refused, and the reading says which rule said so.
    const bool ownerRule =
        refusal.find("whose owner does not fit its kind") != std::string::npos;
    os << " refused=" << (refused ? 1 : 0) << " rule=" << (ownerRule ? 1 : 0) << "\n";
    return last;
}
