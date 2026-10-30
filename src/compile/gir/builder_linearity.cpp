// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A stream is read once on each execution path (design principles 5.4). Whether a program keeps
// that rule is a question about the built graph, not about the spellings the builder happened to
// notice: every data edge that leaves a stream-bearing value is a read of it, whatever wrote the
// edge -- an argument of a call, a member of a tuple, a slot of a function type. The pass below
// measures those edges once the bodies are built, over the whole program: a value handed to a
// function is read where that call stands, and the callee's own graph says nothing about it.
//
// Two reads of one value are one when no path reaches both: one of them stands in an arm of a
// branch and the other in another arm of that same branch. A read standing in a loop's round
// region is a read per round, and only a value that is the loop's own state may be read there --
// a value written outside the loop is read again by every round. Reads that belong to one loop's
// assembly are that loop's one read of what it carries: the sequence it iterates, and the slot of
// its state that holds the value back. A value a closure captures is refused where the closure is
// built, not here (design principles 5.4).

#include "builder.h"
#include "builder_helpers.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/graph.h"

#include <algorithm>

namespace camel::compile::gir {

namespace {

using camel::core::type::FunctionType;
using camel::core::type::StructType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::core::type::TypeCode;
using camel::runtime::GCRegion;
using camel::runtime::GCRegionKind;

/// Whether a value of this type is read once on each path: a stream is, and so is the step
/// function of a stream -- a function taking nothing whose answer carries one, because that is how
/// a stream's value is spelled, so a slot of that shape re-reads the stream every time it is
/// called.
///
/// A tuple or a struct that holds a stream is not a value of this kind: reading `p.0` and `p.1` of
/// one answer reads two members, which is not reading the stream twice, and a stream written twice
/// into one tuple is caught at the stream itself -- the tuple reads it there twice.
bool readOnce(const Type *type, int depth = 0) {
    if (type == nullptr || depth > 8) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Stream:
        return true;
    case TypeCode::Function: {
        const auto *function = static_cast<const FunctionType *>(type);
        return function->withTypesCount() == 0 && function->normTypesCount() == 0 &&
               readOnce(function->exitType(), depth + 1);
    }
    default:
        return false;
    }
}

/// One control region a read stands in, as walking out of it reaches the region: the branch or
/// loop that owns the region, which arm of a branch it is, and whether it is a round region (a
/// loop's condition or its body, which run again on every round).
struct RegionStep {
    uint32_t owner  = 0;
    uint32_t index  = 0;
    uint32_t region = 0;
    bool round      = false;
};

/// The control regions around `region`, innermost first. Regions that only annotate are looked
/// through: they say where a value came from, not when it runs.
std::vector<RegionStep>
controlRegionsAround(const runtime::GraphTransaction &transaction, uint32_t region) {
    const auto &draft = transaction.graph();
    std::vector<RegionStep> steps;
    const uint32_t regions = static_cast<uint32_t>(draft.regionCount());
    for (uint32_t at = region; at != 0 && at < regions && steps.size() < 64;) {
        const GCRegion &step = draft.region(at);
        if (step.kind == GCRegionKind::Arm) {
            steps.push_back({step.owner, step.index, at, false});
        } else if (step.kind == GCRegionKind::LoopCond || step.kind == GCRegionKind::LoopBody) {
            steps.push_back({step.owner, 0, at, true});
        }
        at = step.parent;
    }
    return steps;
}

/// Whether no path reaches both reads: one of them stands in an arm of a branch and the other in
/// another arm of that same branch. Two reads in one arm, or in the rounds of one loop, are on one
/// path and are both made.
bool apart(const std::vector<RegionStep> &left, const std::vector<RegionStep> &right) {
    for (const RegionStep &mine : left) {
        for (const RegionStep &theirs : right) {
            if (!mine.round && !theirs.round && mine.owner == theirs.owner &&
                mine.index != theirs.index) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

/// Whether a value of this type is read once on each path: a stream is, and so is the step
/// function of a stream -- a function taking nothing whose answer carries one, because that is how
/// a stream's value is spelled, so a slot of that shape re-reads the stream every time it is
/// called.
///
/// A tuple or a struct that holds a stream is not a value of this kind: reading `p.0` and `p.1` of
/// one answer reads two members, which is not reading the stream twice, and a stream written twice
/// into one tuple is caught at the stream itself -- the tuple reads it there twice.
bool streamReadOnce(const Type *type) {
    return readOnce(type, 0);
}

void Builder::settleStreamLinearity() {
    // A value a closure captured is the same value inside the closure: a capture port stands for
    // what it captured, and one value stays one value however deep the closures nest. The reads of
    // one value are counted once, over the graphs of the whole program, so a stream read inside a
    // closure and handed along outside it is one value with one reader per path (design principles
    // 5.4). A capture is not a read in itself: it carries the value, and a stream a lambda captures
    // is refused where that closure is built.
    std::unordered_map<node_handle_t, node_handle_t> roots;
    const auto findRoot = [&](node_handle_t node) -> node_handle_t {
        auto at = roots.find(node);
        if (at == roots.end()) {
            roots.emplace(node, node);
            return node;
        }
        node_handle_t root = node;
        while (true) {
            const auto up = roots.find(root);
            if (up == roots.end() || up->second == root) {
                break;
            }
            root = up->second;
        }
        for (node_handle_t walk = node; walk != root;) {
            const auto up = roots.find(walk);
            if (up == roots.end()) {
                break;
            }
            const node_handle_t next = up->second;
            up->second               = root;
            walk                     = next;
        }
        return root;
    };
    for (const auto &[port, origin] : captureOriginOf_) {
        const node_handle_t left  = findRoot(port);
        const node_handle_t right = findRoot(origin);
        if (left != right) {
            roots[left] = right;
        }
    }
    // The ports a machine built for a loop's rounds are one value with what the machine was handed:
    // the sequence the loop iterates and the slot of its state that holds it back are one stream,
    // so a read of the slot inside a round and a read of the sequence outside it are two reads of
    // one value. A machine's port and its origin stand in one graph -- the loop is built where the
    // value is -- where captureInto makes no port and records nothing, so the machine records it
    // itself, in the same place a capture origin is written (noteMachinePortOrigin).
    for (const auto &[port, origin] : machinePortOrigin_) {
        const node_handle_t left  = findRoot(port);
        const node_handle_t right = findRoot(origin);
        if (left != right) {
            roots[left] = right;
        }
    }

    // One read of a value, and the graph it was written in.
    struct Reader {
        DraftGraphBuilder *graph = nullptr;
        draft_node_ref_t node    = 0;
    };
    // What the pass needs of a graph: the transaction that owns it and its loops (the node a round
    // region belongs to).
    struct GraphFacts {
        const runtime::GraphTransaction *transaction = nullptr;
        std::vector<draft_node_ref_t> loops;
    };
    std::unordered_map<DraftGraphBuilder *, GraphFacts> facts;
    std::unordered_map<node_handle_t, std::vector<Reader>> readers;
    std::unordered_map<node_handle_t, node_handle_t> valueOf;

    // A graph is judged once, whatever registered it and however many units name it: a reader
    // counted twice would make one read look like two, and a loop asked about twice the same.
    // The captures a call passes its callee (captures.cpp, addCaptureSite) are remembered as the
    // edges they are: they hand the value on, and the capture they feed is refused where it is
    // made, so counting them here would count one read twice.
    std::unordered_map<node_handle_t, std::unordered_set<node_handle_t>> captureEdges;
    for (const CaptureSite &site : captureSites_) {
        for (node_handle_t value : site.values) {
            captureEdges[site.call].insert(value);
        }
    }
    std::unordered_set<DraftGraphBuilder *> judged;
    for (const ModuleUnit *unit : unitOrder_) {
        for (const compile_graph_ptr_t &graph : unit->builtBodies) {
            // A body that was given up is left out: what it holds was never finished, and the
            // mistake that gave it up is the one to report.
            if (graph == nullptr || failedBodies_.contains(graph.get()) ||
                !judged.insert(graph.get()).second) {
                continue;
            }
            const runtime::GraphTransaction &transaction = graph->transaction();
            const auto &draft                            = transaction.graph();
            GraphFacts &known                            = facts[graph.get()];
            known.transaction                            = &transaction;
            for (uint32_t at = 1; at < static_cast<uint32_t>(draft.regionCount()); ++at) {
                const GCRegion &region = draft.region(at);
                if (region.kind == GCRegionKind::LoopCond || region.kind == GCRegionKind::LoopBody) {
                    known.loops.push_back(region.owner);
                }
            }
            const size_t slots = draft.nodeSlotCount();
            for (draft_node_ref_t id = 0; id < slots; ++id) {
                if (!draft.alive(id)) {
                    continue;
                }
                const node_handle_t self = draft.ref(id);
                Type *selfType           = nodeTypeOf(self);
                const bool closure = selfType != nullptr && selfType->code() == TypeCode::Function;
                const runtime::GCNodeKind kind = nodeKindOf(self);
                // An operator's operand is not read by handing it over: the operator is known, and
                // the stream it is given is the value of its step function -- `:stream/tee_note`
                // reads the type of the source a tee reports and pulls nothing from it, and the
                // store into a cell (`:var/new`) only puts it there, where its own reads are
                // judged. No operator of the core reads elements out of a stream.
                const bool operand = kind == runtime::GCNodeKind::Oper;
                const auto note    = [&](draft_node_ref_t value, bool handed) {
                    if (value >= slots || handed) {
                        return;
                    }
                    const node_handle_t held = draft.ref(value);
                    const node_handle_t key  = findRoot(held);
                    readers[key].push_back(Reader{graph.get(), id});
                    valueOf.emplace(key, held);
                };
                // Every data input of a node is one read of the value it names, `Norm` inputs as
                // well as `With` ones: a loop's state, a call's argument and a slot of a function
                // type are all edges that hand the value on. A tuple written `(s, s)` names the
                // value twice, and the two members are two reads, so equal entries are kept.
                for (draft_node_ref_t value : draft.normInputsOf(id)) {
                    note(value, operand);
                }
                if (!closure) {
                    const auto sites = captureEdges.find(self);
                    for (draft_node_ref_t value : draft.withInputsOf(id)) {
                        const bool handed =
                            sites != captureEdges.end() && sites->second.contains(draft.ref(value));
                        note(value, handed);
                    }
                }
            }
        }
    }

    // Whether a node of a graph takes a node of that graph as one of its inputs.
    const auto readsNode = [](const runtime::GraphTransaction &transaction, draft_node_ref_t id,
                              node_handle_t which) {
        const auto &draft = transaction.graph();
        for (draft_node_ref_t value : draft.normInputsOf(id)) {
            if (draft.ref(value) == which) {
                return true;
            }
        }
        for (draft_node_ref_t value : draft.withInputsOf(id)) {
            if (draft.ref(value) == which) {
                return true;
            }
        }
        return false;
    };
    // A capture is refused only where the closure reads what it captured: what the rule refuses is
    // the read such a capture would make, and a capture that only hands the value on -- into a
    // cell, into a report, into the captures of a call -- makes none. The port's uses are in hand
    // now (the body of a closure is built before the value it makes, and the capture is recorded at
    // its first use), so the verdict is settled with the pass while the capture itself is recorded
    // where it is made, at the one place a capture is written (captureInto).
    std::erase_if(streamViolations_, [&](const StreamViolation &violation) {
        if (violation.kind != StreamViolation::Kind::Captured || violation.port == nullptr) {
            return false;
        }
        const auto known = facts.find(violation.graph);
        if (known == facts.end()) {
            return false;
        }
        const auto &draft  = known->second.transaction->graph();
        const size_t slots = draft.nodeSlotCount();
        for (draft_node_ref_t id = 0; id < slots; ++id) {
            if (!draft.alive(id) || !readsNode(violation.graph->transaction(), id, violation.port)) {
                continue;
            }
            // A call reads the value it applies and the arguments it hands on; a direct call reads
            // the arguments it declares (its `With` inputs are the captures it passes, which the
            // callee's own verdict answers for); a loop reads the sequence it pulls round by round.
            const runtime::GCNodeKind kind = nodeKindOf(draft.ref(id));
            if (kind == runtime::GCNodeKind::Call || kind == runtime::GCNodeKind::Loop) {
                return false;
            }
            if (kind == runtime::GCNodeKind::Func) {
                for (draft_node_ref_t value : draft.normInputsOf(id)) {
                    if (draft.ref(value) == violation.port) {
                        return false;
                    }
                }
            }
        }
        return true;
    });

    for (const auto &[identity, list] : readers) {
        const node_handle_t value = valueOf[identity];
        Type *type                = nodeTypeOf(value);
        if (!readOnce(type)) {
            continue;
        }
        // TEMPORARY INSTRUMENTATION (removed before the commit): the readings the handover asks for.
        Type *valueType = valueTypeOf(type);
        const std::string what =
            valueType != nullptr ? valueType->toString() : std::string("a stream");
        // The loops this value is a state of: a `for` hands the sequence to the LOOP node of the
        // machine it builds, and one `for` is one consumer of the sequence it iterates. Such a loop
        // may stand in another graph than the read does -- a closure's graph holds the value it
        // captured -- so the loops of every graph are asked.
        std::vector<std::pair<DraftGraphBuilder *, draft_node_ref_t>> carriers;
        for (const auto &[builder, known] : facts) {
            for (draft_node_ref_t loop : known.loops) {
                for (draft_node_ref_t state : known.transaction->graph().normInputsOf(loop)) {
                    if (findRoot(known.transaction->graph().ref(state)) == identity) {
                        carriers.emplace_back(builder, loop);
                        break;
                    }
                }
            }
        }
        std::vector<std::vector<RegionStep>> steps;
        std::vector<std::pair<DraftGraphBuilder *, draft_node_ref_t>> consumers;
        steps.reserve(list.size());
        consumers.reserve(list.size());
        for (const Reader &reader : list) {
            const GraphFacts &known = facts[reader.graph];
            const auto &draft = known.transaction->graph();
            const auto around =
                controlRegionsAround(reader.graph->transaction(), draft.nodeRegion(reader.node));
            steps.push_back(around);
            const bool inRound =
                std::any_of(around.begin(), around.end(), [](const RegionStep &step) {
                    return step.round;
                });
            // The loop's own edge is the loop's read: the sequence a `for` iterates, and the state
            // the machine is handed, are wired into the LOOP node itself, and one loop is one
            // consumer of what it carries. Two `for`s over one stream are two LOOP nodes, so each is
            // the consumer of its own read and the pair is refused below.
            bool consumed = false;
            for (const auto &[builder, loop] : carriers) {
                if (builder == reader.graph && reader.node == loop) {
                    consumers.emplace_back(builder, loop);
                    consumed = true;
                    break;
                }
            }
            // A read standing in a round of the value the round carries is the loop's read as well:
            // the round takes the value out of its port and hands it back as the next state, so the
            // pull of a `for` and the state it assembles are that loop's one read, not a round's own
            // work. This is the same test the round rule makes (the round's ports carry the value).
            for (size_t at = 0; !consumed && at < around.size(); ++at) {
                const RegionStep &step = around[at];
                if (!step.round) {
                    continue;
                }
                const auto ports = draft.regionPortsOf(step.region);
                const bool carries =
                    std::any_of(ports.begin(), ports.end(), [&](draft_node_ref_t port) {
                        return findRoot(draft.ref(port)) == identity;
                    });
                if (carries) {
                    consumers.emplace_back(reader.graph, step.owner);
                    consumed = true;
                }
            }
            // A read made in the loop's own region, outside the rounds, is the loop's read: the
            // state assembly of a machine is built where the loop is wired up.
            if (!inRound && !consumed) {
                const GCRegion &home = draft.region(draft.nodeRegion(reader.node));
                for (const auto &[builder, loop] : carriers) {
                    if (builder != reader.graph) {
                        continue;
                    }
                    if (home.owner == loop ||
                        std::any_of(around.begin(), around.end(), [&](const RegionStep &step) {
                            return step.owner == loop;
                        })) {
                        consumers.emplace_back(builder, loop);
                        consumed = true;
                        break;
                    }
                }
            }
            // Failing that, the state a node assembles says which loop's assembly it belongs to: a
            // node of the state type of a loop that carries the value is part of that loop's state.
            if (!consumed) {
                Type *readerType = nodeTypeOf(draft.ref(reader.node));
                if (readerType != nullptr) {
                    const std::string readerTypeText = readerType->toString();
                    for (const auto &[builder, loop] : carriers) {
                        const auto &loopDraft = facts[builder].transaction->graph();
                        Type *loopType        = nodeTypeOf(loopDraft.ref(loop));
                        if (loopType != nullptr && loopType->toString() == readerTypeText) {
                            consumers.emplace_back(builder, loop);
                            consumed = true;
                            break;
                        }
                    }
                }
            }
            // Failing that, following the reading node's inputs reaches the loop it is wired into.
            if (!consumed) {
                std::vector<draft_node_ref_t> frontier{reader.node};
                std::vector<draft_node_ref_t> seen{reader.node};
                for (int depth = 0; depth < 4 && !frontier.empty() && !consumed; ++depth) {
                    std::vector<draft_node_ref_t> next;
                    for (draft_node_ref_t at : frontier) {
                        for (const auto &[builder, loop] : carriers) {
                            if (builder == reader.graph && at != reader.node && at == loop) {
                                consumers.emplace_back(builder, loop);
                                consumed = true;
                                break;
                            }
                        }
                        if (consumed) {
                            break;
                        }
                        const auto walk = [&](draft_node_ref_t from) {
                            if (from < static_cast<draft_node_ref_t>(draft.nodeSlotCount()) &&
                                std::find(seen.begin(), seen.end(), from) == seen.end()) {
                                seen.push_back(from);
                                next.push_back(from);
                            }
                        };
                        for (draft_node_ref_t from : draft.normInputsOf(at)) {
                            walk(from);
                        }
                        for (draft_node_ref_t from : draft.withInputsOf(at)) {
                            walk(from);
                        }
                    }
                    frontier = std::move(next);
                }
            }
            if (!consumed) {
                consumers.emplace_back(reader.graph, reader.node);
            }
        }
        // The read to report: a read of a closure's graph is not a place the writer wrote, so the
        // report points at the value's own read outside it when that is where the mistake is.
        const auto reportedOrigin = [&](size_t at) {
            const Reader &reader = list[at];
            const auto &draft = facts[reader.graph].transaction->graph();
            const auto origin = draft.nodeOrigin(reader.node);
            if (origin != 0) {
                return origin;
            }
            for (size_t other = 0; other < list.size(); ++other) {
                const auto &otherDraft = facts[list[other].graph].transaction->graph();
                const auto otherOrigin = otherDraft.nodeOrigin(list[other].node);
                if (otherOrigin != 0) {
                    return otherOrigin;
                }
            }
            return static_cast<camel::source::origin_id_t>(0);
        };
        const auto record = [&](StreamViolation::Kind kind,
                                size_t at,
                                const char *relatedText,
                                size_t related) {
            StreamViolation violation;
            violation.kind        = kind;
            violation.graph       = list[at].graph;
            violation.origin      = reportedOrigin(at);
            violation.relatedText = relatedText != nullptr ? relatedText : "";
            violation.related     = relatedText != nullptr ? reportedOrigin(related) : 0;
            violation.what        = what;
            streamViolations_.push_back(std::move(violation));
        };
        // A read in a loop's round region is a read per round: the ports of the region are the
        // values a round starts from (the element and the states), so only a value the loop itself
        // carries may be read there.
        //
        // A value the round itself made is the round's own: every round has a new one of those, so
        // reading it in the round it was made in is not reading one value once per round. The rest
        // of the stream a pull answers is threaded into the next state this way -- the pull makes
        // it, and the state assembly reads it.
        const auto madeInTheRound = [&](DraftGraphBuilder *graph, const RegionStep &step) {
            const node_handle_t value = valueOf[identity];
            const auto known          = facts.find(graph);
            if (value == nullptr || known == facts.end()) {
                return false;
            }
            const auto &draft = known->second.transaction->graph();
            // A region id says nothing across graphs: the value must be a node of this one.
            if (nodeTransactionOf(value) != known->second.transaction) {
                return false;
            }
            uint32_t at            = static_cast<uint32_t>(draft.nodeRegion(nodeIdOf(value)));
            const uint32_t regions = static_cast<uint32_t>(draft.regionCount());
            for (uint32_t seen = 0; at != 0 && at < regions && seen < 64; ++seen) {
                const GCRegion &region = draft.region(at);
                if ((region.kind == GCRegionKind::LoopCond ||
                     region.kind == GCRegionKind::LoopBody) &&
                    region.owner == step.owner) {
                    return true;
                }
                at = region.parent;
            }
            return false;
        };
        bool refused = false;
        for (size_t i = 0; i < list.size() && !refused; ++i) {
            for (const RegionStep &step : steps[i]) {
                if (!step.round) {
                    continue;
                }
                const auto &draft  = facts[list[i].graph].transaction->graph();
                const auto ports   = draft.regionPortsOf(step.region);
                const auto carried = std::find_if(
                    ports.begin(), ports.end(), [&](draft_node_ref_t port) {
                        return findRoot(draft.ref(port)) == identity;
                    });
                if (carried != ports.end()) {
                    continue;
                }
                if (madeInTheRound(list[i].graph, step)) {
                    continue;
                }
                // The loop reading its own state is the loop's read, not a round's.
                const bool ownState =
                    std::any_of(carriers.begin(), carriers.end(), [&](const auto &carrier) {
                        return carrier.second == step.owner;
                    });
                if (ownState) {
                    continue;
                }
                record(StreamViolation::Kind::ConsumedInLoop, i, "the loop this use stands in", 0);
                refused = true;
                break;
            }
        }
        if (refused) {
            continue;
        }
        for (size_t i = 0; i < list.size() && !refused; ++i) {
            for (size_t j = i + 1; j < list.size() && !refused; ++j) {
                if (consumers[i] == consumers[j] || apart(steps[i], steps[j])) {
                    continue;
                }
                // The later read is the second consumer, and the first is where the stream was read
                // before it.
                record(
                    StreamViolation::Kind::ConsumedTwice,
                    j,
                    "the stream is consumed here first",
                    i);
                refused = true;
            }
        }
    }
}

void Builder::reportStreamLinearity() {
    // The report runs with the other diagnostics of the module being reported (program.cpp): a
    // violation is named where the read is written, and what it points at is named with it. The
    // first violation of this module ends its report, as a mistake that ends a build does.
    for (const StreamViolation &violation : streamViolations_) {
        if (violation.graph == nullptr ||
            std::find_if(
                unit_->builtBodies.begin(),
                unit_->builtBodies.end(),
                [&](const compile_graph_ptr_t &graph) { return graph.get() == violation.graph; }) ==
                unit_->builtBodies.end()) {
            continue;
        }
        DiagnosticBuilder builder = diags_->of(SemanticDiag::StreamConsumedTwice);
        switch (violation.kind) {
        case StreamViolation::Kind::Captured:
            builder = diags_->of(SemanticDiag::StreamCaptured);
            break;
        case StreamViolation::Kind::ConsumedInLoop:
            builder = diags_->of(SemanticDiag::StreamConsumedInLoop);
            break;
        case StreamViolation::Kind::ConsumedTwice:
            break;
        }
        builder.atOrigin(violation.origin);
        if (!violation.relatedText.empty()) {
            builder.relatedOrigin(violation.related, violation.relatedText);
        }
        builder.commit(violation.what);
        throw BuildAbortException();
    }
}

} // namespace camel::compile::gir