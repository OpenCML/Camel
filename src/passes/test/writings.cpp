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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */


/*
 * Test-only check of WritingStore and DraftWritings: prints "WRITINGS <name> ok" for each property that holds and
 * "WRITINGS <name> FAIL <why>" for each that does not. The program itself is returned unchanged.
 */
#include "camel/passes/test/writings.h"

#include "camel/core/operator_table.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/program.h"
#include "camel/runtime/writings.h"

#include <array>
#include <bit>
#include <format>
#include <stdexcept>
#include <string>

using namespace camel::runtime;

namespace {

struct Report {
    std::ostream &os;
    void check(const char *name, bool ok, const std::string &why = "") {
        os << "WRITINGS " << name << (ok ? " ok" : " FAIL " + why) << "\n";
    }
};

void unionFind(Report &r) {
    WritingStore store;
    r.check("merge_new", store.merge(5, 3) && store.merge(3, 9) && !store.merge(9, 5));
    r.check("root_is_smallest", store.find(0, 9) == 3 && store.find(0, 5) == 3);
    const auto members = store.members(0, 9);
    r.check(
        "members_ascending",
        members.size() == 3 && members[0] == 3 && members[1] == 5 && members[2] == 9);
    r.check("alone", store.find(0, 7) == 7 && store.members(0, 7).empty());
    // The same classes whatever the order of the merges.
    WritingStore other;
    other.merge(9, 3);
    other.merge(5, 9);
    other.merge(3, 5);
    r.check(
        "order_free",
        other.find(0, 5) == 3 && other.members(0, 5).size() == 3 &&
            other.classRoots(0) == std::vector<gc_node_ref_t>{3});
    // A log gives back the classes.
    WritingStore again(store.log());
    r.check("from_log", again.find(0, 9) == 3 && again.members(0, 3).size() == 3);
}

void layers(Report &r) {
    WritingStore store;
    store.merge(1, 2, 0);
    store.merge(2, 3, 0x01);
    store.merge(7, 8, 0x02);
    // A context with no permission sees only the exact equivalence.
    r.check("strict_context", store.same(0, 1, 2) && !store.same(0, 2, 3));
    r.check("granted_context", store.same(0x01, 1, 3) && !store.same(0x01, 7, 8));
    r.check("two_grants", store.same(0x03, 1, 3) && store.same(0x03, 7, 8));
    // A layer made after the merges has the same classes as one made before.
    WritingStore late;
    late.merge(1, 2, 0);
    late.merge(2, 3, 0x01);
    late.find(0x01, 1);
    late.merge(3, 4, 0);
    r.check("exact_reaches_layers", late.same(0x01, 1, 4) && late.same(0, 3, 4));
    r.check("new_flag", !late.merge(1, 4, 0x01) && late.merge(10, 11, 0x01));
    WritingStore many;
    bool refused = false;
    try {
        for (uint8_t context : {0, 1, 2, 4, 8}) {
            many.find(context, 0);
        }
    } catch (const std::logic_error &) {
        refused = true;
    }
    r.check("layer_limit", refused);
}

void deep(Report &r) {
    WritingStore store;
    constexpr gc_node_ref_t n = 100000;
    for (gc_node_ref_t i = n; i > 1; --i) {
        store.merge(i, i - 1);
    }
    r.check("long_chain", store.find(0, n) == 1 && store.members(0, 1).size() == n);
}

std::vector<gc_node_ref_t> portsOf(const GraphDraft &draft) {
    std::vector<gc_node_ref_t> ports;
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (draft.alive(id) && draft.header(id)->kind == GCNodeKind::Port) {
            ports.push_back(id);
        }
    }
    return ports;
}

const GCGraph *graphNamed(const Program &program, const std::string &name) {
    for (const GCGraph *graph : program.graphs()) {
        if (graph->name() == name) {
            return graph;
        }
    }
    return nullptr;
}

/// The writings of `a + 1`, written twice, and what merging two parameters makes of the rest.
void congruence(Report &r, const Program &program) {
    const GCGraph *graph = graphNamed(program, "f");
    if (graph == nullptr) {
        return;
    }
    auto draft = GraphDraft::decode(program, graph);
    DraftWritings writings(*draft);
    writings.build();
    const auto ports = portsOf(*draft);
    r.check("params_are_two", ports.size() == 2);
    const size_t before = writings.congruentMerges();
    r.os << "WRITINGS info build_merges=" << before << "\n";
    r.check("build_merged_repeats", before >= 1);
    // p * q + r: after a = b, p and q are one value, so p * q is p * p.
    size_t classesBefore = writings.store().classRoots(0).size();
    writings.assertEqual(ports[0], ports[1]);
    r.os << "WRITINGS info merges_after=" << writings.congruentMerges() << "\n";
    r.check("parameters_joined", writings.find(ports[1]) == writings.find(ports[0]));
    r.check("congruence_follows", writings.congruentMerges() > before);
    r.check("classes_grew", writings.store().classRoots(0).size() > classesBefore);
    // Asking for a writing that exists gives it; for one that does not, adds a node.
    DraftNodeInit init;
    init.kind = GCNodeKind::Cast;
    init.dataType = draft->header(ports[0])->dataType;
    const size_t slots = draft->nodeSlotCount();
    const gc_node_ref_t made = writings.lookupOrAdd(init, std::array{ports[0]});
    const gc_node_ref_t again = writings.lookupOrAdd(init, std::array{ports[1]});
    r.check("lookup_adds_once", made == slots && again == made);
}

/// Nodes that look alike and are not one value.
void apart(Report &r, const Program &program) {
    const GCGraph *graph = graphNamed(program, "g");
    if (graph == nullptr) {
        return;
    }
    auto draft = GraphDraft::decode(program, graph);
    DraftWritings writings(*draft);
    writings.build();
    std::vector<gc_node_ref_t> zeros, events;
    for (gc_node_ref_t id = 0; id < draft->nodeSlotCount(); ++id) {
        const auto *h = draft->header(id);
        if (!draft->alive(id) || h == nullptr) {
            continue;
        }
        if (h->kind == GCNodeKind::Data && h->dataIndex < 0) {
            const double v = std::bit_cast<double>(
                draft->staticSlots()[static_cast<size_t>(-h->dataIndex)]);
            if (v == 0.0) {
                zeros.push_back(id);
            }
        }
        if (h->kind == GCNodeKind::Oper &&
            !camel::core::OperatorTable::instance().isPure(
                reinterpret_cast<const GCOperBody *>(draft->payloadOf(id).data())->oper)) {
            events.push_back(id);
        }
    }
    r.check("two_zeros_found", zeros.size() == 2);
    r.check(
        "zero_signs_apart",
        zeros.size() == 2 && writings.find(zeros[0]) != writings.find(zeros[1]));
    r.check("two_events_found", events.size() >= 2);
    bool apart = true;
    for (size_t i = 0; i < events.size(); ++i) {
        for (size_t j = i + 1; j < events.size(); ++j) {
            apart = apart && writings.find(events[i]) != writings.find(events[j]);
        }
    }
    r.check("effects_apart", apart);
}

/// The equivalences go through a commit and come back from a decode; a commit that adds none
/// changes nothing; a draft with equivalences refuses to change nodes.
void roundtrip(Report &r, const camel::core::context::context_ptr_t &ctx, const Program &program) {
    const GCGraph *graph = graphNamed(program, "f");
    if (graph == nullptr) {
        graph = graphNamed(program, "chains");
    }
    if (graph == nullptr) {
        return;
    }
    const GraphId id = graph->id();
    RuntimeGraphDraftSession first(ctx, &program);
    GraphDraft &draft = first.edit(const_cast<GCGraph *>(graph));
    {
        DraftWritings writings(draft);
        writings.build();
        const auto ports = portsOf(draft);
        writings.assertEqual(ports[0], ports[1]);
        DraftNodeInit init;
        init.kind     = GCNodeKind::Cast;
        init.dataType = draft.header(ports[0])->dataType;
        GraphDraft::WritingScope scope(draft);
        writings.lookupOrAdd(init, std::array{ports[0]});
        writings.save();
    }
    bool refused = false;
    try {
        draft.eraseNode(portsOf(draft)[0]);
    } catch (const std::logic_error &) {
        refused = true;
    }
    r.check("draft_refuses_changes", refused);
    const Program *tabled = first.commit();
    const GCGraph *after  = tabled->graph(id);
    r.check("commit_makes_new_version", after != nullptr && after != graph);
    r.check("flag_set", after != nullptr && !after->executable() && !after->writings().empty());
    r.check("other_graphs_executable", graph->executable() && program.root()->executable());
    r.check("no_plan", [&] {
        try {
            (void)after->execPlan();
        } catch (const std::logic_error &) {
            return true;
        }
        return false;
    }());
    // Chunks: the new node is at the end, the others are shared.
    const auto *old = graph->nodePayload();
    const auto *now = after->nodePayload();
    size_t shared   = 0;
    for (gc_cnt_t c = 0; c < old->chunkCount && c < now->chunkCount; ++c) {
        shared += old->chunks[c] == now->chunks[c];
    }
    r.os << "WRITINGS info chunks_shared=" << shared << " of " << old->chunkCount << "\n";
    r.check("chunks_shared", shared + 1 >= old->chunkCount);
    // Back into a draft: the same equivalences.
    auto again = GraphDraft::decode(*tabled, after);
    r.check(
        "decode_gives_them_back",
        again->writings().size() == after->writings().size() && !again->writings().empty());
    // Nothing added: the same version.
    RuntimeGraphDraftSession second(ctx, tabled);
    GraphDraft &redraft = second.edit(const_cast<GCGraph *>(after));
    {
        DraftWritings writings(redraft);
        writings.build();
        writings.save();
    }
    const Program *same = second.commit();
    r.check("unchanged_is_same_version", same->graph(id) == after);
    // The table is content: one more equivalence is a new version.
    RuntimeGraphDraftSession third(ctx, tabled);
    GraphDraft &more = third.edit(const_cast<GCGraph *>(after));
    {
        DraftWritings writings(more);
        const auto ports = portsOf(more);
        std::vector<gc_node_ref_t> others;
        for (gc_node_ref_t node = 0; node < more.nodeSlotCount(); ++node) {
            if (more.alive(node) && more.header(node)->dataType == more.header(ports[0])->dataType &&
                more.nodeRegion(node) == more.nodeRegion(ports[0]) && node != ports[0] &&
                !writings.store().same(0, node, ports[0])) {
                others.push_back(node);
            }
        }
        r.check("another_equivalence_exists", !others.empty());
        if (!others.empty()) {
            writings.assertEqual(ports[0], others.front());
            writings.save();
        }
    }
    const Program *grown = third.commit();
    r.check("table_is_content", grown->graph(id) != after);
}

/// A model that likes one node and dislikes the other operators.
class LikesNode : public CostModel {
  public:
    LikesNode(gc_node_ref_t liked, double operCost, double portCost)
        : liked_(liked), operCost_(operCost), portCost_(portCost) {}
    double nodeCost(const CostContext &context, gc_node_ref_t node) const override {
        if (node == liked_) {
            return 1.0;
        }
        const auto kind = context.draft.header(node)->kind;
        return kind == GCNodeKind::Oper ? operCost_ : kind == GCNodeKind::Port ? portCost_ : 1.0;
    }

  private:
    gc_node_ref_t liked_;
    double operCost_;
    double portCost_;
};

gc_node_ref_t firstOperWith(const GraphDraft &draft, std::string_view part, size_t skip = 0) {
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (draft.alive(id) && draft.header(id)->kind == GCNodeKind::Oper) {
            const auto uri =
                reinterpret_cast<const GCOperBody *>(draft.payloadOf(id).data())->uri();
            if (uri.find(part) != uri.npos && skip-- == 0) {
                return id;
            }
        }
    }
    return kInvalidNodeRef;
}

/// `a + 1` written as `1 + a` as well: the value takes the writing the model likes; a value
/// that is one of its own writings (`a * 1`) takes the one that does not read itself. Returns
/// the program with the first extraction committed.
const Program *extraction(
    Report &r, const camel::core::context::context_ptr_t &ctx, const Program &program) {
    const GCGraph *graph = graphNamed(program, "f");
    if (graph == nullptr) {
        return &program;
    }
    const GraphId id = graph->id();
    const Program *extracted = &program;
    {
        RuntimeGraphDraftSession session(ctx, &program);
        GraphDraft &draft = session.edit(const_cast<GCGraph *>(graph));
        DraftWritings writings(draft);
        writings.build();
        const gc_node_ref_t add = firstOperWith(draft, "add");
        const auto inputs       = std::vector<gc_node_ref_t>(
            draft.normInputsOf(add).begin(), draft.normInputsOf(add).end());
        const auto payload = std::vector<std::byte>(
            draft.payloadOf(add).begin(), draft.payloadOf(add).end());
        DraftNodeInit init;
        init.kind        = GCNodeKind::Oper;
        init.dataType    = draft.header(add)->dataType;
        init.permissions = draft.nodePermissions(add);
        init.region      = draft.nodeRegion(add);
        init.payload     = payload;
        const gc_node_ref_t commuted =
            writings.lookupOrAdd(init, std::array{inputs[1], inputs[0]});
        writings.assertEqual(add, commuted);
        r.check("commuted_is_new_writing", commuted != add && writings.find(commuted) == writings.find(add));
        const LikesNode model(commuted, 10.0, 1.0);
        const ExtractStats stats = writings.extract(model, ctx.get());
        r.os << "WRITINGS info extract classes=" << stats.classes << " rewired=" << stats.rewired
             << " erased=" << stats.erased << "\n";
        r.check("extract_took_the_liked_writing", stats.classes >= 1 && stats.erased >= 2);
        r.check("extract_rewired_users", stats.rewired >= 2);
        r.check("extract_is_executable", draft.writings().empty());
        extracted = session.commit();
        r.check(
            "extracted_graph_runs",
            extracted->graph(id) != nullptr && extracted->graph(id)->executable());
    }
    {
        // A value that is one of its own writings: a == a * 1.
        RuntimeGraphDraftSession session(ctx, extracted);
        GraphDraft &draft = session.edit(const_cast<GCGraph *>(extracted->graph(id)));
        DraftWritings writings(draft);
        writings.build();
        const gc_node_ref_t mul = firstOperWith(draft, "mul");
        const auto payload = std::vector<std::byte>(
            draft.payloadOf(mul).begin(), draft.payloadOf(mul).end());
        const auto ports = portsOf(draft);
        gc_node_ref_t one = kInvalidNodeRef;
        for (gc_node_ref_t node = 0; node < draft.nodeSlotCount(); ++node) {
            if (draft.alive(node) && draft.header(node)->kind == GCNodeKind::Data &&
                draft.header(node)->dataType == draft.header(ports[0])->dataType) {
                one = node;
            }
        }
        r.check("one_found", one != kInvalidNodeRef);
        DraftNodeInit init;
        init.kind        = GCNodeKind::Oper;
        init.dataType    = draft.header(ports[0])->dataType;
        init.permissions = draft.nodePermissions(mul);
        init.region      = draft.nodeRegion(mul);
        init.payload     = payload;
        const gc_node_ref_t times = writings.lookupOrAdd(init, std::array{ports[0], one});
        writings.assertEqual(ports[0], times);
        const LikesNode perverse(times, 1.0, 100.0);
        bool threw = false;
        ExtractStats stats;
        try {
            stats = writings.extract(perverse, ctx.get());
        } catch (const std::logic_error &) {
            threw = true;
        }
        r.check("self_reading_writing_excluded", !threw && draft.writings().empty() && stats.erased == 1);
        (void)session.commit();
    }
    return extracted;
}

/// The fact of a value with several writings is the join of the facts of the writings: here
/// the largest node number, over `a + 1` written as `1 + a`.
void facts(Report &r, const camel::core::context::context_ptr_t &ctx, const Program &program) {
    const GCGraph *graph = graphNamed(program, "f");
    if (graph == nullptr) {
        return;
    }
    RuntimeGraphDraftSession session(ctx, &program);
    GraphDraft &draft = session.edit(const_cast<GCGraph *>(graph));
    DraftWritings writings(draft);
    writings.build();
    const gc_node_ref_t add = firstOperWith(draft, "add");
    const auto inputs       = std::vector<gc_node_ref_t>(
        draft.normInputsOf(add).begin(), draft.normInputsOf(add).end());
    const auto payload =
        std::vector<std::byte>(draft.payloadOf(add).begin(), draft.payloadOf(add).end());
    DraftNodeInit init;
    init.kind        = GCNodeKind::Oper;
    init.dataType    = draft.header(add)->dataType;
    init.permissions = draft.nodePermissions(add);
    init.region      = draft.nodeRegion(add);
    init.payload     = payload;
    const gc_node_ref_t commuted = writings.lookupOrAdd(init, std::array{inputs[1], inputs[0]});
    writings.assertEqual(add, commuted);
    // The fact of a writing: its node number (a stand-in for a real analysis); the join keeps
    // the larger.
    const auto on   = [](gc_node_ref_t node) { return static_cast<int>(node); };
    const auto join = [](int a, int b) { return std::max(a, b); };
    const int joined = classFact<int>(writings, add, on, join);
    const gc_node_ref_t port = portsOf(draft)[0];
    r.check("class_fact_is_the_join", joined == static_cast<int>(commuted) && joined > static_cast<int>(add));
    r.check("single_writing_fact_is_its_own", classFact<int>(writings, port, on, join) == static_cast<int>(port));
}

/// Two chains of 300 steps: merging their parameters merges every step.
void chains(Report &r, const Program &program) {
    const GCGraph *graph = graphNamed(program, "chains");
    if (graph == nullptr) {
        return;
    }
    auto draft = GraphDraft::decode(program, graph);
    DraftWritings writings(*draft);
    writings.build();
    const auto ports = portsOf(*draft);
    const size_t before = writings.congruentMerges();
    writings.assertEqual(ports[0], ports[1]);
    r.os << "WRITINGS info chain_merges=" << writings.congruentMerges() - before << "\n";
    r.check("chain_closes", writings.congruentMerges() - before >= 300);
}

} // namespace

const camel::runtime::Program *
WritingsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    Report report{os};
    unionFind(report);
    layers(report);
    deep(report);
    congruence(report, *program);
    roundtrip(report, context_, *program);
    const Program *result = extraction(report, context_, *program);
    facts(report, context_, *program);
    chains(report, *program);
    apart(report, *program);
    return result;
}

const camel::runtime::Program *
TabledTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    (void)os;
    const GCGraph *graph = graphNamed(*program, "f");
    if (graph == nullptr) {
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    GraphDraft &draft = session.edit(const_cast<GCGraph *>(graph));
    DraftWritings writings(draft);
    writings.build();
    const gc_node_ref_t add = firstOperWith(draft, "add");
    const auto inputs       = std::vector<gc_node_ref_t>(
        draft.normInputsOf(add).begin(), draft.normInputsOf(add).end());
    const auto payload =
        std::vector<std::byte>(draft.payloadOf(add).begin(), draft.payloadOf(add).end());
    DraftNodeInit init;
    init.kind        = GCNodeKind::Oper;
    init.dataType    = draft.header(add)->dataType;
    init.permissions = draft.nodePermissions(add);
    init.region      = draft.nodeRegion(add);
    init.payload     = payload;
    const gc_node_ref_t commuted = writings.lookupOrAdd(init, std::array{inputs[1], inputs[0]});
    writings.assertEqual(add, commuted);
    writings.save();
    return session.commit();
}
