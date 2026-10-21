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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The persistence hooks of graph facts (camel/runtime/fact_persist.h).
 *
 * The text form of a PortableFact is one line of tab-separated fields:
 *   kind <TAB> version <TAB> anchors <TAB> hashes <TAB> value
 * anchors: `graph#node` (node `-` for a graph as a whole) joined by `;`, the graph by its name in
 * the program (qualifiedNames); hashes: 16 hex
 * digits each, joined by `;`; the value is the kind's encoding, which must not contain a tab or
 * a newline (the core's encodings are short tokens).
 */

#include "camel/runtime/fact_persist.h"

#include "camel/core/node_attrs.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/reachable.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <format>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace camel::runtime::fact {

namespace {

/// FNV-1a, 64 bits: the same in every process and on every platform for the same bytes.
struct Fnv {
    uint64_t h = 0xcbf29ce484222325ULL;
    void bytes(const void *data, size_t size) {
        const auto *p = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < size; ++i) {
            h = (h ^ p[i]) * 0x100000001b3ULL;
        }
    }
    void u64(uint64_t value) {
        unsigned char b[8];
        for (int i = 0; i < 8; ++i) {
            b[i] = static_cast<unsigned char>(value >> (8 * i)); // little-endian, explicitly
        }
        bytes(b, 8);
    }
    void str(std::string_view text) {
        u64(text.size());
        bytes(text.data(), text.size());
    }
    void refs(std::span<const gc_node_ref_t> nodes) {
        u64(nodes.size());
        for (gc_node_ref_t node : nodes) {
            u64(node);
        }
    }
};

std::string mangled(const camel::core::type::Type *type) { return type ? type->mangle() : ""; }

/// The value of a static constant: a scalar by its bits, an object by its printed form.
void hashStatic(Fnv &h, const GCGraph &graph, const GCNode &node) {
    const camel::core::type::Type *type = node.dataType();
    const slot_t slot                   = graph.staticArea()->get<slot_t>(size_t(-node.dataIndex));
    if (type == nullptr || !type->isGCTraced()) {
        h.u64(static_cast<uint64_t>(slot));
        return;
    }
    auto *object = camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(slot);
    if (object == nullptr) {
        h.str("null");
        return;
    }
    std::ostringstream text;
    object->print(text, type);
    h.str(text.str());
}

/// The content of one graph, with the graphs it refers to counted by their names.
uint64_t ownContentHash(const Program &program, const GCGraph &graph) {
    const auto &table = camel::core::OperatorTable::instance();
    Fnv h;
    h.str(graph.name());
    h.str(mangled(graph.funcType()));
    h.refs(graph.normPorts());
    h.refs(graph.withPorts());
    h.refs(graph.closureNodes());
    h.u64(graph.exitNodeRef());
    h.u64(graph.outputNodeRef());
    h.u64(graph.returnNodeRef());
    h.u64(static_cast<uint64_t>(graph.returnKind()));
    for (gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
        const GCNode &node = *graph.node(ref);
        h.u64(static_cast<uint64_t>(node.kind));
        h.u64(node.flags);
        h.u64(static_cast<uint64_t>(static_cast<int64_t>(node.dataIndex)));
        h.str(mangled(node.dataType()));
        h.u64(graph.nodePermissions(ref));
        // Attributes by their names (the interned ids differ between processes); nothing for a
        // node without any, so that the hash of a graph without attributes is unchanged.
        if (const uint32_t attrs = graph.nodeAttrs(ref); attrs != 0) {
            h.str("attrs:" + camel::core::attr::text(attrs));
        }
        h.refs(graph.normInputsOf(ref));
        h.refs(graph.withInputsOf(ref));
        h.refs(graph.ctrlInputsOf(ref));
        switch (node.kind) {
        case GCNodeKind::Oper: {
            const camel::core::OperId oper = graph.nodeBodyAs<GCOperBody>(ref)->oper;
            h.str(table.uri(oper));
            const auto key = table.key(oper);
            h.str(key ? key->toString() : "");
            const auto traits = table.find(oper);
            h.u64(
                !traits
                    ? 0xFF
                    : (uint64_t(traits->pure) | uint64_t(traits->elementwise) << 1 |
                       uint64_t(traits->stateOnly) << 2 | uint64_t(traits->mayFault) << 3 |
                       uint64_t(traits->callsGraphs) << 4 | uint64_t(traits->inspectsGraphs) << 5));
            break;
        }
        case GCNodeKind::Func:
            h.str(program.calleeOf(graph, ref) ? program.calleeOf(graph, ref)->name() : "");
            break;
        case GCNodeKind::Loop:
            h.u64(static_cast<uint64_t>(graph.loopKindOf(ref)));
            h.str(program.loopCondOf(graph, ref) ? program.loopCondOf(graph, ref)->name() : "");
            h.str(program.loopBodyOf(graph, ref)->name());
            break;
        case GCNodeKind::Accs: {
            const auto *body = graph.nodeBodyAs<GCAccsBody>(ref);
            h.u64(static_cast<uint64_t>(body->accsKind));
            if (body->accsKind == GCAccsKind::StructKey) {
                h.str(body->key()); // a symbol id is process-local; its text is not
            } else {
                h.u64(body->value);
            }
            break;
        }
        case GCNodeKind::Fill: {
            const auto *body = graph.nodeBodyAs<GCFillBody>(ref);
            h.u64(static_cast<uint64_t>(body->fillKind));
            for (gc_slot_idx_t slot : body->slots()) {
                h.u64(static_cast<uint64_t>(static_cast<int64_t>(slot)));
            }
            break;
        }
        case GCNodeKind::Brch:
            h.u64(graph.nodeBodyAs<GCBrchBody>(ref)->join);
            h.u64(graph.nodeBodyAs<GCBrchBody>(ref)->armCount);
            break;
        case GCNodeKind::Join:
            h.u64(graph.nodeBodyAs<GCJoinBody>(ref)->brch);
            h.u64(graph.nodeBodyAs<GCJoinBody>(ref)->armCount);
            break;
        case GCNodeKind::Call:
            h.u64(static_cast<uint64_t>(graph.nodeBodyAs<GCCallBody>(ref)->callKind));
            break;
        case GCNodeKind::Data:
            if (node.dataIndex < 0 && graph.staticArea() != nullptr) {
                hashStatic(h, graph, node);
            }
            break;
        default:
            break;
        }
    }
    return h.h;
}

std::string_view field(std::string_view &rest) {
    const size_t tab           = rest.find('\t');
    const std::string_view out = rest.substr(0, tab);
    rest = tab == std::string_view::npos ? std::string_view{} : rest.substr(tab + 1);
    return out;
}

std::vector<std::string_view> split(std::string_view text, char sep) {
    std::vector<std::string_view> parts;
    if (text.empty()) {
        return parts;
    }
    size_t start = 0;
    while (true) {
        const size_t at = text.find(sep, start);
        parts.push_back(text.substr(start, at == std::string_view::npos ? text.npos : at - start));
        if (at == std::string_view::npos) {
            return parts;
        }
        start = at + 1;
    }
}

/// The graphs of a program by the names a PortableFact uses for them: the graph's name, and for
/// the second and every later graph of one name, in the order of the GraphIds, the name with
/// `~<ordinal>` appended. A name is only displayed and compared as a whole, never taken apart;
/// it is the same in every process that builds the program the same way.
std::unordered_map<const GCGraph *, std::string>
qualifiedNames(const std::vector<GCGraph *> &graphs) {
    std::vector<const GCGraph *> ordered(graphs.begin(), graphs.end());
    std::sort(ordered.begin(), ordered.end(), [](const GCGraph *a, const GCGraph *b) {
        return toValue(a->id()) < toValue(b->id());
    });
    std::unordered_map<std::string, size_t> seen;
    std::unordered_map<const GCGraph *, std::string> names;
    for (const GCGraph *graph : ordered) {
        const size_t ordinal = ++seen[graph->name()];
        names.emplace(
            graph,
            ordinal == 1 ? graph->name() : std::format("{}~{}", graph->name(), ordinal));
    }
    return names;
}

template <typename T> std::optional<T> number(std::string_view text, int base = 10) {
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

uint64_t portableGraphHash(const Program &program, const GCGraph *graph) {
    // The graph and everything below it, each once, in the order the reference tables give.
    Fnv h;
    std::queue<const GCGraph *> work;
    std::unordered_set<const GCGraph *> seen;
    work.push(graph);
    seen.insert(graph);
    while (!work.empty()) {
        const GCGraph *next = work.front();
        work.pop();
        h.u64(ownContentHash(program, *next));
        program.traceGraphs(*next, [&](GCGraph *ref) {
            if (seen.insert(ref).second) {
                work.push(ref);
            }
        });
    }
    return h.h;
}

std::string toText(const PortableFact &fact) {
    std::string anchors, hashes;
    for (const auto &[graph, node] : fact.anchors) {
        anchors += std::format(
            "{}{}#{}",
            anchors.empty() ? "" : ";",
            graph,
            node == kInvalidNodeRef ? std::string("-") : std::to_string(node));
    }
    for (uint64_t hash : fact.graphHashes) {
        hashes += std::format("{}{:016x}", hashes.empty() ? "" : ";", hash);
    }
    return std::format("{}\t{}\t{}\t{}\t{}", fact.kind, fact.version, anchors, hashes, fact.value);
}

std::optional<PortableFact> parsePortableFact(std::string_view line) {
    PortableFact fact;
    std::string_view rest          = line;
    fact.kind                      = std::string(field(rest));
    const auto version             = number<uint16_t>(field(rest));
    const std::string_view anchors = field(rest);
    const std::string_view hashes  = field(rest);
    fact.value                     = std::string(rest);
    if (fact.kind.empty() || !version || anchors.empty()) {
        return std::nullopt;
    }
    fact.version = *version;
    for (std::string_view anchor : split(anchors, ';')) {
        const size_t hash = anchor.rfind('#');
        if (hash == std::string_view::npos) {
            return std::nullopt;
        }
        const std::string_view node = anchor.substr(hash + 1);
        gc_node_ref_t ref           = kInvalidNodeRef;
        if (node != "-") {
            const auto parsed = number<gc_node_ref_t>(node);
            if (!parsed) {
                return std::nullopt;
            }
            ref = *parsed;
        }
        fact.anchors.emplace_back(std::string(anchor.substr(0, hash)), ref);
    }
    for (std::string_view text : split(hashes, ';')) {
        const auto value = number<uint64_t>(text, 16);
        if (!value) {
            return std::nullopt;
        }
        fact.graphHashes.push_back(*value);
    }
    return fact;
}

std::vector<PortableFact> exportFacts(FactStore &store, const Program &program) {
    const std::vector<GCGraph *> graphs = collectReachableGraphs(program);
    std::unordered_map<GraphId, const GCGraph *> byId;
    for (const GCGraph *graph : graphs) {
        byId.emplace(graph->id(), graph);
    }
    const auto names = qualifiedNames(graphs);
    std::unordered_map<const GCGraph *, uint64_t> hashes;
    const auto portable =
        [&](const KindInfo &kind, std::span<const Anchor> anchors, const void *value) {
            PortableFact fact{.kind = kind.key, .version = kind.version};
            std::vector<const GCGraph *> distinct;
            for (const Anchor &anchor : anchors) {
                fact.anchors.emplace_back(names.at(anchor.graph), anchor.node);
                if (std::find(distinct.begin(), distinct.end(), anchor.graph) == distinct.end()) {
                    distinct.push_back(anchor.graph);
                    auto [it, inserted] = hashes.try_emplace(anchor.graph, 0);
                    if (inserted) {
                        it->second = portableGraphHash(program, anchor.graph);
                    }
                    fact.graphHashes.push_back(it->second);
                }
            }
            fact.value = kind.encode(value);
            return fact;
        };

    std::vector<PortableFact> out;
    std::vector<StoredFact> stored;
    store.forEach([&](const StoredFact &fact) { stored.push_back(fact); });
    for (const StoredFact &fact : stored) {
        const KindInfo &kind = kindInfo(fact.kind);
        if (kind.cls != FactClass::Derived || !kind.encode) {
            continue;
        }
        std::vector<Anchor> anchors;
        for (const auto &[id, node] : fact.anchors) {
            if (auto it = byId.find(id); it != byId.end()) {
                anchors.push_back(Anchor{&program, it->second, node});
            }
        }
        if (anchors.size() == fact.anchors.size() &&
            store.probe(fact.kind, anchors) == Status::Valid) {
            out.push_back(portable(kind, anchors, fact.value.get()));
        }
    }
    for (KindId id : registeredKinds()) {
        const KindInfo &kind = kindInfo(id);
        if (kind.cls != FactClass::GraphContent || !kind.encode) {
            continue;
        }
        for (const GCGraph *graph : graphs) {
            const auto all = kind.anchorsIn ? kind.anchorsIn(program, *graph) : [&] {
                std::vector<std::vector<Anchor>> each;
                if (kind.scope == Scope::Graph) {
                    each.push_back({Anchor{&program, graph}});
                } else {
                    for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
                        each.push_back({Anchor{&program, graph, ref}});
                    }
                }
                return each;
            }();
            for (const auto &anchors : all) {
                if (Value value = kind.compute(anchors)) {
                    out.push_back(portable(kind, anchors, value.get()));
                }
            }
        }
    }
    return out;
}

std::string_view importResultName(ImportResult result) {
    switch (result) {
    case ImportResult::Stored:
        return "stored";
    case ImportResult::MatchesGraph:
        return "matches the graph";
    case ImportResult::UnknownKind:
        return "unknown kind";
    case ImportResult::OtherVersion:
        return "other version";
    case ImportResult::UnknownGraph:
        return "unknown graph";
    case ImportResult::GraphChanged:
        return "graph changed";
    case ImportResult::BadValue:
        return "bad value";
    case ImportResult::DiffersFromGraph:
        return "differs from the graph";
    }
    return "?";
}

ImportResult importFact(FactStore &store, const Program &program, const PortableFact &fact) {
    const KindId id = findKind(fact.kind);
    if (id == kNoKind || !kindInfo(id).decode || kindInfo(id).cls == FactClass::Target) {
        return ImportResult::UnknownKind;
    }
    const KindInfo &kind = kindInfo(id);
    if (kind.version != fact.version) {
        return ImportResult::OtherVersion;
    }
    const std::vector<GCGraph *> programGraphs = collectReachableGraphs(program);
    const auto names                           = qualifiedNames(programGraphs);
    std::unordered_map<std::string_view, const GCGraph *> byName;
    for (const GCGraph *graph : programGraphs) {
        byName.emplace(names.at(graph), graph);
    }
    std::vector<Anchor> anchors;
    std::vector<const GCGraph *> distinct;
    for (const auto &[name, node] : fact.anchors) {
        auto it = byName.find(name);
        if (it == byName.end() || (node != kInvalidNodeRef && !it->second->containsNodeRef(node))) {
            return ImportResult::UnknownGraph;
        }
        anchors.push_back(Anchor{&program, it->second, node});
        if (std::find(distinct.begin(), distinct.end(), it->second) == distinct.end()) {
            distinct.push_back(it->second);
        }
    }
    if (distinct.size() != fact.graphHashes.size()) {
        return ImportResult::GraphChanged;
    }
    for (size_t i = 0; i < distinct.size(); ++i) {
        if (portableGraphHash(program, distinct[i]) != fact.graphHashes[i]) {
            return ImportResult::GraphChanged;
        }
    }
    Value value = kind.decode(fact.value);
    if (!value) {
        return ImportResult::BadValue;
    }
    if (kind.cls == FactClass::GraphContent) {
        const Value current = kind.compute(anchors);
        return current && kind.equal(current.get(), value.get()) ? ImportResult::MatchesGraph
                                                                 : ImportResult::DiffersFromGraph;
    }
    store.putValue(id, anchors, std::move(value));
    return ImportResult::Stored;
}

} // namespace camel::runtime::fact
