/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Apr. 12, 2026
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Draft-based GIR debugger JSON export.
 *
 * The debugger only needs a cold-path structural dump. This implementation
 * walks DraftGraphBuilder and GraphDraft directly, without depending on the
 * deleted compile-time Graph/Node class hierarchy.
 */

#include "debug_json.h"

#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/compile/gir/types.h"
#include "camel/core/source/manager.h"

#include <cstdio>
#include <format>

#ifndef EOF
#define EOF (-1)
#endif

#include <nlohmann/json.hpp>
#include <queue>
#include <unordered_set>

namespace debugger {

namespace {

using json = nlohmann::json;
using camel::compile::gir::draft_node_ref_t;
using camel::compile::gir::DraftGraphBuilder;
using camel::runtime::DraftNode;
using camel::runtime::GCNodeKind;

constexpr std::size_t kSourceContextExtraIndex = 3;

camel::source::SourceContext *sourceContextForGraph(const DraftGraphBuilder *graph) {
    return graph ? graph->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()
                 : nullptr;
}

camel::source::gir_draft_node_key_t draftDebugKey(draft_node_ref_t nodeId) {
    return static_cast<camel::source::gir_draft_node_key_t>(nodeId);
}

std::string nodeKindName(GCNodeKind kind) {
    switch (kind) {
    case GCNodeKind::Data:
        return "DATA";
    case GCNodeKind::Port:
        return "PORT";
    case GCNodeKind::Cast:
        return "CAST";
    case GCNodeKind::Copy:
        return "COPY";
    case GCNodeKind::Fill:
        return "FILL";
    case GCNodeKind::Accs:
        return "ACCS";
    case GCNodeKind::Brch:
        return "BRCH";
    case GCNodeKind::Join:
        return "JOIN";
    case GCNodeKind::Call:
        return "CALL";
    case GCNodeKind::Bind:
        return "BIND";
    case GCNodeKind::Func:
        return "FUNC";
    case GCNodeKind::Oper:
        return "OPER";
    case GCNodeKind::Sync:
        return "SYNC";
    case GCNodeKind::Gate:
        return "GATE";
    case GCNodeKind::Dref:
        return "DREF";
    }
    return "UNKNOWN";
}

json graphSummary(const DraftGraphBuilder *graph) {
    json j;
    j["id"]   = graph ? graph->stableId() : "";
    j["name"] = graph ? graph->name() : "";
    if (graph && graph->funcType()) {
        j["funcTypeSummary"] = graph->funcType()->toString();
    }
    return j;
}

json nodeToJson(const DraftGraphBuilder *graph, draft_node_ref_t nodeId) {
    json j;
    const auto *node   = graph->draft().node(nodeId);
    const auto *header = node ? &node->header : nullptr;
    j["id"]            = nodeId;
    j["graphId"]       = graph->stableId();
    j["type"]          = header ? nodeKindName(header->kind) : "DEAD";
    j["stableId"] =
        header ? graph->nodeDebugEntityId(node) : std::format("dead:{}", static_cast<int>(nodeId));
    j["dataType"] = header && header->dataType ? header->dataType->toString() : "";
    if (header && header->kind == GCNodeKind::Port) {
        j["portName"] = graph->nodePortName(node);
    }
    if (header && header->kind == GCNodeKind::Accs) {
        if (const auto *key = graph->tryNodeAccsKey(node)) {
            j["accsKey"] = *key;
        }
    }
    if (header && header->kind == GCNodeKind::Func) {
        if (auto callee = graph->funcTarget(node)) {
            j["funcGraphId"] = callee->stableId();
            j["funcGraph"]   = callee->name();
        }
    }
    if (header && header->kind == GCNodeKind::Oper) {
        j["operName"] = graph->operTarget(node)->name();
    }
    if (auto *sourceContext = sourceContextForGraph(graph)) {
        auto origin = sourceContext->resolveGirNodeOrigin(
            draftDebugKey(nodeId),
            graph->nodeDebugEntityId(node));
        j["originId"] = origin;
    }
    return j;
}

json expandedGraphToJson(const DraftGraphBuilder *graph) {
    json j            = graphSummary(graph);
    j["children"]     = json::array();
    j["dependencies"] = json::array();
    j["nodes"]        = json::array();
    j["edges"]        = json::array();

    for (const auto &sub : graph->subGraphs()) {
        j["children"].push_back(graphSummary(sub.get()));
    }
    for (const auto &dep : graph->dependencyGraphs()) {
        j["dependencies"].push_back(graphSummary(dep.get()));
    }

    for (draft_node_ref_t nodeId = 0; nodeId < graph->draft().nodeSlotCount(); ++nodeId) {
        if (!graph->draft().alive(nodeId)) {
            continue;
        }
        j["nodes"].push_back(nodeToJson(graph, nodeId));

        for (size_t i = 0; i < graph->draft().normInputsOf(nodeId).size(); ++i) {
            j["edges"].push_back({
                {"sourceId", graph->draft().normInputsOf(nodeId)[i]},
                {"targetId", nodeId},
                {"linkType", "Norm"},
                {"targetPortIndex", i},
            });
        }
        for (size_t i = 0; i < graph->draft().withInputsOf(nodeId).size(); ++i) {
            j["edges"].push_back({
                {"sourceId", graph->draft().withInputsOf(nodeId)[i]},
                {"targetId", nodeId},
                {"linkType", "With"},
                {"targetPortIndex", i},
            });
        }
        for (size_t i = 0; i < graph->draft().ctrlInputsOf(nodeId).size(); ++i) {
            j["edges"].push_back({
                {"sourceId", graph->draft().ctrlInputsOf(nodeId)[i]},
                {"targetId", nodeId},
                {"linkType", "Ctrl"},
                {"targetPortIndex", i},
            });
        }
    }

    return j;
}

const DraftGraphBuilder *findGraphById(const DraftGraphBuilder *root, const std::string &graphId) {
    if (!root || graphId.empty()) {
        return nullptr;
    }
    if (root->stableId() == graphId) {
        return root;
    }
    std::queue<const DraftGraphBuilder *> q;
    std::unordered_set<const DraftGraphBuilder *> seen;
    q.push(root);
    seen.insert(root);
    while (!q.empty()) {
        const DraftGraphBuilder *graph = q.front();
        q.pop();
        for (const auto &sub : graph->subGraphs()) {
            if (sub && seen.insert(sub.get()).second) {
                if (sub->stableId() == graphId) {
                    return sub.get();
                }
                q.push(sub.get());
            }
        }
        for (const auto &dep : graph->dependencyGraphs()) {
            if (dep && seen.insert(dep.get()).second) {
                if (dep->stableId() == graphId) {
                    return dep.get();
                }
                q.push(dep.get());
            }
        }
    }
    return nullptr;
}

} // namespace

std::string getDebugNodeIdFromCompileNode(const void *opaqueNode) {
    const auto *node = static_cast<const DraftNode *>(opaqueNode);
    if (!node) {
        return {};
    }
    auto *graph = DraftGraphBuilder::fromDraft(node->header.owner);
    return graph ? graph->nodeDebugEntityId(node) : std::string{};
}

std::string getDebugGraphIdFromCompileNode(const void *opaqueNode) {
    const auto *node = static_cast<const DraftNode *>(opaqueNode);
    if (!node) {
        return {};
    }
    auto *graph = DraftGraphBuilder::fromDraft(node->header.owner);
    return graph ? graph->stableId() : std::string{};
}

std::pair<std::string, std::string>
getGirJsonFromCompileGraph(const void *opaqueRootGraph, const std::string &graphId) {
    const auto *root = static_cast<const DraftGraphBuilder *>(opaqueRootGraph);
    if (!root) {
        return {"", "no graph"};
    }

    if (graphId.empty()) {
        json out;
        out["graph"]                 = graphSummary(root);
        out["graph"]["children"]     = json::array();
        out["graph"]["dependencies"] = json::array();
        for (const auto &sub : root->subGraphs()) {
            out["graph"]["children"].push_back(graphSummary(sub.get()));
        }
        for (const auto &dep : root->dependencyGraphs()) {
            out["graph"]["dependencies"].push_back(graphSummary(dep.get()));
        }
        return {out.dump(), ""};
    }

    const DraftGraphBuilder *target = findGraphById(root, graphId);
    if (!target) {
        return {"", "graph not found"};
    }

    json out;
    out["graph"] = expandedGraphToJson(target);
    return {out.dump(), ""};
}

} // namespace debugger
