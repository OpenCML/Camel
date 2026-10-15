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
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * TEST-ONLY checks of the graph facts (see facts.h).
 */

#include "facts.h"

#include "camel/core/context/context.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/base.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/facts.h"
#include "camel/runtime/loop_lowering.h"
#include "camel/runtime/reachable.h"

#include <format>
#include <map>
#include <unordered_map>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace camel::runtime;
using namespace camel::runtime::fact;

/// The kinds of this test, of the owner `facttest`, registered once per process.
struct TestKinds {
    KindId kind, nodecl, cone, target, content, pair;
};

/// The kind of node a node is, as an int: a value any node has.
std::optional<int> nodeKind(std::span<const Anchor> anchors) {
    return static_cast<int>(anchors[0].graph->node(anchors[0].node)->kind);
}

std::string printInt(const int &value) { return std::to_string(value); }

const TestKinds &testKinds() {
    static const TestKinds kinds = [] {
        TestKinds k{};
        k.kind = registerKind(KindSpec<int>{
            .key     = "facttest.kind",
            .owner   = "facttest",
            .compute = nodeKind,
            .print   = printInt,
        });
        k.nodecl = registerKind(KindSpec<int>{
            .key               = "facttest.nodecl",
            .owner             = "facttest",
            .readsDeclarations = false,
            .compute           = nodeKind,
            .print             = printInt,
        });
        k.cone = registerKind(KindSpec<int>{
            .key        = "facttest.cone",
            .owner      = "facttest",
            .dependency = Dependency::Cone,
            .compute    = nodeKind,
            .print      = printInt,
            // compute reads the node's kind only: that is its whole dependency cone.
            .cone = [](std::span<const Anchor> anchors) -> uint64_t {
                return 0x100u + static_cast<uint64_t>(anchors[0].graph->node(anchors[0].node)->kind);
            },
        });
        k.target = registerKind(KindSpec<int>{
            .key   = "facttest.target",
            .owner = "facttest",
            .cls   = FactClass::Target,
            .print = printInt,
        });
        k.content = registerKind(KindSpec<int>{
            .key   = "facttest.content",
            .owner = "facttest",
            .cls   = FactClass::GraphContent,
            .scope = Scope::Graph,
            .compute = [](std::span<const Anchor> anchors) -> std::optional<int> {
                return static_cast<int>(anchors[0].graph->nodeCount());
            },
            .print = printInt,
        });
        k.pair = registerKind(KindSpec<std::string>{
            .key   = "facttest.pair",
            .owner = "facttest",
            .scope = Scope::CrossGraph,
            .print = [](const std::string &s) { return s; },
        });
        return k;
    }();
    return kinds;
}

/// Runs `body` and prints `FACTS <check> accepted` or `... rejected: <why>`.
template <typename Body> void attempt(std::ostream &os, std::string_view check, Body &&body) {
    try {
        body();
        os << "FACTS " << check << " accepted\n";
    } catch (const std::invalid_argument &e) {
        os << "FACTS " << check << " rejected: " << e.what() << "\n";
    }
}

void registration(std::ostream &os) {
    const auto print = [](const int &v) { return std::to_string(v); };
    attempt(os, "key_without_owner", [&] {
        registerKind(KindSpec<int>{.key = "facttest", .owner = "facttest", .print = print});
    });
    attempt(os, "key_of_the_core", [&] {
        registerKind(KindSpec<int>{.key = "core.mine", .owner = "facttest", .print = print});
    });
    attempt(os, "tiles_prefix_by_another_owner", [&] {
        registerKind(KindSpec<int>{.key = "tiles.mine", .owner = "facttest", .print = print});
    });
    // How a module registers a kind of its own: the module tiles, a fact of class (iii) (a
    // layout depends on the device), computed by the module's own analysis and stored with
    // the hash of its target.
    attempt(os, "tiles_kind_by_tiles", [&] {
        registerKind(KindSpec<std::string>{
            .key   = "tiles.example_layout",
            .owner = "tiles",
            .cls   = FactClass::Target,
            .print = [](const std::string &s) { return s; },
        });
    });
    attempt(os, "no_print", [&] {
        registerKind(KindSpec<int>{.key = "facttest.noprint", .owner = "facttest"});
    });
    attempt(os, "content_without_compute", [&] {
        registerKind(KindSpec<int>{
            .key   = "facttest.content2",
            .owner = "facttest",
            .cls   = FactClass::GraphContent,
            .scope = Scope::Graph,
            .print = print,
        });
    });
    attempt(os, "target_persisted", [&] {
        registerKind(KindSpec<int>{
            .key    = "facttest.target2",
            .owner  = "facttest",
            .cls    = FactClass::Target,
            .print  = print,
            .encode = print,
            .decode = [](std::string_view) -> std::optional<int> { return 0; },
        });
    });
    attempt(os, "encode_without_decode", [&] {
        registerKind(KindSpec<int>{
            .key = "facttest.half", .owner = "facttest", .print = print, .encode = print});
    });
    attempt(os, "cone_without_hash", [&] {
        registerKind(KindSpec<int>{
            .key        = "facttest.nocone",
            .owner      = "facttest",
            .dependency = Dependency::Cone,
            .print      = print,
        });
    });
    const KindId again = registerKind(KindSpec<int>{
        .key = "facttest.kind", .owner = "facttest", .compute = nodeKind, .print = printInt});
    os << "FACTS register_again " << (again == testKinds().kind ? "same id" : "another id") << "\n";
    attempt(os, "register_again_other_version", [&] {
        registerKind(KindSpec<int>{
            .key = "facttest.kind", .owner = "facttest", .version = 2, .print = printInt});
    });
}

GCGraph *named(GCGraph *root, std::string_view name) {
    for (GCGraph *graph : collectReachableGraphs(root)) {
        if (graph->name() == name) {
            return graph;
        }
    }
    throw std::runtime_error(std::format("test::facts needs a graph named '{}'", name));
}

/// `FACTS <check> <status>` for the fact of `kind` at `anchors`.
void probe(std::ostream &os, FactStore &store, std::string_view check, KindId kind,
           std::vector<Anchor> anchors, uint64_t target = 0) {
    os << "FACTS " << check << " " << statusName(store.probe(kind, anchors, target)) << "\n";
}

} // namespace

GCGraph *FactStoreTestPass::apply(GCGraph *graph, std::ostream &os) {
    const TestKinds &k = testKinds();
    FactStore &store   = context_->facts();
    registration(os);

    GCGraph *root = graph;
    GCGraph *main = named(root, "main");
    GCGraph *f    = named(root, "f");

    // ---- Storing and reading --------------------------------------------------------------
    store.put<int>(k.kind, Anchor{main, 0}, 11);
    os << "FACTS get " << *store.get<int>(k.kind, Anchor{main, 0}) << "\n";
    attempt(os, "get_as_another_type", [&] { (void)store.get<double>(k.kind, Anchor{main, 0}); });
    attempt(os, "node_fact_on_a_graph", [&] { store.put<int>(k.kind, Anchor{main}, 1); });
    attempt(os, "node_out_of_range", [&] { store.put<int>(k.kind, Anchor{main, 60000}, 1); });
    attempt(os, "store_graph_content", [&] { store.put<int>(k.content, Anchor{main}, 1); });
    os << "FACTS content " << (*store.get<int>(k.content, Anchor{main}) == main->nodeCount() ? "is the graph's" : "differs") << "\n";
    attempt(os, "target_without_hash", [&] { store.put<int>(k.target, Anchor{main, 0}, 1); });
    store.put<int>(k.target, Anchor{main, 0}, 5, /*targetHash=*/7);
    probe(os, store, "target_same", k.target, {Anchor{main, 0}}, 7);
    probe(os, store, "target_other", k.target, {Anchor{main, 0}}, 8);
    store.put<std::string>(k.pair, std::vector<Anchor>{Anchor{main, 0}, Anchor{f}}, "main.0 ~ f");
    probe(os, store, "cross_graph", k.pair, {Anchor{main, 0}, Anchor{f}});
    probe(os, store, "cross_graph_other_anchors", k.pair, {Anchor{main, 1}, Anchor{f}});

    // The validator recomputes and compares.
    store.put<int>(k.kind, Anchor{main, 1}, 999);
    const auto wrong = store.verify(k.kind, std::vector<Anchor>{Anchor{main, 1}});
    os << "FACTS verify_wrong " << (wrong ? (*wrong ? "equal" : "differs") : "none") << "\n";
    (void)store.obtain<int>(k.kind, Anchor{main, 2});
    const auto right = store.verify(k.kind, std::vector<Anchor>{Anchor{main, 2}});
    os << "FACTS verify_right " << (right ? (*right ? "equal" : "differs") : "none") << "\n";

    // ---- A commit that asks to edit main and changes nothing ------------------------------
    store.put<int>(k.kind, Anchor{root, 0}, 1);
    (void)store.obtain<int>(k.kind, Anchor{f, 0});
    (void)store.obtain<int>(k.cone, Anchor{main, 0});
    {
        RuntimeGraphDraftSession session(context_, root);
        (void)session.edit(main);
        GCGraph *same = session.commit();
        os << "FACTS unchanged_serial_main "
           << (named(same, "main")->serial() == main->serial() ? "kept" : "new") << "\n";
        probe(os, store, "unchanged_main", k.kind, {Anchor{named(same, "main"), 0}});
        root = same;
        main = named(root, "main");
        f    = named(root, "f");
    }

    // ---- A commit that changes main only (one unused constant more) -----------------------
    RuntimeGraphDraftSession session(context_, root);
    (void)session.edit(main).materializeStaticValue(
        camel::core::rtdata::toSlot<int64_t>(7), camel::core::type::Type::Int64());
    GCGraph *root2 = session.commit();
    GCGraph *main2 = named(root2, "main");
    GCGraph *f2    = named(root2, "f");
    os << "FACTS lineage_main " << (main2->lineage() == main->lineage() ? "kept" : "new") << "\n";
    os << "FACTS serial_main " << (main2->serial() == main->serial() ? "kept" : "new") << "\n";
    os << "FACTS serial_f " << (f2->serial() == f->serial() ? "kept" : "new") << "\n";
    os << "FACTS serial_root " << (root2->serial() == root->serial() ? "kept" : "new") << "\n";
    probe(os, store, "after_commit_main", k.kind, {Anchor{main2, 0}});
    probe(os, store, "after_commit_root", k.kind, {Anchor{root2, 0}});
    probe(os, store, "after_commit_f", k.kind, {Anchor{f2, 0}});
    probe(os, store, "after_commit_cross_graph", k.pair, {Anchor{main2, 0}, Anchor{f2}});
    probe(os, store, "after_commit_cone", k.cone, {Anchor{main2, 0}});

    // ---- A new operator declaration -------------------------------------------------------
    (void)store.obtain<int>(k.nodecl, Anchor{f2, 0});
    namespace sem = camel::core::sem;
    camel::core::OperatorTable::instance().declareSemantics(
        "facttest:op",
        sem::OperatorSemantics{
            .key = sem::OperatorKey::of("facttest:op", 1, 0),
            .numeric =
                sem::NumericSemantics{
                    .cls         = sem::NumericClass::CorrectlyRounded,
                    .compute     = camel::core::type::TypeCode::Float32,
                    .sensitiveTo = sem::kReassociate | sem::kFiniteMath,
                    .algorithm   = "a test operator",
                },
        });
    probe(os, store, "after_declaration_reads_declarations", k.kind, {Anchor{f2, 0}});
    probe(os, store, "after_declaration_reads_none", k.nodecl, {Anchor{f2, 0}});

    // ---- Dropping ---------------------------------------------------------------------------
    (void)store.obtain<int>(k.kind, Anchor{f2, 1});
    store.invalidate(k.kind, std::vector<Anchor>{Anchor{f2, 1}});
    probe(os, store, "invalidated", k.kind, {Anchor{f2, 1}});
    (void)store.obtain<int>(k.kind, Anchor{f2, 1});
    store.invalidateGraph(f2);
    probe(os, store, "invalidated_graph", k.kind, {Anchor{f2, 1}});

    const Counts c = store.counts();
    os << std::format(
        "FACTS counts puts={} hits={} misses={} stale={} revalidated={} computed={}\n",
        c.puts, c.hits, c.misses, c.stale, c.revalidated, c.computed);
    return root2;
}

// ---- Seed and check: facts across other passes ----------------------------------------------

namespace {

/// The graphs of the program of `root`, by lineage.
std::unordered_map<uint64_t, GCGraph *> graphsByLineage(GCGraph *root) {
    std::unordered_map<uint64_t, GCGraph *> byLineage;
    for (GCGraph *graph : collectReachableGraphs(root)) {
        byLineage.emplace(graph->lineage(), graph);
    }
    return byLineage;
}

/// Reads every stored fact against the program of `root`, prints the tallies per graph (lines
/// `FACTS <prefix> <graph> valid=.. revalidated=.. stale=..`) and returns how many facts are
/// about graphs the program does not have.
size_t tally(std::ostream &os, FactStore &store, GCGraph *root, std::string_view prefix) {
    const auto byLineage = graphsByLineage(root);
    // Two graphs of one program never share a lineage (a commit copies each graph once).
    for (GCGraph *graph : collectReachableGraphs(root)) {
        if (byLineage.at(graph->lineage()) != graph) {
            os << "FACTS " << prefix << " shared lineage " << graph->name() << " "
               << byLineage.at(graph->lineage())->name() << "\n";
        }
    }
    struct Tally {
        size_t valid = 0, revalidated = 0, stale = 0;
    };
    std::map<std::string, Tally> perGraph;
    size_t gone = 0;
    std::vector<StoredFact> stored;
    store.forEach([&](const StoredFact &fact) { stored.push_back(fact); });
    for (const StoredFact &fact : stored) {
        std::vector<Anchor> anchors;
        for (const auto &[lineage, node] : fact.anchors) {
            if (auto it = byLineage.find(lineage); it != byLineage.end()) {
                anchors.push_back(Anchor{it->second, node});
            }
        }
        if (anchors.size() != fact.anchors.size()) {
            ++gone;
            continue;
        }
        Tally &t = perGraph[anchors[0].graph->name()];
        switch (store.probe(fact.kind, anchors, fact.stamp.targetHash)) {
        case Status::Valid:
            ++t.valid;
            break;
        case Status::Revalidated:
            ++t.revalidated;
            break;
        default:
            ++t.stale;
            break;
        }
    }
    for (const auto &[name, t] : perGraph) {
        os << std::format(
            "FACTS {} {} valid={} revalidated={} stale={}\n", prefix, name, t.valid, t.revalidated, t.stale);
    }
    return gone;
}

} // namespace

GCGraph *FactSeedTestPass::apply(GCGraph *graph, std::ostream &os) {
    FactStore &store = context_->facts();
    computeFacts(store, graph);
    std::map<std::string, size_t> perKind;
    store.forEach([&](const StoredFact &fact) { ++perKind[kindInfo(fact.kind).key]; });
    for (const auto &[kind, n] : perKind) {
        os << "FACTS seeded " << kind << " " << n << "\n";
    }
    return graph;
}

GCGraph *FactCheckTestPass::apply(GCGraph *graph, std::ostream &os) {
    FactStore &store = context_->facts();
    const size_t gone = tally(os, store, graph, "graph");
    os << "FACTS gone " << gone << "\n";

    // What was stale is computed again; then every stored fact must equal a fresh computation.
    const Counts before = store.counts();
    computeFacts(store, graph);
    os << "FACTS recomputed " << store.counts().computed - before.computed << "\n";
    const auto byLineage = graphsByLineage(graph);
    size_t equal = 0, differ = 0;
    std::vector<StoredFact> stored;
    store.forEach([&](const StoredFact &fact) { stored.push_back(fact); });
    for (const StoredFact &fact : stored) {
        std::vector<Anchor> anchors;
        for (const auto &[lineage, node] : fact.anchors) {
            if (auto it = byLineage.find(lineage); it != byLineage.end()) {
                anchors.push_back(Anchor{it->second, node});
            }
        }
        if (anchors.size() == fact.anchors.size()) {
            if (const auto same = store.verify(fact.kind, anchors)) {
                ++(*same ? equal : differ);
            }
        }
    }
    os << std::format("FACTS verify equal={} differ={}\n", equal, differ);

    // A back end's private lowering: a copy of every graph, new only where a loop was.
    if (programHasLoops(graph)) {
        GCGraph *lowered = lowerLoopsToTailRecursion(context_, graph);
        (void)tally(os, store, lowered, "lowered");
    }
    return graph;
}
