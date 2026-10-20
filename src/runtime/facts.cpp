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
 * The registry of fact kinds and the fact store (camel/runtime/facts.h).
 *
 * The registry is process-wide and append-only: a kind is never removed, so a KindId and the
 * KindInfo it names stay valid until the process exits (records live in a deque).
 *
 * The store keeps, per (kind, GraphId of the first anchor's graph, first anchor's node), the
 * facts stored there (several when the further anchors differ). Every read checks the stamp
 * against the graphs the reader passes; a stale fact is erased under the same lock.
 */

#include "camel/runtime/facts.h"

#include "camel/core/operator_table.h"
#include "camel/runtime/reachable.h"

#include <algorithm>
#include <deque>
#include <format>
#include <ostream>
#include <shared_mutex>
#include <tuple>

namespace camel::runtime::fact {

namespace {

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

/// The rules of registerKind (facts.h), in order.
void checkKind(const KindInfo &info) {
    const size_t dot = info.key.find('.');
    if (info.owner.empty() || dot == std::string::npos || dot + 1 == info.key.size() ||
        info.key.substr(0, dot) != info.owner) {
        reject(info.key, std::format("the key must be '<owner>.<name>' with the owner '{}'", info.owner));
    }
    if (!info.print) {
        reject(info.key, "a kind must say how its values are printed");
    }
    if (info.cls == FactClass::GraphContent && !info.compute) {
        reject(info.key, "a fact of class (i) is the graph's content: the kind must read it (compute)");
    }
    if (info.cls == FactClass::Target && (info.encode || info.decode)) {
        reject(info.key, "a fact of class (iii) is never persisted by the core: no encode/decode");
    }
    if (static_cast<bool>(info.encode) != static_cast<bool>(info.decode)) {
        reject(info.key, "encode and decode come together");
    }
    if (info.dependency == Dependency::Cone && !info.cone) {
        reject(info.key, "Dependency::Cone needs the hash of the dependency cone (cone)");
    }
}

/// The distinct graphs of `anchors`, in order.
std::vector<const GCGraph *> graphsOf(std::span<const Anchor> anchors) {
    std::vector<const GCGraph *> graphs;
    for (const Anchor &anchor : anchors) {
        if (std::find(graphs.begin(), graphs.end(), anchor.graph) == graphs.end()) {
            graphs.push_back(anchor.graph);
        }
    }
    return graphs;
}

/// The anchors fit the kind's scope and name nodes of their graphs.
void checkAnchors(const KindInfo &kind, std::span<const Anchor> anchors) {
    if (anchors.empty()) {
        reject(kind.key, "a fact needs at least one anchor");
    }
    for (const Anchor &anchor : anchors) {
        if (anchor.graph == nullptr) {
            reject(kind.key, "an anchor without a graph");
        }
        if (anchor.node != kInvalidNodeRef && !anchor.graph->containsNodeRef(anchor.node)) {
            reject(kind.key, std::format("node {} is not a node of '{}'", anchor.node, anchor.graph->name()));
        }
    }
    const bool oneNode  = anchors.size() == 1 && anchors[0].node != kInvalidNodeRef;
    const bool oneGraph = anchors.size() == 1 && anchors[0].node == kInvalidNodeRef;
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

/// Where a kind has facts in `graph` (KindInfo::anchorsIn, or its scope's default).
std::vector<std::vector<Anchor>> anchorsOf(const KindInfo &kind, const GCGraph &graph) {
    if (kind.anchorsIn) {
        return kind.anchorsIn(graph);
    }
    std::vector<std::vector<Anchor>> all;
    if (kind.scope == Scope::Node) {
        for (gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
            all.push_back({Anchor{&graph, ref}});
        }
    } else if (kind.scope == Scope::Graph) {
        all.push_back({Anchor{&graph, kInvalidNodeRef}});
    }
    return all;
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

std::string_view className(FactClass cls) {
    switch (cls) {
    case FactClass::GraphContent:
        return "content";
    case FactClass::Derived:
        return "derived";
    case FactClass::Target:
        return "target";
    }
    return "?";
}

/// The JSON line of one fact (dumpFacts); `stamp` null for GraphContent.
std::string factJson(
    const KindInfo &kind, std::span<const Anchor> anchors, const void *value, const Stamp *stamp) {
    std::string out = std::format(
        "{{\"kind\":{},\"version\":{},\"class\":{},\"anchors\":[",
        jsonString(kind.key),
        kind.version,
        jsonString(className(kind.cls)));
    for (size_t i = 0; i < anchors.size(); ++i) {
        out += std::format("{}{{\"graph\":{}", i ? "," : "", jsonString(anchors[i].graph->name()));
        if (anchors[i].node != kInvalidNodeRef) {
            out += std::format(",\"node\":{}", anchors[i].node);
        }
        out += "}";
    }
    out += "],\"value\":" + jsonString(kind.print(value)) + ",\"stamp\":";
    if (stamp == nullptr) {
        return out + "null}";
    }
    out += "{\"revisions\":[";
    const std::vector<const GCGraph *> graphs = graphsOf(anchors);
    for (size_t i = 0; i < graphs.size(); ++i) {
        out += std::format(
            "{}{{\"graph\":{},\"revision\":{}}}",
            i ? "," : "",
            jsonString(graphs[i]->name()),
            closureRevision(const_cast<GCGraph *>(graphs[i])));
    }
    out += std::format("],\"epoch\":{},\"target\":{}", stamp->declarationEpoch, stamp->targetHash);
    if (kind.dependency == Dependency::Cone) {
        out += std::format(",\"cone\":\"{:016x}\"", stamp->cone);
    }
    return out + "}}";
}

std::vector<std::pair<GraphId, gc_node_ref_t>> anchorKeys(std::span<const Anchor> anchors) {
    std::vector<std::pair<GraphId, gc_node_ref_t>> keys;
    keys.reserve(anchors.size());
    for (const Anchor &anchor : anchors) {
        keys.emplace_back(anchor.graph->id(), anchor.node);
    }
    return keys;
}

} // namespace

// ---- Registry ------------------------------------------------------------------------------

KindId registerKind(KindInfo info) {
    checkKind(info);
    Registry &r = registry();
    std::unique_lock lock(r.mutex);
    if (auto it = r.byKey.find(info.key); it != r.byKey.end()) {
        const KindInfo &known = r.kinds[it->second];
        if (known.version != info.version || known.payload != info.payload ||
            known.cls != info.cls || known.scope != info.scope) {
            reject(info.key, "registered before as another kind (version, value type, class or scope)");
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
    auto it = r.byKey.find(std::string(key));
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
    Registry &r = registry();
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

// ---- Store ---------------------------------------------------------------------------------

size_t FactStore::KeyHash::operator()(const Key &key) const {
    size_t seed = std::hash<GraphId>{}(key.graph);
    seed ^= std::hash<uint32_t>{}(key.kind) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    seed ^= std::hash<uint32_t>{}(key.node) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

FactStore::FactStore()  = default;
FactStore::~FactStore() = default;

void FactStore::checkType(KindId kind, const std::type_info &type) {
    const KindInfo &info = kindInfo(kind);
    if (info.payload != std::type_index(type)) {
        reject(info.key, std::format("its values are {}, not {}", info.payload.name(), type.name()));
    }
}

Stamp FactStore::stampNow(const KindInfo &kind, std::span<const Anchor> anchors, uint64_t targetHash) {
    Stamp stamp;
    for (const GCGraph *graph : graphsOf(anchors)) {
        stamp.graphs.emplace_back(graph->id(), closureRevision(const_cast<GCGraph *>(graph)));
    }
    stamp.declarationEpoch =
        kind.readsDeclarations ? camel::core::OperatorTable::instance().declarationEpoch() : 0;
    stamp.targetHash = targetHash;
    stamp.cone       = kind.dependency == Dependency::Cone ? kind.cone(anchors) : 0;
    return stamp;
}

FactStore::Entry *
FactStore::find(KindId kind, std::span<const Anchor> anchors, uint64_t targetHash, Status &status) {
    const KindInfo &info = kindInfo(kind);
    status               = Status::Absent;
    auto bucket          = entries_.find(Key{kind, anchors[0].graph->id(), anchors[0].node});
    if (bucket == entries_.end()) {
        return nullptr;
    }
    const auto keys = anchorKeys(anchors);
    auto &list      = bucket->second;
    auto it = std::find_if(list.begin(), list.end(), [&](const Entry &e) { return e.anchors == keys; });
    if (it == list.end()) {
        return nullptr;
    }

    // The stamp, in every build (design 10.4): a few integer comparisons.
    const std::vector<const GCGraph *> graphs = graphsOf(anchors);
    bool revisions = graphs.size() == it->stamp.graphs.size();
    for (size_t i = 0; revisions && i < graphs.size(); ++i) {
        revisions = it->stamp.graphs[i] ==
                    std::pair<GraphId, uint64_t>(
                        graphs[i]->id(), closureRevision(const_cast<GCGraph *>(graphs[i])));
    }
    const bool epoch =
        !info.readsDeclarations ||
        it->stamp.declarationEpoch == camel::core::OperatorTable::instance().declarationEpoch();
    const bool target = it->stamp.targetHash == targetHash;
    if (revisions && epoch && target) {
        status = Status::Valid;
        return &*it;
    }
    // The precise rule: what the fact read may be unchanged although a revision changed.
    if (!revisions && epoch && target && info.dependency == Dependency::Cone &&
        info.cone(anchors) == it->stamp.cone) {
        it->stamp = stampNow(info, anchors, targetHash);
        status    = Status::Revalidated;
        ++counts_.revalidated;
        return &*it;
    }
    list.erase(it);
    if (list.empty()) {
        entries_.erase(bucket);
    }
    status = Status::Stale;
    ++counts_.stale;
    return nullptr;
}

void FactStore::putValue(
    KindId kind, std::span<const Anchor> anchors, Value value, uint64_t targetHash) {
    const KindInfo &info = kindInfo(kind);
    checkAnchors(info, anchors);
    if (info.cls == FactClass::GraphContent) {
        reject(info.key, "a fact of class (i) is the graph's content; it is written with the graph");
    }
    if (info.cls == FactClass::Target && targetHash == 0) {
        reject(info.key, "a fact of class (iii) is stored with the hash of its target");
    }
    if (info.cls != FactClass::Target && targetHash != 0) {
        reject(info.key, "only a fact of class (iii) depends on a target");
    }
    if (!value) {
        reject(info.key, "no value");
    }
    Stamp stamp = stampNow(info, anchors, targetHash);
    std::lock_guard lock(mutex_);
    auto &list      = entries_[Key{kind, anchors[0].graph->id(), anchors[0].node}];
    const auto keys = anchorKeys(anchors);
    auto it = std::find_if(list.begin(), list.end(), [&](const Entry &e) { return e.anchors == keys; });
    if (it == list.end()) {
        list.push_back(Entry{.anchors = keys, .value = std::move(value), .stamp = std::move(stamp)});
    } else {
        it->value = std::move(value);
        it->stamp = std::move(stamp);
    }
    ++counts_.puts;
}

Value FactStore::getValue(KindId kind, std::span<const Anchor> anchors, uint64_t targetHash) {
    const KindInfo &info = kindInfo(kind);
    checkAnchors(info, anchors);
    if (info.cls == FactClass::GraphContent) {
        return info.compute(anchors); // the graph is where it is stored
    }
    std::lock_guard lock(mutex_);
    Status status;
    if (Entry *entry = find(kind, anchors, targetHash, status)) {
        ++counts_.hits;
        return entry->value;
    }
    ++counts_.misses;
    return nullptr;
}

Value FactStore::obtainValue(KindId kind, std::span<const Anchor> anchors, uint64_t targetHash) {
    if (Value value = getValue(kind, anchors, targetHash)) {
        return value;
    }
    const KindInfo &info = kindInfo(kind);
    if (!info.compute) {
        return nullptr;
    }
    Value value = info.compute(anchors);
    {
        std::lock_guard lock(mutex_);
        ++counts_.computed;
    }
    if (value) {
        putValue(kind, anchors, value, targetHash);
    }
    return value;
}

Status FactStore::probe(KindId kind, std::span<const Anchor> anchors, uint64_t targetHash) {
    const KindInfo &info = kindInfo(kind);
    checkAnchors(info, anchors);
    if (info.cls == FactClass::GraphContent) {
        return Status::Valid;
    }
    std::lock_guard lock(mutex_);
    Status status;
    (void)find(kind, anchors, targetHash, status);
    return status;
}

std::optional<bool> FactStore::verify(KindId kind, std::span<const Anchor> anchors) {
    const KindInfo &info = kindInfo(kind);
    if (!info.compute) {
        return std::nullopt;
    }
    checkAnchors(info, anchors);
    if (info.cls != FactClass::Derived) {
        return std::nullopt; // (i) is read, not stored; (iii) is checked by its target's owner
    }
    Value stored;
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
    Value fresh = info.compute(anchors);
    return fresh != nullptr && info.equal(stored.get(), fresh.get());
}

void FactStore::invalidate(KindId kind, std::span<const Anchor> anchors) {
    const KindInfo &info = kindInfo(kind);
    checkAnchors(info, anchors);
    std::lock_guard lock(mutex_);
    auto bucket = entries_.find(Key{kind, anchors[0].graph->id(), anchors[0].node});
    if (bucket == entries_.end()) {
        return;
    }
    const auto keys = anchorKeys(anchors);
    std::erase_if(bucket->second, [&](const Entry &e) { return e.anchors == keys; });
    if (bucket->second.empty()) {
        entries_.erase(bucket);
    }
}

void FactStore::invalidateGraph(const GCGraph *graph) {
    std::lock_guard lock(mutex_);
    std::erase_if(entries_, [&](const auto &item) { return item.first.graph == graph->id(); });
}

void FactStore::clear() {
    std::lock_guard lock(mutex_);
    entries_.clear();
}

void FactStore::forEach(const std::function<void(const StoredFact &)> &visitor) const {
    std::vector<StoredFact> facts;
    {
        std::lock_guard lock(mutex_);
        for (const auto &[key, list] : entries_) {
            for (const Entry &entry : list) {
                facts.push_back(StoredFact{
                    .kind    = key.kind,
                    .anchors = entry.anchors,
                    .value   = entry.value,
                    .stamp   = entry.stamp,
                });
            }
        }
    }
    // A stable order for printing: by kind, then by anchors.
    std::sort(facts.begin(), facts.end(), [](const StoredFact &a, const StoredFact &b) {
        return std::tie(a.kind, a.anchors) < std::tie(b.kind, b.anchors);
    });
    for (const StoredFact &fact : facts) {
        visitor(fact); // outside the lock: the visitor may read the store
    }
}

size_t FactStore::size() const {
    std::lock_guard lock(mutex_);
    size_t n = 0;
    for (const auto &[_, list] : entries_) {
        n += list.size();
    }
    return n;
}

Counts FactStore::counts() const {
    std::lock_guard lock(mutex_);
    return counts_;
}

void FactStore::resetCounts() {
    std::lock_guard lock(mutex_);
    counts_ = {};
}

// ---- Bulk computation and the dump -----------------------------------------------------------

void computeFacts(FactStore &store, GCGraph *root) {
    const std::vector<GCGraph *> graphs = collectReachableGraphs(root);
    for (KindId kind : registeredKinds()) {
        const KindInfo &info = kindInfo(kind);
        if (!info.compute || info.cls == FactClass::GraphContent) {
            continue;
        }
        for (const GCGraph *graph : graphs) {
            for (const std::vector<Anchor> &anchors : anchorsOf(info, *graph)) {
                (void)store.obtainValue(kind, anchors);
            }
        }
    }
}

void dumpFacts(FactStore &store, GCGraph *root, std::ostream &os) {
    const std::vector<GCGraph *> graphs = collectReachableGraphs(root);
    std::unordered_map<GraphId, const GCGraph *> byId;
    for (const GCGraph *graph : graphs) {
        byId.emplace(graph->id(), graph);
    }

    // The stored facts about these graphs whose stamps hold for them.
    std::vector<StoredFact> stored;
    store.forEach([&](const StoredFact &fact) { stored.push_back(fact); });
    for (const StoredFact &fact : stored) {
        const KindInfo &info = kindInfo(fact.kind);
        std::vector<Anchor> anchors;
        for (const auto &[id, node] : fact.anchors) {
            auto it = byId.find(id);
            if (it == byId.end()) {
                break;
            }
            anchors.push_back(Anchor{it->second, node});
        }
        if (anchors.size() != fact.anchors.size()) {
            continue; // about a graph this program does not have
        }
        const Status status = store.probe(fact.kind, anchors, fact.stamp.targetHash);
        if (status == Status::Valid || status == Status::Revalidated) {
            // The revisions printed are the anchors' (equal to the stamp's once it holds).
            os << "FACT " << factJson(info, anchors, fact.value.get(), &fact.stamp) << "\n";
        }
    }

    // Graph content (class i): read from the graphs.
    for (KindId kind : registeredKinds()) {
        const KindInfo &info = kindInfo(kind);
        if (info.cls != FactClass::GraphContent) {
            continue;
        }
        for (const GCGraph *graph : graphs) {
            for (const std::vector<Anchor> &anchors : anchorsOf(info, *graph)) {
                if (Value value = info.compute(anchors)) {
                    os << "FACT " << factJson(info, anchors, value.get(), nullptr) << "\n";
                }
            }
        }
    }
}

} // namespace camel::runtime::fact
