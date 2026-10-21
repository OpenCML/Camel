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
 * Author: Zhenjie Wei
 * Created: Oct. 21, 2024
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Export the materialized runtime graph: IDs and references are data, never parsed labels.
#include "camel/passes/trans/json/gir.h"
#include "camel/core/node_attrs.h"
#include "camel/core/operator_semantics.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/source/manager.h"
#include "camel/passes/trans/gir_metadata.h"
#include "camel/runtime/node_roles.h"
#include "camel/runtime/reachable.h"
#include <algorithm>
#include <format>
#include <nlohmann/json.hpp>
#include <unordered_map>
using nlohmann::json;

namespace {

/// A node attribute's value (camel/core/node_attrs.h): true for a mark, the bit indices of
/// flags, a number, a string, {"graph": stable id} for a graph, an array for a tuple.
json attrValueJson(camel::core::attr::AttrValueId id) {
    namespace attr      = camel::core::attr;
    const attr::Value v = attr::value(id);
    switch (v.kind) {
    case attr::ValueKind::Unit:
        return true;
    case attr::ValueKind::Flags: {
        json bits = json::array();
        for (size_t w = 0; w < v.words.size(); ++w) {
            for (size_t b = 0; b < 64; ++b) {
                if ((v.words[w] >> b) & 1) {
                    bits.push_back(w * 64 + b);
                }
            }
        }
        return bits;
    }
    case attr::ValueKind::Int:
        return v.integer;
    case attr::ValueKind::Symbol:
        return v.text;
    case attr::ValueKind::Graph:
        return json{{"graph", v.text}};
    case attr::ValueKind::Tuple: {
        json items = json::array();
        for (attr::AttrValueId item : v.items) {
            items.push_back(attrValueJson(item));
        }
        return items;
    }
    }
    return nullptr;
}

} // namespace
using namespace camel::runtime;
using namespace camel::passes::gir;

std::string dumpJsonRuntimeGraph(GCGraph *root, const camel::core::context::context_ptr_t &ctx) {
    if (!root || !root->hasNodePayload())
        return {};
    const Program *program = ctx ? ctx->programOfRuntimeGraph(root) : nullptr;
    return program ? dumpJsonRuntimeGraph(*program, root, ctx) : std::string{};
}

std::string dumpJsonRuntimeGraph(
    const Program &program, GCGraph *root, const camel::core::context::context_ptr_t &ctx) {
    if (!root || !root->hasNodePayload())
        return {};
    const auto graphs = collectReachableGraphs(program, root);
    std::unordered_map<const GCGraph *, std::string> ids;
    for (size_t i = 0; i < graphs.size(); ++i)
        ids[graphs[i]] = std::format("g{}", i);
    const auto graphId = [&](const GCGraph *g) -> json {
        auto at = ids.find(g);
        return at == ids.end() ? json(nullptr) : json(at->second);
    };
    const auto nodeId = [&](GCGraph *g, gc_node_ref_t n) {
        return std::format("{}n{}", ids.at(g), n);
    };
    json document{
        {"format", "camel.gir"},
        {"version", 1},
        {"root", ids.at(root)},
        {"graphs", json::array()}};
    for (auto *g : graphs) {
        json graph{
            {"id", ids.at(g)},
            {"name", g->name()},
            {"signature", graphTooltip(g)},
            {"dependencies", json::array()},
            {"nodes", json::array()},
            {"edges", json::array()},
            {"normalPorts", json::array()},
            {"contextPorts", json::array()},
            {"capturePorts", json::array()},
            {"exit", nodeId(g, g->exitNodeRef())}};
        program.traceGraphs(*g, [&](GCGraph *dep) {
            if (ids.contains(dep))
                graph["dependencies"].push_back(ids.at(dep));
        });
        for (auto n : g->normPorts())
            graph["normalPorts"].push_back(nodeId(g, n));
        for (auto n : g->withPorts())
            graph["contextPorts"].push_back(nodeId(g, n));
        for (auto n : g->closureNodes())
            graph["capturePorts"].push_back(nodeId(g, n));
        // The regions, when the graph has any besides the body (an inlined function is one).
        if (g->regionCount() > 1) {
            static const char *const kRegionKinds[] =
                {"body", "arm", "loop_cond", "loop_body", "inline", "block"};
            json regions = json::array();
            for (uint32_t r = 1; r < g->regionCount(); ++r) {
                const GCRegion &region = *g->region(r);
                if (region.kind == GCRegionKind::Dead) {
                    continue;
                }
                json entry{
                    {"id", std::format("{}r{}", ids.at(g), r)},
                    {"kind", kRegionKinds[static_cast<size_t>(region.kind)]},
                    {"parent", std::format("{}r{}", ids.at(g), region.parent)}};
                if (region.owner != kInvalidNodeRef)
                    entry["owner"] = nodeId(g, region.owner);
                if (region.result != kInvalidNodeRef)
                    entry["result"] = nodeId(g, region.result);
                if (region.kind == GCRegionKind::Inline) {
                    if (const GCGraph *source = program.graph(region.inlineOf))
                        entry["inlined"] = source->name();
                }
                regions.push_back(std::move(entry));
            }
            graph["regions"] = std::move(regions);
        }
        for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
            const auto ref   = it.ref();
            const auto *node = *it;
            if (!node)
                continue;
            const auto kind = recordType(*node);
            json value{
                {"id", nodeId(g, ref)},
                {"kind", kind},
                {"label", recordLabel(program, g, ref, *node)},
                {"outputType", nullptr}};
            if (const uint32_t region = g->nodeRegion(ref))
                value["region"] = std::format("{}r{}", ids.at(g), region);
            if (const auto *type = nodeStaticSlotType(g, *node))
                value["outputType"] = camel::core::type::sourceTypeSpelling(type->toString());
            if (node->kind == GCNodeKind::Port) {
                const auto port    = portInfo(g, ref);
                value["parameter"] = {
                    {"name", port.name},
                    {"kind", port.kind},
                    {"index", port.index}};
            }
            if (node->kind == GCNodeKind::Func)
                value["callee"] = graphId(program.calleeOf(*g, ref));
            if (node->kind == GCNodeKind::Data && node->dataIndex < 0) {
                auto *type      = nodeStaticSlotType(g, *node);
                const auto slot = static_cast<size_t>(-node->dataIndex);
                if (type && type->code() == camel::core::type::TypeCode::Function &&
                    slot < g->staticSlots().size()) {
                    const auto *fn =
                        camel::core::rtdata::fromSlot<::Function *>(g->staticSlots()[slot]);
                    if (fn)
                        value["callee"] = graphId(fn->graph());
                }
            }
            if (node->kind == GCNodeKind::Loop) {
                value["loop"] = {
                    {"kind", camel::core::loopKindName(g->loopKindOf(ref))},
                    {"condition", graphId(program.loopCondOf(*g, ref))},
                    {"body", graphId(program.loopBodyOf(*g, ref))}};
            }
            const auto origin = g->nodeOrigin(ref);
            if (origin) {
                // Text preserves the complete uint64 identity in JavaScript clients.
                value["origin"] = std::to_string(origin);
                if (ctx && ctx->sourceContext()) {
                    auto *sc           = ctx->sourceContext().get();
                    const auto *record = sc->origin(origin);
                    if (record && record->primarySpan != camel::source::kInvalidSpanId) {
                        const auto r    = sc->resolveOrigin(origin);
                        value["source"] = {
                            {"file", sc->pathForOrigin(origin)},
                            {"start", {{"line", r.start.line}, {"character", r.start.character}}},
                            {"end", {{"line", r.end.line}, {"character", r.end.character}}}};
                    }
                }
            }
            const auto permissions = g->nodePermissions(ref);
            if (permissions)
                value["numeric"] = camel::core::sem::permissionsText(permissions);
            if (const auto attrs = g->nodeAttrs(ref)) {
                value["attrs"] = json::object();
                for (const auto &a : camel::core::attr::entries(attrs))
                    value["attrs"][std::string(camel::core::attr::keyName(a.key))] =
                        attrValueJson(a.value);
            }
            value["roles"] = json::array();
            for (const auto &role : NodeRoleRegistry::instance().classify(*g, ref))
                value["roles"].push_back(role.spec.name);
            graph["nodes"].push_back(std::move(value));
            // Explicit edge classes and indices preserve duplicate inputs and their order.
            const auto edges = [&](const char *edgeKind, auto inputs, auto outputsOf) {
                for (size_t i = 0; i < inputs.size(); ++i) {
                    const auto source  = inputs[i];
                    const auto outputs = outputsOf(source);
                    // The runtime use index lists each consumer once per edge class,
                    // even when it reads this source in several ordered input slots.
                    // Keep every input edge, sharing its consumer's source-side index.
                    const auto at = std::find(outputs.begin(), outputs.end(), ref);
                    ASSERT(
                        at != outputs.end(),
                        "GIR JSON edge is missing its outgoing connection.");
                    graph["edges"].push_back(
                        {{"source", nodeId(g, source)},
                         {"target", nodeId(g, ref)},
                         {"kind", edgeKind},
                         {"sourceIndex", static_cast<size_t>(at - outputs.begin())},
                         {"targetIndex", i}});
                }
            };
            edges("normal", g->normInputsOf(ref), [&](auto source) {
                return g->normOutputsOf(source);
            });
            edges("context", g->withInputsOf(ref), [&](auto source) {
                return g->withOutputsOf(source);
            });
            edges("control", g->ctrlInputsOf(ref), [&](auto source) {
                return g->ctrlOutputsOf(source);
            });
        }
        document["graphs"].push_back(std::move(graph));
    }
    return document.dump(2);
}
const camel::runtime::Program *
JsonGirDumpPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    camel::runtime::GCGraph *graph = program->root();
    out << dumpJsonRuntimeGraph(*program, graph, context_) << '\n';
    return nullptr;
}
