// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Export the materialized runtime graph: IDs and references are data, never parsed labels.

#include "camel/passes/trans/json/gir.h"
#include "camel/core/node_attrs.h"
#include "camel/core/tags.h"
#include "camel/core/operator_semantics.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/source/manager.h"
#include "camel/passes/trans/gir_metadata.h"
#include "camel/runtime/node_roles.h"
#include "camel/runtime/reachable.h"
#include "camel/runtime/writings.h"
#include <algorithm>
#include <format>
#include <nlohmann/json.hpp>
#include <stdexcept>
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

/// An attribute set as an object keyed by the key's name, empty for kNoAttrs.
json attrSetJson(uint32_t set) {
    namespace attr = camel::core::attr;
    json out       = json::object();
    for (const attr::Attr &entry : attr::entries(set)) {
        out[std::string(attr::keyName(entry.key))] = attrValueJson(entry.value);
    }
    return out;
}

} // namespace
using namespace camel::runtime;
using namespace camel::passes::gir;

namespace {

/// The class id of a value in an `egraph-serialize` document: the value's root writing, named
/// after the graph it belongs to (the ids of the display document are not reused by accident).
std::string egraphClassId(const std::string &graphId, gc_node_ref_t root) {
    return std::format("{}c{}", graphId, root);
}

///  The document `egraph-serialize` reads (§5.04 item 3): `nodes` maps every writing to its
/// operator, to the classes of the values it reads and to the class it is a writing of;
/// `root_eclasses` names the value each graph returns; `class_data` says what the cost model
/// makes of computing a value. The shape is the crate's and nothing else is added to it: a tool
/// that opens it (`egraph-visualizer`) is what the export is for. A control input orders a node
/// rather than being a value (design principles 3.3), so it is not a child.
std::string dumpEgraphRuntimeGraph(
    const Program &program, GCGraph *root, const camel::core::context::context_ptr_t &ctx) {
    const auto graphs = collectReachableGraphs(program, root);
    std::shared_ptr<const CostModel> model = findCostModel(kWritingsCostModel);
    if (!model) {
        throw std::runtime_error(
            std::format("There is no cost model '{}'.", kWritingsCostModel));
    }
    std::unordered_map<const GCGraph *, std::string> ids;
    std::unordered_map<const GCGraph *, GraphWritings> classes;
    for (size_t i = 0; i < graphs.size(); ++i) {
        ids[graphs[i]]       = std::format("g{}", i);
        classes[graphs[i]] = graphWritings(program, graphs[i], *model, ctx.get());
    }
    // The class of the value a node is a writing of: the node itself when its value has one
    // writing, the root of its class otherwise.
    const auto classId = [&](GCGraph *g, gc_node_ref_t node) {
        const auto &view = classes.at(g);
        const auto at    = view.nodes.find(node);
        return egraphClassId(ids.at(g), at == view.nodes.end() ? node : at->second.root);
    };
    json nodes     = json::object();
    json classData = json::object();
    json roots     = json::array();
    for (auto *g : graphs) {
        for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
            const auto ref   = it.ref();
            const auto *node = *it;
            if (node == nullptr) {
                continue;
            }
            json children = json::array();
            for (auto list : {g->normInputsOf(ref), g->withInputsOf(ref)}) {
                for (gc_node_ref_t input : list) {
                    children.push_back(classId(g, input));
                }
            }
            json entry{
                {"op", recordLabel(program, g, ref, *node)},
                {"children", std::move(children)},
                {"eclass", classId(g, ref)}};
            if (const auto at = classes.at(g).nodes.find(ref); at != classes.at(g).nodes.end()) {
                entry["cost"]           = at->second.own;
                classData[classId(g, ref)] = json{{"cost", at->second.value}};
            }
            nodes[std::format("{}n{}", ids.at(g), ref)] = std::move(entry);
        }
        // A graph hands one value to its caller: that value's class is a root of the term graph.
        if (const gc_node_ref_t exit = g->exitNodeRef(); exit != kInvalidNodeRef) {
            roots.push_back(classId(g, exit));
        }
    }
    json document{
        {"nodes", std::move(nodes)},
        {"root_eclasses", std::move(roots)},
        {"class_data", std::move(classData)}};
    return document.dump(2);
}

} // namespace

std::string dumpJsonRuntimeGraph(
    GCGraph *root, const camel::core::context::context_ptr_t &ctx, const JsonGirOptions &options) {
    if (!root || !root->hasNodePayload())
        return {};
    const Program *program = ctx ? ctx->programOfRuntimeGraph(root) : nullptr;
    return program ? dumpJsonRuntimeGraph(*program, root, ctx, options) : std::string{};
}

std::string dumpJsonRuntimeGraph(
    const Program &program, GCGraph *root, const camel::core::context::context_ptr_t &ctx,
    const JsonGirOptions &options) {
    if (!root || !root->hasNodePayload())
        return {};
    // The canonical document is a separate shape, not a variation of the display document: the
    // version 1 body below is untouched so that every existing reader keeps the exact bytes.
    if (options.egraph)
        return dumpEgraphRuntimeGraph(program, root, ctx);
    if (options.canonical)
        return dumpCanonicalRuntimeGraph(program, root);
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
        // The regions, when the graph has any besides the body (an inlined function is one), and
        // the body itself when it carries a fact (the tags written on the function). A body
        // region with no attributes is left out, so a graph without tags keeps its form.
        const uint32_t bodyAttrs =
            g->regionCount() > 0 ? g->region(0)->attrs : camel::core::attr::kNoAttrs;
        if (g->regionCount() > 1 || bodyAttrs != camel::core::attr::kNoAttrs) {
            static const char *const kRegionKinds[] =
                {"body", "arm", "loop_cond", "loop_body", "inline", "block"};
            json regions = json::array();
            if (bodyAttrs != camel::core::attr::kNoAttrs) {
                regions.push_back(json{
                    {"id", std::format("{}r0", ids.at(g))},
                    {"kind", "body"},
                    {"attrs", attrSetJson(bodyAttrs)}});
            }
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
                if (region.attrs != camel::core::attr::kNoAttrs) {
                    entry["attrs"] = attrSetJson(region.attrs);
                }
                regions.push_back(std::move(entry));
            }
            graph["regions"] = std::move(regions);
        }
        //  The values with several writings: their classes, the writings of each and the
        // one extraction would take by the cost model the pipeline itself uses. A graph with one
        // writing per value prints no `classes` key, so every existing reader keeps its bytes.
        if (!g->executable()) {
            std::shared_ptr<const CostModel> model = findCostModel(kWritingsCostModel);
            if (model) {
                const GraphWritings classes = graphWritings(program, g, *model, ctx.get());
                if (!classes.empty()) {
                    json list = json::array();
                    for (const auto &value : classes.values) {
                        json writings = json::array();
                        for (gc_node_ref_t member : value.writings)
                            writings.push_back(nodeId(g, member));
                        list.push_back(json{
                            {"id", std::format("{}c{}", ids.at(g), value.root)},
                            {"writings", std::move(writings)},
                            {"chosen", nodeId(g, value.chosen)},
                            {"cost", value.cost}});
                    }
                    graph["classes"] = std::move(list);
                }
            }
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
            if (const std::string_view name = g->nodeName(ref); !name.empty())
                value["name"] = std::string(name);
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
                    {"body", graphId(program.loopBodyOf(*g, ref))},
                    {"conditionRegion", g->loopRegionOf(ref, camel::runtime::GCRegionKind::LoopCond)},
                    {"bodyRegion", g->loopRegionOf(ref, camel::runtime::GCRegionKind::LoopBody)}};
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
            const camel::core::tag::Facts facts = camel::core::tag::factsOf(*g, ref);
            if (facts.permissions)
                value["numeric"] = camel::core::sem::permissionsText(facts.permissions);
            if (facts.tags != 0) {
                value["attrs"] = json::object();
                for (const auto &a : camel::core::attr::entries(facts.tags))
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
            // An order edge and a sync edge are not ports: they live in the graph's own arrays,
            // which is what `std::gir` draws them from too, so neither has an input slot and
            // neither is addressed by one (docs/design/effects/edges.cn.md 1.5, 5). A dump that
            // left them out showed a `sync` block's statements joined by nothing.
            for (const camel::runtime::GCGraphOrderEdge &edge : g->orderPredsOf(ref)) {
                if (!g->containsNodeRef(edge.pred)) {
                    continue;
                }
                graph["edges"].push_back(
                    {{"source", nodeId(g, edge.pred)},
                     {"target", nodeId(g, ref)},
                     {"kind", "order"}});
            }
            for (const camel::runtime::GCGraphSyncEdge &edge : g->syncPredsOf(ref)) {
                if (!g->containsNodeRef(edge.pred)) {
                    continue;
                }
                graph["edges"].push_back(
                    {{"source", nodeId(g, edge.pred)},
                     {"target", nodeId(g, ref)},
                     {"kind", "sync"}});
            }
        }
        document["graphs"].push_back(std::move(graph));
    }
    return document.dump(2);
}
const camel::runtime::Program *
JsonGirDumpPass::apply(const camel::runtime::Program *program, std::ostream &out) {
    camel::runtime::GCGraph *graph = program->root();
    out << dumpJsonRuntimeGraph(*program, graph, context_, options_) << '\n';
    return nullptr;
}

PassMaker jgirPass() {
    return [](const camel::core::context::context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<JsonGirDumpPass>(
            ctx,
            JsonGirOptions{
                .canonical = args.getBool("canonical"), .egraph = args.getBool("egraph")});
    };
}
