/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Feb. 22, 2026
 * Updated: Mar. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/**
 * GIR 懒加载 JSON 序列化实现。
 */

#include <stdio.h>

#ifndef EOF
#define EOF (-1)
#endif

#include "camel/compile/gir/graph.h"
#include "camel/compile/gir/nodes.h"
#include "camel/compile/gir/types.h"
#include "camel/core/error/diagnostics/range.h"
#include "camel/core/source/manager.h"
#include "camel/utils/type.h"
#include "gir_json.h"

#ifndef EOF
#define EOF (-1)
#endif

#include <format>
#include <nlohmann/json.hpp>
#include <queue>
#include <unordered_set>

using namespace camel::core::error;

namespace debugger {

namespace {

using json = nlohmann::json;
using namespace GIR;

namespace {
constexpr std::size_t kSourceContextExtraIndex = 3;
}

std::string ptrToId(const void *ptr) {
    return std::format("0x{:x}", reinterpret_cast<uintptr_t>(ptr));
}

camel::source::SourceContext *sourceContextForGraph(const graph_ptr_t &graph) {
    return graph ? graph->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()
                 : nullptr;
}

json rangeToJson(const CharRange &range) {
    return {
        {"start", {{"line", range.start.line}, {"character", range.start.character}}},
        {"end", {{"line", range.end.line}, {"character", range.end.character}}}};
}

json originToJson(camel::source::SourceContext *sourceContext, camel::source::origin_id_t origin) {
    json j;
    j["id"] = origin;
    if (!sourceContext || origin == camel::source::kInvalidOriginId) {
        return j;
    }
    if (const auto *record = sourceContext->origin(origin)) {
        j["stage"]     = camel::source::to_string(record->stage);
        j["kind"]      = camel::source::to_string(record->kind);
        j["label"]     = record->label;
        j["synthetic"] = record->synthetic;
        j["parentId"] =
            record->parent == camel::source::kInvalidOriginId ? json() : json(record->parent);
        j["sourcePath"] = sourceContext->pathForOrigin(origin);
        j["sourceSpan"] = rangeToJson(sourceContext->resolveOrigin(origin));
        j["inputs"]     = json::array();
        for (auto input : record->inputs) {
            j["inputs"].push_back(input);
        }
    }
    return j;
}

json semanticBundleToJson(
    camel::source::SourceContext *sourceContext, const camel::source::SemanticBundle *bundle) {
    if (!bundle) {
        return json();
    }
    json j;
    j["mainOrigin"]      = originToJson(sourceContext, bundle->mainOrigin);
    j["mergedInputs"]    = json::array();
    j["semanticParts"]   = json::array();
    j["synthetic"]       = bundle->synthetic;
    j["syntheticReason"] = bundle->syntheticReason;
    for (auto input : bundle->mergedInputs) {
        j["mergedInputs"].push_back(originToJson(sourceContext, input));
    }
    for (const auto &part : bundle->parts) {
        json p;
        p["role"]   = camel::source::to_string(part.role);
        p["label"]  = part.label;
        p["slot"]   = part.slot;
        p["origin"] = originToJson(sourceContext, part.origin);
        j["semanticParts"].push_back(p);
    }
    return j;
}

void attachOriginAndSemantic(
    json &j, camel::source::SourceContext *sourceContext, camel::source::origin_id_t origin,
    const camel::source::SemanticBundle *bundle, const char *generatedFrom) {
    if (!sourceContext || origin == camel::source::kInvalidOriginId) {
        return;
    }
    j["originId"]      = origin;
    j["sourceSpan"]    = rangeToJson(sourceContext->resolveOrigin(origin));
    j["sourcePath"]    = sourceContext->pathForOrigin(origin);
    j["generatedFrom"] = generatedFrom;
    j["origin"]        = originToJson(sourceContext, origin);
    if (bundle) {
        j["semantic"] = semanticBundleToJson(sourceContext, bundle);
    }
}

std::string inferEdgeSemanticRole(Node *to, const std::string &linkType, size_t inIdx) {
    if (!to) {
        return "";
    }
    if (linkType == "Norm") {
        switch (to->type()) {
        case NodeType::CALL:
        case NodeType::OPER:
        case NodeType::FUNC:
            return "argument";
        case NodeType::BRCH:
            return "branchCondition";
        case NodeType::JOIN:
            return "branchIndex";
        case NodeType::FILL:
            return inIdx == 0 ? "valueProducer" : "fillInput";
        case NodeType::ACCS:
            return "receiver";
        case NodeType::CAST:
            return "valueProducer";
        case NodeType::EXIT:
            return "valueProducer";
        default:
            return "normInput";
        }
    }
    if (linkType == "With") {
        switch (to->type()) {
        case NodeType::CALL:
            return inIdx == 0 ? "callee" : "withArgument";
        case NodeType::BRCH:
            return "caseValue";
        case NodeType::JOIN:
            return "branchResult";
        case NodeType::FILL:
            return "capture";
        case NodeType::FUNC:
        case NodeType::OPER:
            return "withArgument";
        default:
            return "withInput";
        }
    }
    if (linkType == "Ctrl") {
        switch (to->type()) {
        case NodeType::BRCH:
            return "captureReady";
        case NodeType::FUNC:
            return "branchLaunch";
        case NodeType::EXIT:
            return "returnBarrier";
        default:
            return "control";
        }
    }
    return "";
}

/// 单个图的摘要：id 使用稳定 API stableId()，name, funcTypeSummary（用于 children/dependencies
/// 数组项）
json graphSummary(const graph_ptr_t &g) {
    json j;
    j["id"]   = g->stableId();
    j["name"] = g->name();
    if (g->funcType())
        j["funcTypeSummary"] = g->funcType()->toString();
    if (auto *sourceContext = sourceContextForGraph(g)) {
        auto origin = sourceContext->debugMap().graphOrigin(g->stableId());
        attachOriginAndSemantic(
            j,
            sourceContext,
            origin,
            sourceContext->girGraphSemantic(g->stableId()),
            "graph");
    }
    return j;
}

/// 根图摘要：无 nodes/edges，含 children 与 dependencies
json rootSummaryToJson(const graph_ptr_t &root) {
    json graph            = graphSummary(root);
    graph["children"]     = json::array();
    graph["dependencies"] = json::array();

    for (const auto &[name, subSet] : root->subGraphs()) {
        for (const auto &sub : subSet) {
            if (sub)
                graph["children"].push_back(graphSummary(sub));
        }
    }
    for (const auto &dep : root->dependencies()) {
        if (dep)
            graph["dependencies"].push_back(graphSummary(dep));
    }

    return graph;
}

/// 从 root 起 BFS 查找 id 匹配的图（id 为 stableId）
graph_ptr_t findGraphById(const graph_ptr_t &root, const std::string &graphId) {
    if (!root || graphId.empty())
        return nullptr;
    if (root->stableId() == graphId)
        return root;
    std::queue<graph_ptr_t> q;
    std::unordered_set<Graph *> seen;
    q.push(root);
    seen.insert(root.get());
    while (!q.empty()) {
        graph_ptr_t g = q.front();
        q.pop();
        for (const auto &[name, subSet] : g->subGraphs()) {
            for (const auto &sub : subSet) {
                if (sub && seen.insert(sub.get()).second) {
                    if (sub->stableId() == graphId)
                        return sub;
                    q.push(sub);
                }
            }
        }
        for (const auto &dep : g->dependencies()) {
            if (dep && seen.insert(dep.get()).second) {
                if (dep->stableId() == graphId)
                    return dep;
                q.push(dep);
            }
        }
    }
    return nullptr;
}

/// 节点 label / shape / style（与 graphviz 一致）
void nodeLabelShapeStyle(Node *node, std::string &label, std::string &shape, std::string &style) {
    label = "";
    shape = "circle";
    style = "solid";

    switch (node->type()) {
    case NodeType::DATA: {
        auto sourceNode = tt::as_ptr<DataNode>(node);
        label           = sourceNode->data() ? sourceNode->data()->toString() : "";
        break;
    }
    case NodeType::PORT: {
        auto portNode = tt::as_ptr<PortNode>(node);
        label         = portNode->name();
        break;
    }
    case NodeType::CAST:
        label = "CAST";
        shape = "diamond";
        break;
    case NodeType::COPY:
        label = "COPY";
        shape = "diamond";
        break;
    case NodeType::FILL:
        label = "FILL";
        shape = "diamond";
        break;
    case NodeType::ACCS: {
        auto accsNode = tt::as_ptr<AccsNode>(node);
        label         = "." + accsNode->index2String();
        shape         = "diamond";
        break;
    }
    case NodeType::BRCH:
        label = "BRCH";
        shape = "diamond";
        break;
    case NodeType::JOIN:
        label = "JOIN";
        shape = "diamond";
        break;
    case NodeType::CALL:
        label = "CALL";
        shape = "diamond";
        break;
    case NodeType::BIND:
        label = "BIND";
        shape = "diamond";
        break;
    case NodeType::FUNC: {
        auto *func = tt::as_ptr<FuncNode>(node)->func();
        label      = func->name().empty() ? func->graph().name() : func->name();
        shape      = "Mdiamond";
        break;
    }
    case NodeType::OPER: {
        auto oper = tt::as_ptr<OperNode>(node);
        label     = oper->oper()->name();
        shape     = "diamond";
        break;
    }
    case NodeType::EXIT:
        label = "EXIT";
        shape = "doublecircle";
        break;
    case NodeType::SYNC:
        label = "SYNC";
        shape = "diamond";
        style = "dashed";
        break;
    case NodeType::NREF:
        label = "NREF";
        shape = "diamond";
        style = "dashed";
        break;
    case NodeType::DREF:
        label = "DREF";
        shape = "diamond";
        break;
    default:
        label = to_string(node->type());
        break;
    }
}

/// 按类型写入原始字段，供前端自行拼展示；不输出 label/tooltip
void nodeRawFields(Node *node, json &j) {
    switch (node->type()) {
    case NodeType::DATA: {
        auto sourceNode = tt::as_ptr<DataNode>(node);
        j["dataRepr"]   = sourceNode->data() ? sourceNode->data()->toString() : "";
        break;
    }
    case NodeType::PORT: {
        auto portNode = tt::as_ptr<PortNode>(node);
        j["portName"] = portNode->name();
        break;
    }
    case NodeType::ACCS: {
        auto accsNode  = tt::as_ptr<AccsNode>(node);
        j["indexRepr"] = accsNode->index2String();
        break;
    }
    case NodeType::FUNC: {
        auto *func         = tt::as_ptr<FuncNode>(node)->func();
        j["funcName"]      = func->name();
        j["funcGraphName"] = func->graph().name();
        break;
    }
    case NodeType::OPER: {
        auto oper     = tt::as_ptr<OperNode>(node);
        j["operName"] = oper->oper()->name();
        break;
    }
    default:
        break;
    }
}

json nodeToJson(Node *node) {
    json j;
    j["id"]       = ptrToId(node);
    j["graphId"]  = node->graph().stableId();
    j["stableId"] = node->stableId();
    j["type"]     = to_string(node->type());
    std::string label, shape, style;
    nodeLabelShapeStyle(node, label, shape, style);
    j["label"]     = label;
    j["shape"]     = shape;
    j["style"]     = style;
    j["graphName"] = node->graph().name();
    j["nodeRepr"]  = node->toString();
    if (auto *sourceContext =
            node->graph().getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
        auto origin = sourceContext->debugMap().nodeOrigin(node->stableId());
        attachOriginAndSemantic(
            j,
            sourceContext,
            origin,
            sourceContext->girNodeSemantic(node->stableId()),
            "node");
    }
    nodeRawFields(node, j);
    return j;
}

/// 单图展开：nodes, edges, children, dependencies；图 id 使用 stableId()
json expandedGraphToJson(const graph_ptr_t &graph) {
    json j            = graphSummary(graph);
    j["parentId"]     = graph->isRoot() ? json() : json(graph->outer()->stableId());
    j["children"]     = json::array();
    j["dependencies"] = json::array();
    j["nodes"]        = json::array();
    j["edges"]        = json::array();

    for (const auto &[name, subSet] : graph->subGraphs()) {
        for (const auto &sub : subSet)
            if (sub)
                j["children"].push_back(graphSummary(sub));
    }
    for (const auto &dep : graph->dependencies())
        if (dep)
            j["dependencies"].push_back(graphSummary(dep));

    node_vec_t nodes;
    for (Node *n : graph->nodes())
        nodes.push_back(n);
    nodes.push_back(graph->exitNode());
    for (Node *port : graph->normPorts())
        nodes.push_back(port);
    for (Node *port : graph->withPorts())
        nodes.push_back(port);
    for (Node *c : graph->closure())
        nodes.push_back(c);

    for (Node *n : nodes)
        j["nodes"].push_back(nodeToJson(n));

    size_t edgeId = 0;
    auto addEdge =
        [&](Node *from, Node *to, const std::string &linkType, size_t outIdx, size_t inIdx) {
            json e;
            e["id"]              = "e" + std::to_string(edgeId++);
            e["sourceId"]        = ptrToId(from);
            e["targetId"]        = ptrToId(to);
            e["linkType"]        = linkType;
            e["semanticRole"]    = inferEdgeSemanticRole(to, linkType, inIdx);
            e["sourcePortIndex"] = outIdx;
            e["targetPortIndex"] = inIdx;
            j["edges"].push_back(e);
        };

    for (const auto &node : nodes) {
        auto withInputs = node->withInputs();
        for (size_t i = 0; i < withInputs.size(); ++i) {
            if (withInputs[i]) {
                size_t outIdx = 0;
                for (const auto &out : withInputs[i]->dataOutputs()) {
                    if (out == node)
                        break;
                    outIdx++;
                }
                addEdge(withInputs[i], node, "With", outIdx, i);
            }
        }
        auto normInputs = node->normInputs();
        for (size_t i = 0; i < normInputs.size(); ++i) {
            if (normInputs[i]) {
                size_t outIdx = 0;
                for (const auto &out : normInputs[i]->dataOutputs()) {
                    if (out == node)
                        break;
                    outIdx++;
                }
                addEdge(normInputs[i], node, "Norm", outIdx, i);
            }
        }
        auto ctrlInputs = node->ctrlInputs();
        for (size_t i = 0; i < ctrlInputs.size(); ++i) {
            if (ctrlInputs[i]) {
                size_t outIdx = 0;
                for (const auto &out : ctrlInputs[i]->ctrlOutputs()) {
                    if (out == node)
                        break;
                    outIdx++;
                }
                addEdge(ctrlInputs[i], node, "Ctrl", outIdx, i);
            }
        }
    }

    return j;
}

} // namespace

std::string getStableNodeId(const Node *node) {
    return node ? const_cast<Node *>(node)->stableId() : std::string{};
}

std::pair<std::string, std::string>
getGirJson(const graph_ptr_t &root, const std::string &graphId) {
    if (!root)
        return {"", "no graph"};

    if (graphId.empty()) {
        json out;
        out["graph"] = rootSummaryToJson(root);
        return {out.dump(), ""};
    }

    graph_ptr_t target = findGraphById(root, graphId);
    if (!target)
        return {"", "graph not found"};

    json out;
    out["graph"] = expandedGraphToJson(target);
    return {out.dump(), ""};
}

} // namespace debugger
