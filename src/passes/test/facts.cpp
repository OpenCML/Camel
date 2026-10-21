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
#include "camel/runtime/fact_persist.h"
#include "camel/runtime/facts.h"
#include "camel/runtime/loop_outline.h"
#include "camel/runtime/reachable.h"

#include <algorithm>
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

GCGraph *named(const Program &program, GCGraph *root, std::string_view name) {
    for (GCGraph *graph : collectReachableGraphs(program, root)) {
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

const camel::runtime::Program *
FactStoreTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    camel::runtime::GCGraph *graph = program->root();
    const TestKinds &k = testKinds();
    FactStore &store   = context_->facts();
    registration(os);

    GCGraph *root = graph;
    GCGraph *main = named(*program, root, "main");
    GCGraph *f    = named(*program, root, "f");

    // ---- Storing and reading --------------------------------------------------------------
    store.put<int>(k.kind, Anchor{program, main, 0}, 11);
    os << "FACTS get " << *store.get<int>(k.kind, Anchor{program, main, 0}) << "\n";
    attempt(os, "get_as_another_type", [&] {
        (void)store.get<double>(k.kind, Anchor{program, main, 0});
    });
    attempt(os, "node_fact_on_a_graph", [&] { store.put<int>(k.kind, Anchor{program, main}, 1); });
    attempt(os, "node_out_of_range", [&] {
        store.put<int>(k.kind, Anchor{program, main, 60000}, 1);
    });
    attempt(os, "store_graph_content", [&] {
        store.put<int>(k.content, Anchor{program, main}, 1);
    });
    os << "FACTS content "
       << (*store.get<int>(k.content, Anchor{program, main}) == main->nodeCount() ? "is the graph's"
                                                                                  : "differs")
       << "\n";
    attempt(os, "target_without_hash", [&] {
        store.put<int>(k.target, Anchor{program, main, 0}, 1);
    });
    store.put<int>(k.target, Anchor{program, main, 0}, 5, /*targetHash=*/7);
    probe(os, store, "target_same", k.target, {Anchor{program, main, 0}}, 7);
    probe(os, store, "target_other", k.target, {Anchor{program, main, 0}}, 8);
    store.put<std::string>(
        k.pair,
        std::vector<Anchor>{Anchor{program, main, 0}, Anchor{program, f}},
        "main.0 ~ f");
    probe(os, store, "cross_graph", k.pair, {Anchor{program, main, 0}, Anchor{program, f}});
    probe(
        os,
        store,
        "cross_graph_other_anchors",
        k.pair,
        {Anchor{program, main, 1}, Anchor{program, f}});

    // The validator recomputes and compares.
    store.put<int>(k.kind, Anchor{program, main, 1}, 999);
    const auto wrong = store.verify(k.kind, std::vector<Anchor>{Anchor{program, main, 1}});
    os << "FACTS verify_wrong " << (wrong ? (*wrong ? "equal" : "differs") : "none") << "\n";
    (void)store.obtain<int>(k.kind, Anchor{program, main, 2});
    const auto right = store.verify(k.kind, std::vector<Anchor>{Anchor{program, main, 2}});
    os << "FACTS verify_right " << (right ? (*right ? "equal" : "differs") : "none") << "\n";

    // ---- A commit that asks to edit main and changes nothing ------------------------------
    store.put<int>(k.kind, Anchor{program, root, 0}, 1);
    (void)store.obtain<int>(k.kind, Anchor{program, f, 0});
    (void)store.obtain<int>(k.cone, Anchor{program, main, 0});
    {
        RuntimeGraphDraftSession session(context_, program);
        (void)session.edit(main);
        program       = session.commit();
        GCGraph *same = program->root();
        os << "FACTS unchanged_revision_main "
           << (named(*program, same, "main")->revision() == main->revision() ? "kept" : "new")
           << "\n";
        probe(
            os,
            store,
            "unchanged_main",
            k.kind,
            {Anchor{program, named(*program, same, "main"), 0}});
        root = same;
        main = named(*program, root, "main");
        f    = named(*program, root, "f");
    }

    // ---- A commit that changes main only (one unused constant more) -----------------------
    RuntimeGraphDraftSession session(context_, program);
    (void)session.edit(main).materializeStaticValue(
        camel::core::rtdata::toSlot<int64_t>(7), camel::core::type::Type::Int64());
    const camel::runtime::Program *program2 = session.commit();
    GCGraph *root2                          = program2->root();
    GCGraph *main2                          = named(*program2, root2, "main");
    GCGraph *f2                             = named(*program2, root2, "f");
    os << "FACTS id_main " << (main2->id() == main->id() ? "kept" : "new") << "\n";
    os << "FACTS id_f " << (f2->id() == f->id() ? "kept" : "new") << "\n";
    os << "FACTS id_root " << (root2->id() == root->id() ? "kept" : "new") << "\n";
    os << "FACTS revision_main " << (main2->revision() == main->revision() ? "kept" : "new")
       << "\n";
    os << "FACTS revision_f " << (f2->revision() == f->revision() ? "kept" : "new") << "\n";
    os << "FACTS revision_root " << (root2->revision() == root->revision() ? "kept" : "new")
       << "\n";
    probe(os, store, "after_commit_main", k.kind, {Anchor{program2, main2, 0}});
    probe(os, store, "after_commit_root", k.kind, {Anchor{program2, root2, 0}});
    probe(os, store, "after_commit_f", k.kind, {Anchor{program2, f2, 0}});
    probe(
        os,
        store,
        "after_commit_cross_graph",
        k.pair,
        {Anchor{program2, main2, 0}, Anchor{program2, f2}});
    probe(os, store, "after_commit_cone", k.cone, {Anchor{program2, main2, 0}});

    // ---- A new operator declaration -------------------------------------------------------
    (void)store.obtain<int>(k.nodecl, Anchor{program2, f2, 0});
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
    probe(os, store, "after_declaration_reads_declarations", k.kind, {Anchor{program2, f2, 0}});
    probe(os, store, "after_declaration_reads_none", k.nodecl, {Anchor{program2, f2, 0}});

    // ---- Dropping ---------------------------------------------------------------------------
    (void)store.obtain<int>(k.kind, Anchor{program2, f2, 1});
    store.invalidate(k.kind, std::vector<Anchor>{Anchor{program2, f2, 1}});
    probe(os, store, "invalidated", k.kind, {Anchor{program2, f2, 1}});
    (void)store.obtain<int>(k.kind, Anchor{program2, f2, 1});
    store.invalidateGraph(f2);
    probe(os, store, "invalidated_graph", k.kind, {Anchor{program2, f2, 1}});

    const Counts c = store.counts();
    os << std::format(
        "FACTS counts puts={} hits={} misses={} stale={} revalidated={} computed={}\n",
        c.puts, c.hits, c.misses, c.stale, c.revalidated, c.computed);
    return program2;
}

// ---- Seed and check: facts across other passes ----------------------------------------------

namespace {

/// The graphs of the program of `root`, by GraphId.
std::unordered_map<GraphId, GCGraph *> graphsById(const Program &program, GCGraph *root) {
    std::unordered_map<GraphId, GCGraph *> byId;
    for (GCGraph *graph : collectReachableGraphs(program, root)) {
        byId.emplace(graph->id(), graph);
    }
    return byId;
}

/// Reads every stored fact against the program of `root`, prints the tallies per graph (lines
/// `FACTS <prefix> <graph> valid=.. revalidated=.. stale=..`) and returns how many facts are
/// about graphs the program does not have.
size_t tally(
    std::ostream &os, FactStore &store, const Program &program, GCGraph *root,
    std::string_view prefix) {
    const auto byId = graphsById(program, root);
    // Two graphs of one program never share a GraphId (a commit copies each graph once).
    for (GCGraph *graph : collectReachableGraphs(program, root)) {
        if (byId.at(graph->id()) != graph) {
            os << "FACTS " << prefix << " shared id " << graph->name() << " "
               << byId.at(graph->id())->name() << "\n";
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
        for (const auto &[id, node] : fact.anchors) {
            if (auto it = byId.find(id); it != byId.end()) {
                anchors.push_back(Anchor{&program, it->second, node});
            }
        }
        if (anchors.size() != fact.anchors.size()) {
            ++gone;
            continue;
        }
        Tally &t = perGraph[anchors[0].graph->name()];
        // A node the graph no longer has (a rewrite took it out) is a fact about nothing: stale.
        const bool present = std::ranges::all_of(anchors, [](const Anchor &anchor) {
            return anchor.node == kInvalidNodeRef ||
                   (anchor.graph->containsNodeRef(anchor.node) &&
                    anchor.graph->node(anchor.node) != nullptr);
        });
        if (!present) {
            ++t.stale;
            continue;
        }
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

const camel::runtime::Program *
FactSeedTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    camel::runtime::GCGraph *graph = program->root();
    FactStore &store = context_->facts();
    computeFacts(store, *program);
    std::map<std::string, size_t> perKind;
    store.forEach([&](const StoredFact &fact) { ++perKind[kindInfo(fact.kind).key]; });
    for (const auto &[kind, n] : perKind) {
        os << "FACTS seeded " << kind << " " << n << "\n";
    }
    return program;
}

const camel::runtime::Program *
FactCheckTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    camel::runtime::GCGraph *graph = program->root();
    FactStore &store = context_->facts();
    const size_t gone              = tally(os, store, *program, graph, "graph");
    os << "FACTS gone " << gone << "\n";

    // What was stale is computed again; then every stored fact must equal a fresh computation.
    const Counts before = store.counts();
    computeFacts(store, *program);
    os << "FACTS recomputed " << store.counts().computed - before.computed << "\n";
    const auto byId = graphsById(*program, graph);
    size_t equal = 0, differ = 0;
    std::vector<StoredFact> stored;
    store.forEach([&](const StoredFact &fact) { stored.push_back(fact); });
    for (const StoredFact &fact : stored) {
        std::vector<Anchor> anchors;
        for (const auto &[id, node] : fact.anchors) {
            if (auto it = byId.find(id); it != byId.end()) {
                anchors.push_back(Anchor{program, it->second, node});
            }
        }
        if (anchors.size() == fact.anchors.size()) {
            if (const auto same = store.verify(fact.kind, anchors)) {
                ++(*same ? equal : differ);
            }
        }
    }
    os << std::format("FACTS verify equal={} differ={}\n", equal, differ);

    // The program with its loops as graphs (what autodiff works on): a copy of every graph, new
    // only where a loop was.
    if (const Program *outlinedProgram = outlineLoopRegions(context_, program, graph);
        outlinedProgram != program) {
        GCGraph *outlined = outlinedProgram->root();
        (void)tally(os, store, *outlinedProgram, outlined, "outlined");
    }
    return program;
}

// ---- Persistence ------------------------------------------------------------------------------

const camel::runtime::Program *
FactPersistTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    camel::runtime::GCGraph *graph = program->root();
    FactStore &store = context_->facts();
    computeFacts(store, *program);
    const std::vector<PortableFact> facts = exportFacts(store, *program);
    std::map<std::string, size_t> perKind;
    size_t textRoundTrips = 0;
    for (const PortableFact &fact : facts) {
        ++perKind[fact.kind];
        const auto back = parsePortableFact(toText(fact));
        textRoundTrips += back && *back == fact ? 1 : 0;
    }
    for (const auto &[kind, n] : perKind) {
        os << "FACTS exported " << kind << " " << n << "\n";
    }
    os << std::format("FACTS text round trip {}/{}\n", textRoundTrips, facts.size());

    // Into an empty store, against the same program.
    store.clear();
    std::map<std::string, size_t> results;
    for (const PortableFact &fact : facts) {
        ++results[std::string(importResultName(importFact(store, *program, fact)))];
    }
    for (const auto &[result, n] : results) {
        os << "FACTS imported " << result << " " << n << "\n";
    }
    size_t equal = 0, verified = 0;
    const auto byId = graphsById(*program, graph);
    std::vector<StoredFact> stored;
    store.forEach([&](const StoredFact &fact) { stored.push_back(fact); });
    for (const StoredFact &fact : stored) {
        std::vector<Anchor> anchors;
        for (const auto &[id, node] : fact.anchors) {
            anchors.push_back(Anchor{program, byId.at(id), node});
        }
        if (const auto same = store.verify(fact.kind, anchors)) {
            ++verified;
            equal += *same ? 1 : 0;
        }
    }
    os << std::format("FACTS loaded equal to a fresh computation {}/{}\n", equal, verified);

    // Change main (an unused constant more), then load again.
    RuntimeGraphDraftSession session(context_, program);
    GraphDraft &main = session.edit(named(*program, graph, "main"));
    (void)main.materializeStaticValue(
        camel::core::rtdata::toSlot<int64_t>(7), camel::core::type::Type::Int64());
    const camel::runtime::Program *changedProgram = session.commit();
    GCGraph *changed                              = changedProgram->root();
    store.clear();
    std::map<std::string, std::map<std::string, size_t>> perGraph;
    for (const PortableFact &fact : facts) {
        const ImportResult result = importFact(store, *changedProgram, fact);
        ++perGraph[fact.anchors[0].first][std::string(importResultName(result))];
    }
    for (const auto &[name, counts] : perGraph) {
        for (const auto &[result, n] : counts) {
            os << "FACTS reloaded " << name << " " << result << " " << n << "\n";
        }
    }
    return changedProgram;
}
