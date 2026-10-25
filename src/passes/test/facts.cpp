// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
#include "camel/runtime/reachable.h"
#include "execute/camel_pass.h"

#include <algorithm>
#include <chrono>
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
    KindId kind, nodecl, cone, target, content, pair, node;
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
        k.node = registerKind(KindSpec<int>{
            .key        = "facttest.node",
            .owner      = "facttest",
            .dependency = Dependency::Node,
            .compute    = nodeKind,
            .print      = printInt,
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

/// Prints the revision of every graph of the program of `root` (`FACTS <prefix> revision <graph>
/// <n>`). A commit keeps a graph's revision unless it changed that graph (graph.h), so comparing
/// these lines before and after a pass says which graphs the pass changed.
void printRevisions(
    std::ostream &os, const Program &program, GCGraph *root, std::string_view prefix) {
    const auto byId = graphsById(program, root);
    for (GCGraph *graph : collectReachableGraphs(program, root)) {
        if (byId.at(graph->id()) != graph) {
            continue;
        }
        os << std::format("FACTS {} revision {} {}\n", prefix, graph->name(), graph->revision());
    }
}

/// Reads every stored fact against the program of `root`, prints the tallies per graph (lines
/// `FACTS <prefix> <graph> valid=.. revalidated=.. stale=..`) and per kind on that graph (lines
/// `FACTS <prefix> kind <graph> <kind> valid=.. revalidated=.. stale=..`), and returns how many
/// facts are about graphs the program does not have. The per-kind lines are how a case says
/// *which* facts a rewrite invalidated, not just how many.
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
    std::map<std::pair<std::string, std::string>, Tally> perKind;
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
        const std::string graphName = anchors[0].graph->name();
        const std::string kindName  = kindInfo(fact.kind).key;
        Tally &t                    = perGraph[graphName];
        Tally &k                    = perKind[{graphName, kindName}];
        const auto bump             = [&](size_t Tally::*field) {
            ++(t.*field);
            ++(k.*field);
        };
        // A node the graph no longer has (a rewrite took it out) is a fact about nothing: stale.
        const bool present = std::ranges::all_of(anchors, [](const Anchor &anchor) {
            return anchor.node == kInvalidNodeRef ||
                   (anchor.graph->containsNodeRef(anchor.node) &&
                    anchor.graph->node(anchor.node) != nullptr);
        });
        if (!present) {
            bump(&Tally::stale);
            continue;
        }
        switch (store.probe(fact.kind, anchors, fact.stamp.targetHash)) {
        case Status::Valid:
            bump(&Tally::valid);
            break;
        case Status::Revalidated:
            bump(&Tally::revalidated);
            break;
        default:
            bump(&Tally::stale);
            break;
        }
    }
    for (const auto &[name, t] : perGraph) {
        os << std::format(
            "FACTS {} {} valid={} revalidated={} stale={}\n", prefix, name, t.valid, t.revalidated, t.stale);
    }
    for (const auto &[key, t] : perKind) {
        os << std::format(
            "FACTS {} kind {} {} valid={} revalidated={} stale={}\n", prefix, key.first, key.second,
            t.valid, t.revalidated, t.stale);
    }
    return gone;
}

} // namespace

const camel::runtime::Program *
FactLoadRulesTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    // The control for the stale-fact measurement: load the library `std::opt`'s algebra stage would load, before any
    // fact exists. The facts seeded later are then stamped with the epoch the load leaves behind,
    // so a later `std::opt` declares nothing new and they stay valid; without this pass they are
    // invalidated by the epoch alone although their graphs did not change.
    //
    // Importing alone is not enough: a module written in Camel has its declarations compiled by
    // whoever compiles it, and a pass named `<module>::<function>` is what makes the executor do
    // that (`registerCamelPass`). `std::opt`'s algebra stage gets the same effect from
    // `resolvePassWithModule("rules::simplify")`; this pass asks for the same compilation without
    // running the pass on the program.
    const auto &table     = camel::core::OperatorTable::instance();
    const uint64_t before = table.declarationEpoch();
    std::string problem;
    camel::execute::registerCamelPass(context_, "rules", "simplify", problem);
    os << std::format(
        "FACTS rules_loaded epoch {} -> {} problem={}\n",
        before,
        table.declarationEpoch(),
        problem.empty() ? 0 : 1);
    return program;
}

const camel::runtime::Program *
FactSeedTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    FactStore &store = context_->facts();
    computeFacts(store, *program);
    std::map<std::string, size_t> perKind;
    store.forEach([&](const StoredFact &fact) { ++perKind[kindInfo(fact.kind).key]; });
    for (const auto &[kind, n] : perKind) {
        os << "FACTS seeded " << kind << " " << n << "\n";
    }
    // The stamp of a kind that reads declarations holds this epoch; comparing it with the check's
    // line says whether a declaration was added between the two passes (docs/design/op-semantics
    // 10.3: the epoch is one reason a fact goes stale even when its graph did not change).
    os << std::format(
        "FACTS epoch seed={}\n", camel::core::OperatorTable::instance().declarationEpoch());
    printRevisions(os, *program, program->root(), "seed");
    return program;
}

const camel::runtime::Program *
FactCheckTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    camel::runtime::GCGraph *graph = program->root();
    FactStore &store = context_->facts();
    os << std::format(
        "FACTS epoch check={}\n", camel::core::OperatorTable::instance().declarationEpoch());
    const size_t gone = tally(os, store, *program, graph, "graph");
    os << "FACTS gone " << gone << "\n";
    printRevisions(os, *program, graph, "check");

    // What was stale is computed again; then every stored fact must equal a fresh computation.
    // The wall time and the number of (re)computed and (re)stored facts say what reading the
    // stale facts costs; the count is the measurement, the time only says its size.
    std::map<std::string, size_t> storedBefore;
    store.forEach([&](const StoredFact &fact) { ++storedBefore[kindInfo(fact.kind).key]; });
    const Counts before = store.counts();
    const auto started   = std::chrono::steady_clock::now();
    computeFacts(store, *program);
    const std::chrono::duration<double, std::micro> elapsed =
        std::chrono::steady_clock::now() - started;
    const Counts after = store.counts();
    std::map<std::string, size_t> storedAfter;
    store.forEach([&](const StoredFact &fact) { ++storedAfter[kindInfo(fact.kind).key]; });
    os << "FACTS recomputed " << after.computed - before.computed << "\n";
    os << std::format(
        "FACTS recompute us={:.1f} calls={} stored={}\n",
        elapsed.count(),
        after.computed - before.computed,
        after.puts - before.puts);
    for (const auto &[kind, n] : storedAfter) {
        const auto was = storedBefore.find(kind);
        const size_t had = was == storedBefore.end() ? 0 : was->second;
        if (n != had) {
            os << std::format("FACTS recomputed kind {} {}\n", kind, n - had);
        }
    }
    const auto byId = graphsById(*program, graph);
    size_t equal = 0, differ = 0;
    // Every fact a consumer could read — one whose graphs and nodes the program still has — must
    // be valid now: what the tally above found stale was recomputed, and a fact about a node or a
    // graph that is gone is not readable (the probe drops it). This is the no-silent-staleness
    // check: after the recompute pass, no readable fact is stale.
    size_t readableValid = 0, readableStale = 0, unreadable = 0;
    std::vector<StoredFact> stored;
    store.forEach([&](const StoredFact &fact) { stored.push_back(fact); });
    for (const StoredFact &fact : stored) {
        std::vector<Anchor> anchors;
        bool present = true;
        for (const auto &[id, node] : fact.anchors) {
            if (auto it = byId.find(id); it != byId.end()) {
                anchors.push_back(Anchor{program, it->second, node});
                const bool nodePresent =
                    node == kInvalidNodeRef ||
                    (it->second->containsNodeRef(node) && it->second->node(node) != nullptr);
                present = present && nodePresent;
            } else {
                present = false;
            }
        }
        if (anchors.size() != fact.anchors.size() || !present) {
            ++unreadable;
            continue;
        }
        const Status status = store.probe(fact.kind, anchors, fact.stamp.targetHash);
        if (status == Status::Valid || status == Status::Revalidated) {
            ++readableValid;
        } else {
            ++readableStale;
        }
        if (const auto same = store.verify(fact.kind, anchors)) {
            ++(*same ? equal : differ);
        }
    }
    os << std::format(
        "FACTS after recompute valid={} stale={} gone={}\n", readableValid, readableStale, unreadable);
    os << std::format("FACTS verify equal={} differ={}\n", equal, differ);
    const Counts total = store.counts();
    // What the store did over this pass: `stale` counts the facts a read found stale (the tally
    // above reads each stored fact once), `computed` the times a computation ran, `puts` the facts
    // stored. These are read beside the epoch pair to say how many facts the conservative rule
    // invalidated and how many were derived again.
    os << std::format(
        "FACTS counts puts={} hits={} misses={} stale={} revalidated={} computed={}\n",
        total.puts,
        total.hits,
        total.misses,
        total.stale,
        total.revalidated,
        total.computed);
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

// ---- Node versions ---------------------------------------------------------------------------

const camel::runtime::Program *
FactNodeTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    FactStore &store   = context_->facts();
    const TestKinds &k = testKinds();
    GCGraph *root      = program->root();
    // The entry node: a node no edit of another node of the graph rewrites, and one whose place
    // the rewrite keeps (the encoder keeps the ids of a graph it rewrites).
    const gc_node_ref_t kept = root->entryNodeRef();
    const Anchor anchor{program, root, kept};
    (void)store.obtain<int>(k.node, anchor);
    (void)store.obtain<int>(k.kind, anchor); // the conservative rule, for the contrast
    const uint64_t version  = root->nodeContentHash(kept);
    const uint64_t revision = root->revision();

    // A node more at the end of the root: its revision changes, the record of `kept` does not.
    RuntimeGraphDraftSession session(context_, program);
    (void)session.edit(root).addSyncNode();
    const camel::runtime::Program *grown = session.commit();
    GCGraph *grownRoot                   = grown->root();
    const Anchor grownAnchor{grown, grownRoot, kept};
    os << "FACTS node_revision " << (grownRoot->revision() == revision ? "kept" : "new") << "\n";
    os << "FACTS node_version " << (grownRoot->nodeContentHash(kept) == version ? "kept" : "new")
       << "\n";
    os << "FACTS node_other node="
       << statusName(store.probe(k.node, std::vector<Anchor>{grownAnchor}))
       << " graph=" << statusName(store.probe(k.kind, std::vector<Anchor>{grownAnchor})) << "\n";

    // The node itself: another slot index, a commit, another version of the node. The commit
    // after it puts the index back, so that the program this pass returns runs as it did.
    const gc_slot_idx_t index = grownRoot->node(kept)->dataIndex;
    RuntimeGraphDraftSession edit(context_, grown);
    edit.edit(grownRoot).setNodeDataIndex(kept, index + 1);
    const camel::runtime::Program *changed = edit.commit();
    os << "FACTS node_self node="
       << statusName(store.probe(k.node, std::vector<Anchor>{Anchor{changed, changed->root(), kept}}))
       << "\n";
    RuntimeGraphDraftSession restore(context_, changed);
    restore.edit(changed->root()).setNodeDataIndex(kept, index);
    return restore.commit();
}
