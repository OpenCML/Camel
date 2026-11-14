// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Choosing and landing a writing: the cost models, `extract`, and the classes a dump draws.

#include "camel/runtime/writings.h"
#include "writings_internal.h"
#include "camel/runtime/operator_instance.h"
#include "camel/runtime/reachable.h"

#include "camel/core/operator_table.h"
#include "camel/core/tags.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/text.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/draft_writes.h"
#include "camel/runtime/effect_liveness.h"
#include "camel/runtime/graph_txn.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace camel::runtime {

namespace {

std::mutex &costModelsMutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, std::shared_ptr<const CostModel>> &costModels() {
    static std::map<std::string, std::shared_ptr<const CostModel>> models;
    return models;
}

class NodeCountModel : public CostModel {
  public:
    double nodeCost(const CostContext &, gc_node_ref_t) const override { return 1.0; }
};

class WeightedModel : public CostModel {
  public:
    double nodeCost(const CostContext &context, gc_node_ref_t node) const override {
        const auto &draft = context.transaction.graph();
        const auto *h     = draft.header(node);
        switch (h->kind) {
        case GCNodeKind::Data:
        case GCNodeKind::Port:
            return 0.1;
        case GCNodeKind::Cast:
            return 0.5;
        case GCNodeKind::Oper: {
            const std::string_view uri =
                reinterpret_cast<const GCOperBody *>(draft.payloadOf(node).data())->uri();
            auto has = [&](std::string_view name) { return uri.find(name) != uri.npos; };
            if (has("div") || has("mod")) {
                return 8.0;
            }
            if (has("mul")) {
                return 3.0;
            }
            if (has("add") || has("sub") || has("neg")) {
                return 1.0;
            }
            return 2.0;
        }
        default:
            return 2.0;
        }
    }
};

void ensureBuiltinCostModels() {
    static const bool once = [] {
        costModels().emplace("nodes", std::make_shared<NodeCountModel>());
        costModels().emplace("weighted", std::make_shared<WeightedModel>());
        return true;
    }();
    (void)once;
}

} // namespace

void registerCostModel(std::string name, std::shared_ptr<const CostModel> model) {
    std::lock_guard lock(costModelsMutex());
    ensureBuiltinCostModels();
    costModels()[std::move(name)] = std::move(model);
}

std::shared_ptr<const CostModel> findCostModel(const std::string &name) {
    std::lock_guard lock(costModelsMutex());
    ensureBuiltinCostModels();
    auto found = costModels().find(name);
    return found == costModels().end() ? nullptr : found->second;
}

WritingChoice
DraftWritings::choiceByCost(const CostModel &model, camel::core::context::Context *context) {
    rebuild();
    const size_t slots = draft_.nodeSlotCount();
    const CostContext costContext{*transaction_, context};
    constexpr double kInfinite = std::numeric_limits<double>::infinity();

    // The value each node is a writing of, and how many writings the value has.
    WritingChoice choice;
    choice.classOf.assign(slots, kInvalidNodeRef);
    choice.multi.assign(slots, 0);
    choice.chosen.assign(slots, kInvalidNodeRef);
    choice.cost.assign(slots, kInfinite);
    choice.own.assign(slots, 0.0);
    std::vector<double> own(slots, 0.0);
    for (gc_node_ref_t node = 0; node < slots; ++node) {
        if (!draft_.alive(node)) {
            continue;
        }
        const camel::core::sem::PermissionMask perms = contextOf(node);
        choice.classOf[node]                         = store_.canon(perms, node);
        choice.multi[node]                           = store_.members(perms, node).empty() ? 0 : 1;
        own[node]                                    = model.nodeCost(costContext, node);
        if (!(own[node] > 0.0)) {
            throw std::logic_error("A cost model gives every node a positive cost.");
        }
    }

    // The cheapest way to compute each value: the cost of a writing is its own plus that of the
    // values it reads. Repeated until nothing gets cheaper; the first writing (smallest node)
    // wins a tie.
    std::vector<double> best(slots, kInfinite);
    std::vector<gc_node_ref_t> chosen(slots, kInvalidNodeRef);
    const auto inputsOf = [&](gc_node_ref_t node) {
        std::vector<gc_node_ref_t> all;
        for (auto part :
             {draft_.normInputsOf(node),
              draft_.withInputsOf(node),
              draft_.ctrlInputsOf(node),
              draft_.syncInputsOf(node)}) {
            all.insert(all.end(), part.begin(), part.end());
        }
        return all;
    };
    bool changed = true;
    for (size_t round = 0; changed && round <= slots; ++round) {
        changed = false;
        for (gc_node_ref_t node = 0; node < slots; ++node) {
            if (!draft_.alive(node)) {
                continue;
            }
            std::vector<double> inputCosts;
            bool known = true;
            for (gc_node_ref_t input : inputsOf(node)) {
                const double cost = best[choice.classOf[input]];
                if (cost == kInfinite) {
                    known = false;
                    break;
                }
                inputCosts.push_back(cost);
            }
            const double total =
                known ? model.totalCost(costContext, node, own[node], inputCosts) : kInfinite;
            if (known && total < best[choice.classOf[node]]) {
                best[choice.classOf[node]]   = total;
                chosen[choice.classOf[node]] = node;
                changed                      = true;
            }
        }
    }
    for (gc_node_ref_t node = 0; node < slots; ++node) {
        if (!draft_.alive(node)) {
            continue;
        }
        choice.chosen[node] = chosen[choice.classOf[node]];
        choice.cost[node]   = best[choice.classOf[node]];
        choice.own[node]    = own[node];
    }
    return choice;
}

ExtractStats
DraftWritings::extract(const CostModel &model, camel::core::context::Context *context) {
    GraphDraft::WritingScope scope(draft_);
    ExtractStats stats;
    const size_t slots                        = draft_.nodeSlotCount();
    const WritingChoice choice                = choiceByCost(model, context);
    const std::vector<gc_node_ref_t> &classOf = choice.classOf;
    const std::vector<uint8_t> &multi         = choice.multi;
    const std::vector<gc_node_ref_t> &chosen  = choice.chosen;
    for (gc_node_ref_t node = 0; node < slots; ++node) {
        if (draft_.alive(node) && multi[node] && classOf[node] == node) {
            ++stats.classes;
        }
    }
    const auto inputsOf = [&](gc_node_ref_t node) {
        std::vector<gc_node_ref_t> all;
        for (auto part :
             {draft_.normInputsOf(node),
              draft_.withInputsOf(node),
              draft_.ctrlInputsOf(node),
              draft_.syncInputsOf(node)}) {
            all.insert(all.end(), part.begin(), part.end());
        }
        return all;
    };

    // What stays: what the graph names (its ports, entry, exit, result), the nodes extraction
    // does not trim, and the writing chosen for every value that is read.
    //
    // Structural anchors and values written in place remain roots. Operators and casts stay
    // when selected writings demand them, including through sync dependencies. Admission
    // already refused every candidate that would drop an external access or a write that
    // outlives the call (9.7), and applied the selected fault guard besides: retaining every
    // effect operator here would restore a fault guard the caller disabled and leave orphan
    // effects without an admission credential.
    // Order edges alone do not demand execution (design principles 3.1).
    std::vector<uint8_t> selected(slots, 0);
    std::vector<gc_node_ref_t> work;
    const auto select = [&](gc_node_ref_t node) {
        if (node != kInvalidNodeRef && draft_.alive(node) && !selected[node]) {
            selected[node] = 1;
            work.push_back(node);
        }
    };
    for (gc_node_ref_t node = 0; node < slots; ++node) {
        if (draft_.alive(node) &&
            (writtenInPlace(node) ||
             (carriesOrdering(draft_, node, true) && !isUnanchoredPure(draft_, node) &&
              draft_.header(node)->kind != GCNodeKind::Oper &&
              draft_.header(node)->kind != GCNodeKind::Cast))) {
            select(node);
        }
    }
    for (auto list : {draft_.normPorts(), draft_.withPorts(), draft_.closureNodes()}) {
        for (gc_node_ref_t node : list) {
            select(node);
        }
    }
    // The nodes the graph names as its entry, exit and result stand for values: the writing
    // chosen for the value takes their place.
    const auto valueOf = [&](gc_node_ref_t node) {
        return node != kInvalidNodeRef && draft_.alive(node) && multi[node] ? chosen[classOf[node]]
                                                                            : node;
    };
    const gc_node_ref_t entry  = valueOf(draft_.entryNode());
    const gc_node_ref_t exit   = valueOf(draft_.exitNode());
    const gc_node_ref_t output = valueOf(draft_.outputNode());
    const gc_node_ref_t result = valueOf(draft_.returnNode());
    for (gc_node_ref_t node : {entry, exit, output, result}) {
        select(node);
    }
    std::vector<gc_node_ref_t> regionResults;
    for (uint32_t r = 0; r < draft_.regionCount(); ++r) {
        regionResults.push_back(valueOf(draft_.region(r).result));
        select(regionResults.back());
        // A control region is entered with all its ports, as a function with all its parameters:
        // a loop condition that reads one state of two still has a port for the other. The ports
        // stay whether or not something in the region reads them (the graph check requires every
        // port of a control region; an annotation region may lose one a rewrite removed).
        const GCRegionKind kind = draft_.region(r).kind;
        if (kind == GCRegionKind::Arm || kind == GCRegionKind::LoopCond ||
            kind == GCRegionKind::LoopBody) {
            for (gc_node_ref_t port : draft_.regionPortsOf(r)) {
                select(port);
            }
        }
    }
    for (size_t at = 0; at < work.size(); ++at) {
        // The other writings of this value that waited for something: the writing that stays waits
        // for it too, in whichever kind the chain of its block is written (a control input, or the
        // sync edge of a `sync` block's chain; docs/design/effects/edges.cn.md 1.5 and 5.3).
        if (multi[work[at]]) {
            for (gc_node_ref_t member : store_.members(contextOf(work[at]), work[at])) {
                for (auto waits : {draft_.ctrlInputsOf(member), draft_.syncInputsOf(member)}) {
                    for (gc_node_ref_t wait : waits) {
                        const gc_node_ref_t pick = chosen[classOf[wait]];
                        if (pick == kInvalidNodeRef) {
                            throw std::logic_error(
                                "A control input has no writing that does not read itself.");
                        }
                        select(pick);
                    }
                }
            }
        }
        for (gc_node_ref_t input : inputsOf(work[at])) {
            const gc_node_ref_t pick = chosen[classOf[input]];
            if (pick == kInvalidNodeRef) {
                throw std::logic_error("A value has no writing that does not read itself.");
            }
            select(pick);
        }
        if (!multi[work[at]] && chosen[classOf[work[at]]] == kInvalidNodeRef) {
            throw std::logic_error(
                "A node reads a value that has no writing that does not read itself.");
        }
    }

    // The nodes that stay read the writings that stay.
    for (gc_node_ref_t node = 0; node < slots; ++node) {
        if (!selected[node]) {
            continue;
        }
        bool rewired    = false;
        const auto pick = [&](std::span<const gc_node_ref_t> inputs) {
            std::vector<gc_node_ref_t> picked(inputs.begin(), inputs.end());
            for (gc_node_ref_t &input : picked) {
                const gc_node_ref_t to = chosen[classOf[input]];
                if (to != input) {
                    input   = to;
                    rewired = true;
                }
            }
            return picked;
        };
        const auto norm = pick(draft_.normInputsOf(node));
        const auto with = pick(draft_.withInputsOf(node));
        auto ctrl       = pick(draft_.ctrlInputsOf(node));
        if (multi[node] && chosen[classOf[node]] == node) {
            for (gc_node_ref_t member : store_.members(contextOf(node), node)) {
                if (member == node) {
                    continue;
                }
                // What the member carries: the wait it has, and whether it sits on a block's chain.
                // A structural reading, written where it is read -- `EffectLivenessFacts` no longer
                // carries a wait field (camel/runtime/effect_liveness.h says why). Only the wait is
                // moved; the member's place on a block's chain is not, so it is not read here
                // either (it used to be assigned and never used, which is one more reason the
                // field was a bag rather than an answer).
                const bool memberCarriesWait = !draft_.ctrlInputsOf(member).empty();
                if (!memberCarriesWait) {
                    continue;
                }
                for (gc_node_ref_t wait : draft_.ctrlInputsOf(member)) {
                    const gc_node_ref_t to = chosen[classOf[wait]];
                    if (to != node && std::find(ctrl.begin(), ctrl.end(), to) == ctrl.end()) {
                        ctrl.push_back(to);
                        rewired = true;
                    }
                }
            }
        }
        if (rewired) {
            draft_.setNormInputs(node, norm);
            draft_.setWithInputs(node, with);
            draft_.setCtrlInputs(node, ctrl);
            ++stats.rewired;
        }
    }
    if (entry != draft_.entryNode()) {
        draft_.setEntryNode(entry);
    }
    if (exit != draft_.exitNode()) {
        draft_.setExitNode(exit);
    }
    if (output != draft_.outputNode()) {
        draft_.setOutputNode(output);
    }
    if (result != draft_.returnNode()) {
        draft_.setReturnNode(result, draft_.returnKind());
    }
    for (uint32_t r = 0; r < draft_.regionCount(); ++r) {
        if (regionResults[r] != draft_.region(r).result) {
            draft_.setRegionResult(r, regionResults[r]);
        }
    }
    for (gc_node_ref_t node = 0; node < slots; ++node) {
        if (draft_.alive(node) && !selected[node]) {
            // Replacing a node hands its sync edge and its order edges to the replacement
            // (docs/design/effects/edges.cn.md 4, the rule for the three kinds): the statements
            // written after the writing being folded away wait for the writing that takes its value
            // instead of losing the edge with the node, and it is entered from where the folded
            // writing stood. The chain place and its neighboring dependencies are handed over by
            // `handChainPlaceTo`, the same call the branch dissolve makes
            // (src/passes/opt/generic/generic.cpp); a node a chain does not hold is erased as it
            // was.
            const gc_node_ref_t replacement = multi[node] ? chosen[classOf[node]] : node;
            if (replacement != node && replacement != kInvalidNodeRef &&
                draft_.alive(replacement) && !draft_.syncUsersOf(node).empty()) {
                draft_.handChainPlaceTo(node, replacement);
            }
            draft_.eraseNode(node);
            ++stats.erased;
        }
    }
    draft_.setWritings({});
    store_ = WritingStore();
    index_ = std::make_unique<Index>();
    pending_.clear();
    queued_.clear();
    return stats;
}

bool hasWritings(const Program &program) {
    for (const GCGraph *graph : program.graphs()) {
        if (!graph->executable()) {
            return true;
        }
    }
    return false;
}

const Program *extractWritings(
    const std::shared_ptr<camel::core::context::Context> &context, const Program *program,
    const CostModel &model, bool detached, GCGraph *root) {
    if (program == nullptr || !hasWritings(*program)) {
        return program;
    }
    auto session = root != nullptr
                       ? std::make_unique<RuntimeGraphDraftSession>(context, program, root)
                       : std::make_unique<RuntimeGraphDraftSession>(context, program);
    // A cost written in Camel runs at a safepoint: every draft keeps its static values relocatable
    // until the commit reads them again.
    RuntimeGraphDraftSession::RootScope draftRoots(*session);
    std::vector<GCGraph *> graphs;
    if (root != nullptr) {
        graphs = collectReachableGraphs(*program, root);
    } else {
        for (GCGraph *graph : program->graphs()) {
            graphs.push_back(graph);
        }
    }
    for (GCGraph *graph : graphs) {
        if (graph->executable()) {
            continue;
        }
        auto &draft = session->edit(graph).edit();
        DraftWritings writings(draft);
        writings.extract(model, context.get());
    }
    return session->commit(detached);
}

gc_node_ref_t DraftWritings::find(gc_node_ref_t node) { return store_.find(contextOf(node), node); }

std::span<const gc_node_ref_t> DraftWritings::writingsOf(gc_node_ref_t node) {
    return store_.members(contextOf(node), node);
}

GraphWritings graphWritings(
    const Program &program, const GCGraph *graph, const CostModel &model,
    camel::core::context::Context *context) {
    GraphWritings classes;
    // The executable state has one writing per value by definition: a dump of such a graph draws
    // no class, and does not pay for a decode of it.
    if (graph == nullptr || graph->executable()) {
        return classes;
    }
    // The graph is a decoded copy, and the transaction is opened on it only to give the writings
    // a face to write on: it is never committed, because the caller gets a printed result and no
    // program is handed back. A commit here would be a lifecycle the dump has no use for.
    const auto transaction = GraphTransaction::decode(program, graph);
    if (transaction == nullptr) {
        return classes;
    }
    auto &draft = transaction->edit();
    // The same policy the driver's extraction uses (`extractWritings`), so the classes and the
    // choice the dump shows are the ones the pipeline would act on.
    DraftWritings writings(*transaction);
    writings.build();
    const WritingChoice choice = writings.choiceByCost(model, context);
    for (gc_node_ref_t node = 0; node < draft.nodeSlotCount(); ++node) {
        if (!draft.alive(node)) {
            continue;
        }
        classes.nodes[node] = GraphWritings::Node{
            .root  = choice.classOf[node],
            .own   = choice.own[node],
            .value = choice.cost[node]};
        // One entry per value, at its root (the smallest writing); the members are ascending.
        if (!choice.multi[node] || choice.classOf[node] != node) {
            continue;
        }
        const auto members = writings.writingsOf(node);
        if (members.size() < 2) {
            continue;
        }
        GraphWritings::Value value;
        value.root = node;
        value.writings.assign(members.begin(), members.end());
        value.chosen          = choice.chosen[node];
        value.cost            = choice.cost[node];
        classes.valueOf[node] = classes.values.size();
        for (gc_node_ref_t member : value.writings) {
            classes.valueOf[member] = classes.values.size();
        }
        classes.values.push_back(std::move(value));
    }
    return classes;
}

} // namespace camel::runtime
