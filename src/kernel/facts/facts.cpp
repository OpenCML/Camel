// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The registry of fact kinds, and the facts of a graph (camel/facts/facts.h).
 *
 * The registry is process-wide and append-only: a kind is never removed, so a KindId and the
 * KindInfo it names stay valid until the process exits. The records live in a deque, which never
 * moves what it already holds, so a reader of a record reads it without the lock.
 *
 * A store is handed the two things only the owner of the graphs can say -- the view to read a graph
 * through and the version of its content (`GraphAccess`) -- when it is created. A view is made on
 * demand, so its address names nothing: a store is keyed on the graph as a value and never on a
 * view of it, and the same graph read through another adapter finds the same facts.
 *
 * A read finds the entries stored at (kind, first anchor's graph, first anchor's node), one of them
 * at a time when the further anchors differ, and compares each one's stamp: the version of every
 * anchored graph, the operator registry's epoch when the computation reported records (a record
 * completed since may change the answer without the graph moving), and the target hash. A stamp
 * that no longer matches drops the entry and the fact is computed again.
 *
 * What a computation reported reading (`Reads`) is kept beside the value and printed by the dump.
 * It does not decide staleness here: `GraphAccess` carries one version per graph and no version for
 * a node, a region or a name binding, so a version that moved cannot be narrowed to the things the
 * computation reported. The conservative rule therefore decides, which recomputes a fact more often
 * and never answers a question differently; the answer the precise rule produces
 * (`Status::Revalidated`) is not produced until the owner versions what a computation read.
 *
 * `computeAll` and `dump` read the graph handed to them. Reaching a callee would mean making a view
 * of it, which only the owner can do, and the header names no accessor for the store's
 * `GraphAccess`: a callee's facts are filled in when its owner asks for them, at its own anchors.
 */

#include "camel/facts/facts.h"

#include "camel/facts/arrangeable.h"

#include "camel/core/loop_kind.h"
#include "camel/core/operator_table.h"

#include "camel/graph/graph.h"
#include "camel/runtime/fact_slots.h"
#include "graph/graph_view.h"

#include <algorithm>
#include <deque>
#include <format>
#include <ostream>
#include <shared_mutex>
#include <tuple>

namespace camel::facts {

namespace {

/// The kinds, in registration order, and the key each answers to.
struct Registry {
    std::shared_mutex mutex;
    std::deque<KindInfo> kinds;
    std::unordered_map<std::string, KindId> byKey;
};

Registry &registry() {
    static Registry instance;
    return instance;
}

[[noreturn]] void reject(const std::string &key, std::string_view why) {
    throw std::invalid_argument(std::format("fact kind '{}': {}", key, why));
}

/// The rules `registerKind` states, in the order the header lists them.
void checkKind(const KindInfo &info) {
    const size_t dot = info.key.find('.');
    if (info.owner.empty() || dot == std::string::npos || dot + 1 == info.key.size() ||
        info.key.substr(0, dot) != info.owner) {
        reject(
            info.key,
            std::format("the key must be '<owner>.<name>' with the owner '{}'", info.owner));
    }
    if (!info.print) {
        reject(info.key, "a kind must say how its values are printed");
    }
    if (info.cls == Class::Content) {
        // The graph's content: read from the view each time, never stored, so it states no law
        // that makes two values one value and there is nothing to invalidate.
        if (info.merges || info.merge) {
            reject(info.key, "a fact of class (i) is read, never stored: it states no merge");
        }
        if (!info.compute) {
            reject(info.key, "a fact of class (i) is the graph's content: the kind must read it");
        }
    }
}

/// The distinct graphs of `anchors`, in the order they first appear.
std::vector<const graph::Graph *> graphsOf(std::span<const Anchor> anchors) {
    std::vector<const graph::Graph *> graphs;
    for (const Anchor &anchor : anchors) {
        if (std::find(graphs.begin(), graphs.end(), anchor.graph) == graphs.end()) {
            graphs.push_back(anchor.graph);
        }
    }
    return graphs;
}

/// Whether two anchor lists name the same things, in the same order.
bool sameAnchors(std::span<const Anchor> left, std::span<const Anchor> right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t i = 0; i < left.size(); ++i) {
        if (left[i].graph != right[i].graph || left[i].node != right[i].node) {
            return false;
        }
    }
    return true;
}

/// Whether the anchors fit the kind's scope, and each names a node of its graph when one is named.
void checkAnchors(
    const KindInfo &kind, std::span<const Anchor> anchors, const GraphAccess &graphs) {
    if (anchors.empty()) {
        reject(kind.key, "a fact needs at least one anchor");
    }
    for (const Anchor &anchor : anchors) {
        if (anchor.graph == nullptr) {
            reject(kind.key, "an anchor without a graph");
        }
        if (anchor.node == kWholeGraph) {
            continue;
        }
        const graph::GraphView *view = graphs.view ? graphs.view(*anchor.graph) : nullptr;
        if (view != nullptr && !view->contains(anchor.node)) {
            reject(kind.key, std::format("node {} is not a node of its graph", anchor.node));
        }
    }
    const bool oneNode  = anchors.size() == 1 && anchors[0].node != kWholeGraph;
    const bool oneGraph = anchors.size() == 1 && anchors[0].node == kWholeGraph;
    bool sameGraph      = true;
    for (const Anchor &anchor : anchors) {
        sameGraph = sameGraph && anchor.graph == anchors[0].graph;
    }
    switch (kind.scope) {
    case Scope::Node:
        if (!oneNode) {
            reject(kind.key, "a Node fact has one anchor, a node");
        }
        break;
    case Scope::Graph:
        if (!oneGraph) {
            reject(kind.key, "a Graph fact has one anchor, a graph as a whole");
        }
        break;
    case Scope::CrossNode:
        if (!sameGraph) {
            reject(kind.key, "a CrossNode fact anchors nodes of one graph");
        }
        break;
    case Scope::CrossGraph:
        break;
    }
}

/// Where a kind has facts in `view`: `KindInfo::anchorsIn`, or its scope's default.
std::vector<std::vector<Anchor>>
anchorsOf(const KindInfo &kind, const graph::Graph &graph, const graph::GraphView &view) {
    if (kind.anchorsIn) {
        return kind.anchorsIn(view);
    }
    std::vector<std::vector<Anchor>> all;
    if (kind.scope == Scope::Node) {
        for (graph::NodeRef ref : view.nodes()) {
            all.push_back({Anchor{&graph, ref}});
        }
    } else if (kind.scope == Scope::Graph) {
        all.push_back({Anchor{&graph, kWholeGraph}});
    }
    return all;
}

/// The version of each anchored graph, one per distinct graph, in anchor order.
std::vector<std::pair<const graph::Graph *, std::uint64_t>>
versionsOf(std::span<const Anchor> anchors, const GraphAccess &graphs) {
    std::vector<std::pair<const graph::Graph *, std::uint64_t>> versions;
    for (const graph::Graph *graph : graphsOf(anchors)) {
        versions.emplace_back(graph, graphs.version ? graphs.version(*graph) : 0);
    }
    return versions;
}

/// Publishes `value` into the graph's slot for `kind` (`camel/runtime/fact_slots.h`): the answer
/// the next reader gets without asking this store. Only the two scopes with one fact per place
/// have a slot -- a Graph fact's value, a Node fact's array entry -- so a CrossNode or CrossGraph
/// fact, which has several facts on one graph, is answered by the store alone.
void publishSlot(
    const GraphAccess &graphs,
    KindId kind,
    Scope scope,
    std::span<const Anchor> anchors,
    const void *value) {
    if (anchors.empty() || anchors[0].graph == nullptr) {
        return;
    }
    const Anchor &at = anchors[0];
    if (scope == Scope::Graph && graphs.setSlot) {
        graphs.setSlot(*at.graph, kind, value);
    } else if (scope == Scope::Node && graphs.setNodeSlot && at.node != kWholeGraph) {
        graphs.setNodeSlot(*at.graph, kind, at.node, value);
    }
}

/// Clears the slot `publishSlot` would have written. Called before the value is released, so that
/// a reader never finds a slot naming a fact that is gone. It needs no scope: a graph slot that a
/// Node kind never writes is cleared harmlessly, and the same the other way round.
void clearSlot(const GraphAccess &graphs, KindId kind, std::span<const Anchor> anchors) {
    if (anchors.empty() || anchors[0].graph == nullptr) {
        return;
    }
    const Anchor &at = anchors[0];
    if (at.node == kWholeGraph) {
        if (graphs.setSlot) {
            graphs.setSlot(*at.graph, kind, nullptr);
        }
    } else if (graphs.setNodeSlot) {
        graphs.setNodeSlot(*at.graph, kind, at.node, nullptr);
    }
}

std::string jsonString(std::string_view text) {
    std::string out = "\"";
    for (char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
        }
    }
    return out + "\"";
}

std::string_view className(Class cls) {
    switch (cls) {
    case Class::Content:
        return "content";
    case Class::Derived:
        return "derived";
    case Class::Target:
        return "target";
    }
    return "?";
}

/// One `FACT {json}` line (dump). `stamp` is null for a Content kind: its value is the graph's.
std::string factJson(
    const KindInfo &kind,
    std::span<const Anchor> anchors,
    const void *value,
    const StoredFact *stored,
    const std::function<std::string(const graph::Graph *)> &labelOf) {
    std::string out = std::format(
        "{{\"library\":\"facts\",\"kind\":{},\"version\":{},\"class\":{},\"anchors\":[",
        jsonString(kind.key),
        kind.version,
        jsonString(className(kind.cls)));
    for (size_t i = 0; i < anchors.size(); ++i) {
        out += std::format("{}{{\"graph\":{}", i ? "," : "", jsonString(labelOf(anchors[i].graph)));
        if (anchors[i].node != kWholeGraph) {
            out += std::format(",\"node\":{}", anchors[i].node);
        }
        out += "}";
    }
    out += "],\"value\":" + jsonString(kind.print(value)) + ",\"stamp\":";
    if (stored == nullptr) {
        return out + "null}";
    }
    out += "{\"versions\":[";
    for (size_t i = 0; i < stored->versions.size(); ++i) {
        out += std::format(
            "{}{{\"graph\":{},\"version\":{}}}",
            i ? "," : "",
            jsonString(labelOf(stored->versions[i].first)),
            stored->versions[i].second);
    }
    out += "],\"reads\":{\"nodes\":[";
    for (size_t i = 0; i < stored->reads.nodes.size(); ++i) {
        out += std::format("{}{}", i ? "," : "", stored->reads.nodes[i]);
    }
    out += "],\"regions\":[";
    for (size_t i = 0; i < stored->reads.regions.size(); ++i) {
        out += std::format("{}{}", i ? "," : "", stored->reads.regions[i]);
    }
    out += "],\"names\":[";
    for (size_t i = 0; i < stored->reads.names.size(); ++i) {
        out += std::format(
            "{}{{\"name\":{},\"holds\":{}}}",
            i ? "," : "",
            jsonString(stored->reads.names[i].name),
            stored->reads.names[i].holds == nullptr
                ? std::string("null")
                : jsonString(labelOf(stored->reads.names[i].holds)));
    }
    out += "],\"records\":[";
    for (size_t i = 0; i < stored->reads.records.size(); ++i) {
        out += std::format("{}{}", i ? "," : "", stored->reads.records[i]);
    }
    // The epoch and the target are fields of the stamp, not of the fact, and the fact's own
    // closing brace follows the stamp's: a reader parses a FACT line as one object.
    out += "]},\"epoch\":" + std::to_string(stored->epoch) + ",\"target\":" +
           std::to_string(stored->target) + "}}";
    return out;
}

} // namespace

// ---- Registry --------------------------------------------------------------------------------

KindId registerKind(KindInfo info) {
    checkKind(info);
    Registry &r = registry();
    std::unique_lock lock(r.mutex);
    if (auto it = r.byKey.find(info.key); it != r.byKey.end()) {
        const KindInfo &known = r.kinds[it->second];
        if (known.version != info.version || known.payload != info.payload ||
            known.cls != info.cls || known.scope != info.scope) {
            reject(
                info.key,
                "registered before as another kind (version, value type, class or scope)");
        }
        return it->second;
    }
    const KindId id = static_cast<KindId>(r.kinds.size());
    r.byKey.emplace(info.key, id);
    r.kinds.push_back(std::move(info));
    return id;
}

KindId findKind(std::string_view key) {
    Registry &r = registry();
    std::shared_lock lock(r.mutex);
    const auto it = r.byKey.find(std::string(key));
    return it == r.byKey.end() ? kNoKind : it->second;
}

const KindInfo &kindInfo(KindId kind) {
    Registry &r = registry();
    std::shared_lock lock(r.mutex);
    if (kind >= r.kinds.size()) {
        throw std::out_of_range(std::format("no fact kind has the id {}", kind));
    }
    return r.kinds[kind]; // a deque never moves its elements
}

std::vector<KindId> registeredKinds() {
    Registry &r    = registry();
    std::shared_lock lock(r.mutex);
    std::vector<KindId> ids(r.kinds.size());
    for (KindId id = 0; id < ids.size(); ++id) {
        ids[id] = id;
    }
    return ids;
}

std::string_view statusName(Status status) {
    switch (status) {
    case Status::Absent:
        return "absent";
    case Status::Valid:
        return "valid";
    case Status::Revalidated:
        return "revalidated";
    case Status::Stale:
        return "stale";
    }
    return "?";
}

// ---- The read log ----------------------------------------------------------------------------

Reads ReadLog::take() {
    Reads taken = std::move(reads_);
    reads_      = {};
    std::sort(taken.nodes.begin(), taken.nodes.end());
    taken.nodes.erase(std::unique(taken.nodes.begin(), taken.nodes.end()), taken.nodes.end());
    std::sort(taken.regions.begin(), taken.regions.end());
    taken.regions.erase(
        std::unique(taken.regions.begin(), taken.regions.end()), taken.regions.end());
    std::sort(
        taken.records.begin(), taken.records.end(), [](ops::OperId left, ops::OperId right) {
            return left < right;
        });
    taken.records.erase(
        std::unique(taken.records.begin(), taken.records.end()), taken.records.end());
    std::sort(
        taken.names.begin(),
        taken.names.end(),
        [](const NameRead &left, const NameRead &right) {
            return std::tie(left.in, left.name, left.holds) <
                   std::tie(right.in, right.name, right.holds);
        });
    taken.names.erase(
        std::unique(
            taken.names.begin(),
            taken.names.end(),
            [](const NameRead &left, const NameRead &right) {
                return left.in == right.in && left.name == right.name && left.holds == right.holds;
            }),
        taken.names.end());
    return taken;
}

// ---- Which store holds a graph ---------------------------------------------------------------

namespace {

/// The graphs the process's stores were told about, by address, with the store that was told. A
/// process makes one store per context, so a graph names one of them; this table is what lets a
/// reader that holds only a graph -- the control-region reader, the graph's own destructor --
/// reach the store of the context that made it, without the graph carrying a member and without
/// the framework reaching for a context the graph does not know.
///
/// Allocated once and never freed, as the store is: a graph can be freed after
/// the last context's teardown, and a table that is gone by then would turn a graph's last act
/// into a use of a dead object. A store that is destroyed takes its rows out, so a graph whose
/// store is gone names nothing rather than a dead store.
struct GraphHolds {
    std::mutex mutex;
    std::unordered_map<const graph::Graph *, Store *> stores;
};

GraphHolds &graphHolds() {
    static GraphHolds *holds = new GraphHolds();
    return *holds;
}

} // namespace

GraphAccess frozenGraphs() {
    return GraphAccess{
        .view        = [](const graph::Graph &graph) { return &graph.view(); },
        .version     = [](const graph::Graph &) { return std::uint64_t(0); },
        // The graph's own slots: written and cleared by the store, read by whoever has the graph
        // (camel/runtime/fact_slots.h). A draft keeps none, and these writes land nowhere.
        .setSlot     = [](const graph::Graph &graph, KindId kind, const void *value) {
            runtime::setFactSlotOf(graph, kind, value);
        },
        .setNodeSlot = [](const graph::Graph &graph, KindId kind, NodeId node, const void *value) {
            runtime::setNodeFactSlotOf(graph, kind, node, value);
        },
    };
}

void holdGraph(Store &store, const graph::Graph &graph) {
    GraphHolds &holds = graphHolds();
    std::lock_guard lock(holds.mutex);
    holds.stores[&graph] = &store;
}

Store *storeHolding(const graph::Graph &graph) {
    GraphHolds &holds = graphHolds();
    std::lock_guard lock(holds.mutex);
    const auto it = holds.stores.find(&graph);
    return it == holds.stores.end() ? nullptr : it->second;
}

void forgetGraph(const graph::Graph *graph) {
    if (graph == nullptr) {
        return;
    }
    Store *store = nullptr;
    {
        GraphHolds &holds = graphHolds();
        std::lock_guard lock(holds.mutex);
        const auto it = holds.stores.find(graph);
        if (it != holds.stores.end()) {
            store = it->second;
            holds.stores.erase(it);
        }
    }
    // Outside the holds' lock: the store takes its own.
    if (store != nullptr) {
        store->invalidateGraph(graph);
    }
}

// ---- Store -----------------------------------------------------------------------------------

size_t Store::KeyHash::operator()(const Key &key) const {
    size_t seed = std::hash<const graph::Graph *>{}(key.graph);
    seed ^= std::hash<std::uint32_t>{}(key.kind) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    seed ^= std::hash<NodeId>{}(key.node) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

Store::Store(GraphAccess graphs, bool persistent)
    : graphs_(std::move(graphs)), persistent_(persistent) {}

Store::~Store() {
    // A graph may outlive the store (the store goes with its context; a frozen graph can be freed
    // after it). Every slot this store published is cleared while the values it names are still
    // its own -- after this body they are released with the entries, and a slot left behind would
    // name a fact nobody owns (camel/runtime/fact_slots.h).
    for (const auto &[key, list] : entries_) {
        for (const Entry &entry : list) {
            clearSlot(graphs_, key.kind, entry.anchors);
        }
    }
    // A store that goes before the graphs it was told about stops being their answer: the graph's
    // end (`forgetGraph`) then names no store rather than a dead one.
    GraphHolds &holds = graphHolds();
    std::lock_guard lock(holds.mutex);
    std::erase_if(holds.stores, [this](const auto &item) { return item.second == this; });
}

void Store::checkType(KindId kind, const std::type_info &type) {
    const KindInfo &info = kindInfo(kind);
    if (info.payload != std::type_index(type)) {
        reject(info.key, std::format("its values are {}, not {}", info.payload.name(), type.name()));
    }
}

Store::Entry *
Store::find(KindId kind, std::span<const Anchor> anchors, std::uint64_t target, Status &status) {
    status      = Status::Absent;
    auto bucket = entries_.find(Key{kind, anchors[0].graph, anchors[0].node});
    if (bucket == entries_.end()) {
        return nullptr;
    }
    auto &list = bucket->second;
    auto it    = std::find_if(list.begin(), list.end(), [&](const Entry &entry) {
        return sameAnchors(entry.anchors, anchors);
    });
    if (it == list.end()) {
        return nullptr;
    }

    // The stamp: a few integer comparisons.
    const auto versions = versionsOf(anchors, graphs_);
    bool current        = versions.size() == it->versions.size();
    for (size_t i = 0; current && i < versions.size(); ++i) {
        current = it->versions[i] == versions[i];
    }
    // A fact computed from operator records is stale when a record was completed since: what it
    // asked may have changed while the graph stood still.
    const bool epoch = !it->reads.declarations() || it->epoch == ops::Registry::instance().epoch();
    const bool onTarget = it->target == target;
    if (current && epoch && onTarget) {
        status = Status::Valid;
        return &*it;
    }
    // The fact is gone: its slot goes first, while the value it names is still the entry's.
    clearSlot(graphs_, kind, it->anchors);
    list.erase(it);
    if (list.empty()) {
        entries_.erase(bucket);
    }
    status = Status::Stale;
    ++counts_.stale;
    return nullptr;
}

void Store::putValue(
    KindId kind,
    std::span<const Anchor> anchors,
    std::shared_ptr<const void> value,
    std::uint64_t target) {
    const KindInfo &info = kindInfo(kind);
    checkAnchors(info, anchors, graphs_);
    if (info.cls == Class::Content) {
        reject(info.key, "a fact of class (i) is the graph's content; it is written with the graph");
    }
    if (info.cls == Class::Target && target == 0) {
        reject(info.key, "a fact of class (iii) is stored with the hash of its target");
    }
    if (info.cls != Class::Target && target != 0) {
        reject(info.key, "only a fact of class (iii) depends on a target");
    }
    if (!value) {
        reject(info.key, "no value");
    }
    Entry entry;
    entry.anchors  = std::vector<Anchor>(anchors.begin(), anchors.end());
    entry.value    = std::move(value);
    entry.versions = versionsOf(anchors, graphs_);
    entry.target   = target;
    entry.epoch    = 0;
    std::lock_guard lock(mutex_);
    auto &list = entries_[Key{kind, anchors[0].graph, anchors[0].node}];
    auto it    = std::find_if(list.begin(), list.end(), [&](const Entry &known) {
        return sameAnchors(known.anchors, anchors);
    });
    if (it == list.end()) {
        list.push_back(std::move(entry));
        publishSlot(graphs_, kind, info.scope, anchors, list.back().value.get());
    } else {
        *it = std::move(entry);
        publishSlot(graphs_, kind, info.scope, anchors, it->value.get());
    }
    ++counts_.puts;
}

std::shared_ptr<const void>
Store::getValue(KindId kind, std::span<const Anchor> anchors, std::uint64_t target) {
    const KindInfo &info = kindInfo(kind);
    checkAnchors(info, anchors, graphs_);
    if (info.cls == Class::Content) {
        // The graph is where it is stored: read it from the view each time.
        ReadLog log;
        return info.compute ? info.compute(anchors, graphs_, log) : nullptr;
    }
    std::lock_guard lock(mutex_);
    Status status;
    if (Entry *entry = find(kind, anchors, target, status)) {
        ++counts_.hits;
        return entry->value;
    }
    ++counts_.misses;
    return nullptr;
}

std::shared_ptr<const void>
Store::obtainValue(KindId kind, std::span<const Anchor> anchors, std::uint64_t target) {
    if (std::shared_ptr<const void> value = getValue(kind, anchors, target)) {
        return value;
    }
    const KindInfo &info = kindInfo(kind);
    if (!info.compute) {
        return nullptr;
    }
    ReadLog log;
    std::shared_ptr<const void> value = info.compute(anchors, graphs_, log);
    {
        std::lock_guard lock(mutex_);
        ++counts_.computed;
    }
    if (!value) {
        return nullptr;
    }
    Reads reads = log.take();
    // Stored with what it reported reading: the entry's stamp holds it, and the epoch it was
    // computed at is the registry's while those reads name records.
    const std::uint64_t epoch = reads.declarations() ? ops::Registry::instance().epoch() : 0;
    Entry entry;
    entry.anchors  = std::vector<Anchor>(anchors.begin(), anchors.end());
    entry.value    = value;
    entry.versions = versionsOf(anchors, graphs_);
    entry.reads    = std::move(reads);
    entry.epoch    = epoch;
    entry.target   = target;
    std::lock_guard lock(mutex_);
    auto &list = entries_[Key{kind, anchors[0].graph, anchors[0].node}];
    auto it    = std::find_if(list.begin(), list.end(), [&](const Entry &known) {
        return sameAnchors(known.anchors, anchors);
    });
    if (it == list.end()) {
        list.push_back(std::move(entry));
        publishSlot(graphs_, kind, info.scope, anchors, list.back().value.get());
    } else {
        *it = std::move(entry);
        publishSlot(graphs_, kind, info.scope, anchors, it->value.get());
    }
    ++counts_.puts;
    return value;
}

Status Store::probe(KindId kind, std::span<const Anchor> anchors, std::uint64_t target) {
    const KindInfo &info = kindInfo(kind);
    checkAnchors(info, anchors, graphs_);
    if (info.cls == Class::Content) {
        return Status::Valid; // read from the graph, never stored, never stale
    }
    std::lock_guard lock(mutex_);
    Status status;
    (void)find(kind, anchors, target, status);
    return status;
}

std::optional<bool> Store::verify(KindId kind, std::span<const Anchor> anchors) {
    const KindInfo &info = kindInfo(kind);
    if (!info.compute) {
        return std::nullopt;
    }
    checkAnchors(info, anchors, graphs_);
    if (info.cls != Class::Derived) {
        return std::nullopt; // (i) is read, not stored; (iii) is checked by its target's owner
    }
    std::shared_ptr<const void> stored;
    {
        std::lock_guard lock(mutex_);
        Status status;
        if (Entry *entry = find(kind, anchors, 0, status)) {
            stored = entry->value;
        }
    }
    if (!stored) {
        return std::nullopt;
    }
    ReadLog log;
    std::shared_ptr<const void> fresh = info.compute(anchors, graphs_, log);
    return fresh != nullptr && info.equal(stored.get(), fresh.get());
}

void Store::invalidate(KindId kind, std::span<const Anchor> anchors) {
    const KindInfo &info = kindInfo(kind);
    checkAnchors(info, anchors, graphs_);
    std::lock_guard lock(mutex_);
    auto bucket = entries_.find(Key{kind, anchors[0].graph, anchors[0].node});
    if (bucket == entries_.end()) {
        return;
    }
    auto &list = bucket->second;
    for (auto it = list.begin(); it != list.end();) {
        if (sameAnchors(it->anchors, anchors)) {
            clearSlot(graphs_, kind, it->anchors);
            it = list.erase(it);
        } else {
            ++it;
        }
    }
    if (list.empty()) {
        entries_.erase(bucket);
    }
}

void Store::invalidateGraph(const graph::Graph *graph) {
    std::lock_guard lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->first.graph != graph) {
            ++it;
            continue;
        }
        for (const Entry &entry : it->second) {
            clearSlot(graphs_, it->first.kind, entry.anchors);
        }
        it = entries_.erase(it);
    }
}

void Store::clear() {
    std::lock_guard lock(mutex_);
    for (const auto &[key, list] : entries_) {
        for (const Entry &entry : list) {
            clearSlot(graphs_, key.kind, entry.anchors);
        }
    }
    entries_.clear();
}

void Store::forEach(const std::function<void(const StoredFact &)> &visitor) const {
    std::vector<StoredFact> facts;
    {
        std::lock_guard lock(mutex_);
        for (const auto &[key, list] : entries_) {
            for (const Entry &entry : list) {
                facts.push_back(StoredFact{
                    .kind     = key.kind,
                    .anchors  = entry.anchors,
                    .value    = entry.value,
                    .versions = entry.versions,
                    .reads    = entry.reads,
                    .epoch    = entry.epoch,
                    .target   = entry.target,
                });
            }
        }
    }
    // A stable order to print in: by kind, then by anchors.
    std::sort(facts.begin(), facts.end(), [](const StoredFact &left, const StoredFact &right) {
        if (left.kind != right.kind) {
            return left.kind < right.kind;
        }
        for (size_t i = 0; i < left.anchors.size() && i < right.anchors.size(); ++i) {
            if (left.anchors[i].graph != right.anchors[i].graph) {
                return left.anchors[i].graph < right.anchors[i].graph;
            }
            if (left.anchors[i].node != right.anchors[i].node) {
                return left.anchors[i].node < right.anchors[i].node;
            }
        }
        return left.anchors.size() < right.anchors.size();
    });
    for (const StoredFact &fact : facts) {
        visitor(fact); // outside the lock: the visitor may read the store
    }
}

size_t Store::size() const {
    std::lock_guard lock(mutex_);
    size_t n = 0;
    for (const auto &[_, list] : entries_) {
        n += list.size();
    }
    return n;
}

Counts Store::counts() const {
    std::lock_guard lock(mutex_);
    return counts_;
}

void Store::resetCounts() {
    std::lock_guard lock(mutex_);
    counts_ = {};
}

// ---- Bulk computation and the dump -----------------------------------------------------------

void computeAll(
    Store &store, const graph::Graph &root, const graph::GraphView &rootView,
    std::span<const KindId> only) {
    for (KindId kind : registeredKinds()) {
        if (!only.empty() && std::find(only.begin(), only.end(), kind) == only.end()) {
            continue;
        }
        const KindInfo &info = kindInfo(kind);
        if (!info.compute || info.cls == Class::Content) {
            continue;
        }
        for (const std::vector<Anchor> &anchors : anchorsOf(info, root, rootView)) {
            (void)store.obtainValue(kind, anchors);
        }
    }
}

void dump(
    Store &store, const graph::Graph &root, const graph::GraphView &rootView, std::ostream &os,
    std::span<const KindId> only) {
    const std::function<std::string(const graph::Graph *)> labelOf =
        [&root](const graph::Graph *graph) {
            return graph == &root ? std::string("root") : std::string("outside-the-walk");
        };
    // The kinds a dump prints: every one, or the ones the caller named.
    const auto wanted = [only](KindId kind) {
        return only.empty() || std::find(only.begin(), only.end(), kind) != only.end();
    };

    // The stored facts about this graph whose stamps hold for it.
    std::vector<StoredFact> stored;
    store.forEach([&](const StoredFact &fact) { stored.push_back(fact); });
    for (const StoredFact &fact : stored) {
        if (!wanted(fact.kind)) {
            continue;
        }
        bool here = true;
        for (const Anchor &anchor : fact.anchors) {
            here = here && anchor.graph == &root;
        }
        if (!here) {
            continue;
        }
        if (store.probe(fact.kind, fact.anchors, fact.target) != Status::Valid) {
            continue; // stale: not printed, and the probe dropped it
        }
        const KindInfo &info = kindInfo(fact.kind);
        os << "FACT " << factJson(info, fact.anchors, fact.value.get(), &fact, labelOf) << "\n";
    }

    // Graph content (class i): read from the graph each time.
    for (KindId kind : registeredKinds()) {
        if (!wanted(kind)) {
            continue;
        }
        const KindInfo &info = kindInfo(kind);
        if (info.cls != Class::Content) {
            continue;
        }
        for (const std::vector<Anchor> &anchors : anchorsOf(info, root, rootView)) {
            // A Content kind's value is the graph's: asking the store reads it from the view.
            if (std::shared_ptr<const void> value = store.getValue(kind, anchors)) {
                os << "FACT " << factJson(info, anchors, value.get(), nullptr, labelOf) << "\n";
            }
        }
    }
}

// ---- core.arrangeable -------------------------------------------------------------------------

namespace {

Arrangeable andTerms(const Arrangeable &left, const Arrangeable &right) {
    return Arrangeable{
        left.pure && right.pure,
        left.noExternalEffect && right.noExternalEffect,
        left.cannotFault && right.cannotFault,
        left.terminates && right.terminates};
}

/// The four terms of an OPER node, read from the operator and from nothing else. The first three
/// are what the operator record states (`camel/ops/operator_record.h`: `pure` is the empty `uses`
/// clause, `external` is what an access outside the program or an unstated clause amounts to, and
/// `panic` is the fault fact); the fourth is the node's form -- an operator node computes a value
/// and hands it on, so whatever it runs is bounded by the call that asked for that value, and a
/// loop is a LOOP node rather than an operator (design principles 3.10).
Arrangeable termsOfOperator(camel::ops::OperId oper) {
    Arrangeable terms;
    if (oper == camel::ops::kNoOper) {
        // An operator nobody named: none of the four terms is known.
        return terms;
    }
    const camel::core::OperatorTable &table = camel::core::OperatorTable::instance();
    if (const camel::ops::Record *record = table.recordOf(oper); record != nullptr) {
        // The record is the one statement of the three, and this is the only place it is read for
        // this kind: `pure` is the empty `uses` clause, `external` the reach that clause states (a
        // clause nobody stated reads as writing the global resource, design principles 3.3), and
        // `panic` the fault fact. The built-in module writes its clause into every record it
        // registers, and a Camel declaration writes its own, so a reader of this kind and a reader
        // of the table read one statement instead of two derivations of it.
        terms.pure             = record->pure();
        terms.noExternalEffect = !record->external();
        terms.cannotFault      = !record->mayFault();
    } else {
        // No record at all: nobody stated anything about the operator, so none of the three terms
        // holds. The table's derived bits are not a second answer to read here -- falling back to
        // them is the pair this kind exists to remove.
        terms = Arrangeable{};
    }
    terms.terminates = true;
    return terms;
}

/// Walks the nodes one node stands for. A branch, a loop and a call carry what runs inside them,
/// so their terms are the conjunction over the nodes of their arms, of their regions, or of the
/// callee's body: a branch with an effect in one arm is not freely arrangeable, and neither is a
/// call whose callee may not end. A callee that is already being judged above the node is a
/// recursion, and a recursion is not known to terminate (design principles 3.4, 3.10) -- the node
/// that makes the call, not the nodes of the body it is in.
class ArrangeWalk {
  public:
    ArrangeWalk(const GraphAccess &graphs, ReadLog &log) : graphs_(graphs), log_(log) {}

    /// The graph the node is anchored on: a call to it, met while walking below the node, is the
    /// recursion this walk has to see.
    void seed(const graph::Graph *graph) {
        if (graph != nullptr) {
            active_.push_back(graph);
        }
    }

    Arrangeable at(const graph::GraphView &view, NodeId node) {
        const graph::NodeRef ref = static_cast<graph::NodeRef>(node);
        if (!view.contains(ref)) {
            return Arrangeable{};
        }
        log_.node(node);
        switch (view.kindOf(ref)) {
        case graph::NodeKind::Oper: {
            const camel::ops::OperId oper = view.operatorOf(ref);
            log_.record(oper);
            return termsOfOperator(oper);
        }
        case graph::NodeKind::Data:
        case graph::NodeKind::Port:
        case graph::NodeKind::Gate:
            // A value, a port of the body, and the hand-on of a `sync` block: computed from what
            // they read, with nothing to write, nothing to fail and nothing to wait for.
            return Arrangeable{true, true, true, true};
        case graph::NodeKind::Brch: {
            Arrangeable terms{true, true, true, true};
            for (const RegionId region : view.regionsOwnedBy(ref)) {
                terms = andRegions(view, region, terms);
            }
            return terms;
        }
        case graph::NodeKind::Loop: {
            // The loop's own termination term is its form's (design principles 3.10): a `for` and
            // a `for ... while` take a finite sequence, and a `while` has no bound to read, so it
            // is not known to end.
            Arrangeable terms{
                true, true, true, view.loopFormOf(ref).kind != camel::core::LoopKind::While};
            for (const RegionId region : view.regionsOwnedBy(ref)) {
                terms = andRegions(view, region, terms);
            }
            return terms;
        }
        case graph::NodeKind::Call:
        case graph::NodeKind::Func: {
            const graph::CalleeRef callee = view.calleeOf(ref);
            if (callee.kind != graph::CalleeRef::Kind::Value || callee.graph == nullptr) {
                // A callee named by an environment entry, or not named yet: what it runs is not
                // in this graph, so none of the four terms is known here.
                return Arrangeable{};
            }
            if (std::find(active_.begin(), active_.end(), callee.graph) != active_.end()) {
                // A call that reaches a graph this walk is already in: a recursion. It may not
                // end, so the call is not freely arrangeable wherever it stands.
                return Arrangeable{};
            }
            const graph::GraphView *calleeView = graphs_.view ? graphs_.view(*callee.graph) : nullptr;
            if (calleeView == nullptr) {
                return Arrangeable{};
            }
            return atGraph(*calleeView, callee.graph);
        }
        default:
            // A BIND node (reserved: nothing creates one): not known to be anything.
            return Arrangeable{};
        }
    }

  private:
    Arrangeable atGraph(const graph::GraphView &view, const graph::Graph *graph) {
        active_.push_back(graph);
        Arrangeable terms{true, true, true, true};
        for (const graph::NodeRef ref : view.nodes()) {
            terms = andTerms(terms, at(view, ref));
        }
        active_.pop_back();
        return terms;
    }

    Arrangeable andRegions(const graph::GraphView &view, RegionId region, Arrangeable terms) {
        log_.region(region);
        for (const graph::NodeRef ref : view.regionNodes(region)) {
            terms = andTerms(terms, at(view, ref));
        }
        return terms;
    }

    const GraphAccess &graphs_;
    ReadLog &log_;
    std::vector<const graph::Graph *> active_;
};

} // namespace

Arrangeable operatorArrangeable(camel::ops::OperId oper) { return termsOfOperator(oper); }

Arrangeable
nodeArrangeable(const graph::Graph &graph, const graph::GraphView &view, NodeId node, const GraphAccess &graphs, ReadLog &log) {
    ArrangeWalk walk{graphs, log};
    walk.seed(&graph);
    return walk.at(view, node);
}

std::string toString(const Arrangeable &arrangeable) {
    if (arrangeable.all()) {
        return "freely arrangeable";
    }
    std::string out;
    const auto add = [&out](bool holds, std::string_view why) {
        if (holds) {
            return;
        }
        if (!out.empty()) {
            out += ", ";
        }
        out += std::string(why);
    };
    add(arrangeable.pure, "not pure");
    add(arrangeable.noExternalEffect, "external effect");
    add(arrangeable.cannotFault, "may fault");
    add(arrangeable.terminates, "may not terminate");
    return out;
}

KindId arrangeableKind() {
    static const KindId id = registerKind<Arrangeable>(Kind<Arrangeable>{
        .key        = "core.arrangeable",
        .owner      = "core",
        .version    = 1,
        .cls        = Class::Derived,
        .scope      = Scope::Node,
        .dependence = Dependence::Reported,
        .monotone   = false,
        .compute =
            [](std::span<const Anchor> anchors, const GraphAccess &graphs, ReadLog &log)
            -> std::optional<Arrangeable> {
            if (anchors.size() != 1 || anchors[0].graph == nullptr ||
                anchors[0].node == kWholeGraph || !graphs.view) {
                return std::nullopt;
            }
            const graph::GraphView *view = graphs.view(*anchors[0].graph);
            if (view == nullptr) {
                return std::nullopt;
            }
            ArrangeWalk walk{graphs, log};
            walk.seed(anchors[0].graph);
            return walk.at(*view, anchors[0].node);
        },
        // The answer for a union of places is the conjunction of the answers for the parts: a
        // place is freely arrangeable when every place it is made of is. Adding nodes can turn
        // the answer false (an effect written into an arm), so the kind is not monotone.
        .merge =
            [](const Arrangeable &left, const Arrangeable &right) -> std::optional<Arrangeable> {
            return andTerms(left, right);
        },
        .print = [](const Arrangeable &arrangeable) { return toString(arrangeable); },
        .anchorsIn = {},
    });
    return id;
}

} // namespace camel::facts

namespace camel::facts {
namespace {
// Registered when the library loads, not on the first reader's call. A kind that exists only
// after somebody has asked for it is a kind `registeredKinds` cannot name, so `computeAll` never
// fills it and `camel inspect --facts` prints a table with this kind missing -- and a missing
// kind reads the same as a graph that has none.
[[maybe_unused]] const KindId kArrangeableKind = arrangeableKind();
} // namespace
} // namespace camel::facts
