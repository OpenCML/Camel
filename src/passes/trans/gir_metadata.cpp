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
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Shared semantic metadata; formatting and layout belong to the individual exporters.
#include "camel/passes/trans/gir_metadata.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/func.h"
#include <format>
#include <sstream>
using namespace std;
namespace camel::passes::gir {
const camel::runtime::GCNode *
lookupRecord(camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t index) {
    return graph ? graph->node(index) : nullptr;
}

camel::core::type::Type *
nodeStaticSlotType(camel::runtime::GCGraph *graph, const camel::runtime::GCNode &record) {
    if (graph == nullptr || record.dataIndex >= 0) {
        return record.dataType();
    }
    const auto staticTypes = graph->staticDataType();
    const size_t staticIdx = static_cast<size_t>(-record.dataIndex);
    if (staticTypes != nullptr && staticIdx < staticTypes->size()) {
        return staticTypes->typeAt(staticIdx);
    }
    return record.dataType();
}

string formatStaticDataValue(camel::runtime::GCGraph *graph, const camel::runtime::GCNode &record) {
    if (graph == nullptr || record.dataIndex >= 0) {
        return {};
    }
    const auto staticSlots = graph->staticSlots();
    const size_t staticIdx = static_cast<size_t>(-record.dataIndex);
    if (staticIdx >= staticSlots.size()) {
        return {};
    }

    auto *slotType = nodeStaticSlotType(graph, record);
    if (slotType == nullptr) {
        return {};
    }

    std::ostringstream os;
    camel::core::rtdata::printSlot(os, staticSlots[staticIdx], slotType);
    return os.str();
}

PortInfo portInfo(camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef) {
    if (!graph)
        return {};
    const auto captured = graph->captureNames();
    const auto closures = graph->closureNodes();
    // Captures have their own name table, even when the function has no declared arguments.
    for (size_t i = 0; i < closures.size(); ++i) {
        if (closures[i] == nodeRef) {
            return {
                i < captured.size() ? string(camel::core::symbolText(captured[i])) : "",
                "capture",
                i};
        }
    }
    const auto withPorts      = graph->withPorts();
    const size_t declaredWith = closures.empty() && captured.size() <= withPorts.size()
                                    ? withPorts.size() - captured.size()
                                    : withPorts.size();
    auto argName              = [&](size_t index) -> string {
        const auto *type = graph->funcType();
        return type && index < type->argNamesCount() ? string(type->argNameAt(index)) : "";
    };
    for (size_t i = 0; i < withPorts.size(); ++i) {
        if (withPorts[i] != nodeRef)
            continue;
        if (i >= declaredWith) {
            return {string(camel::core::symbolText(captured[i - declaredWith])), "capture", i};
        }
        return {argName(i), "with", i};
    }
    const auto normPorts = graph->normPorts();
    for (size_t i = 0; i < normPorts.size(); ++i) {
        if (normPorts[i] == nodeRef)
            return {argName(declaredWith + i), "normal", i};
    }
    return {};
}

string portLabel(
    camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
    const camel::runtime::GCNode &) {
    const auto info = portInfo(graph, nodeRef);
    return info.name.empty() ? "PORT" : info.name;
}

string graphTooltip(camel::runtime::GCGraph *graph) {
    ASSERT(graph != nullptr, "Graph tooltip requires a runtime graph.");
    auto *funcType = graph->funcType();
    return funcType ? funcType->toString() : string("<unknown>");
}

string recordTooltip(camel::runtime::GCGraph *graph, const camel::runtime::GCNode &record) {
    ASSERT(graph != nullptr, "Node tooltip requires a runtime graph.");
    return std::format("{}::node#{}", graph->name(), record.dataIndex);
}

string recordLabel(
    const camel::runtime::Program &program, camel::runtime::GCGraph *graph,
    camel::runtime::gc_node_ref_t nodeRef, const camel::runtime::GCNode &record) {
    switch (record.kind) {
    case camel::runtime::GCNodeKind::Data: {
        const string value = formatStaticDataValue(graph, record);
        return value.empty() ? "DATA" : value;
    }
    case camel::runtime::GCNodeKind::Port:
        return portLabel(graph, nodeRef, record);
    case camel::runtime::GCNodeKind::Cast:
        return "CAST";
    case camel::runtime::GCNodeKind::Copy:
        return "COPY";
    case camel::runtime::GCNodeKind::Fill:
        return record.dataType() ? record.dataType()->toString() : "FILL";
    case camel::runtime::GCNodeKind::Accs: {
        const auto *body = graph->nodeBodyAs<camel::runtime::GCAccsBody>(nodeRef);
        ASSERT(
            graph->nodePayloadBytes(nodeRef).size() >= sizeof(camel::runtime::GCAccsBody),
            "GraphViz ACCS payload is smaller than its fixed header.");
        if (body->accsKind == camel::runtime::GCAccsKind::StructKey) {
            const std::string_view key = body->key();
            return "." + std::string(key.data(), key.size());
        }
        return std::format(".{}", body->value);
    }
    case camel::runtime::GCNodeKind::Brch:
        return "BRCH";
    case camel::runtime::GCNodeKind::Join:
        return "JOIN";
    case camel::runtime::GCNodeKind::Call:
        return "CALL";
    case camel::runtime::GCNodeKind::Func: {
        auto *callee = program.calleeOf(*graph, nodeRef);
        return callee ? callee->name() : "FUNC";
    }
    case camel::runtime::GCNodeKind::Oper:
        ASSERT(
            graph->nodePayloadBytes(nodeRef).size() >= sizeof(camel::runtime::GCOperBody),
            "GraphViz OPER payload is smaller than its fixed header.");
        return std::string(graph->nodeBodyAs<camel::runtime::GCOperBody>(nodeRef)->uri());
    case camel::runtime::GCNodeKind::Sync:
        return "SYNC";
    case camel::runtime::GCNodeKind::Gate:
        return "GATE";
    case camel::runtime::GCNodeKind::Loop: {
        // `LOOP while`: the condition and the body are regions of the graph (drawn as such). A
        // loop in graph form (outlineLoopRegions) names its graphs: `LOOP while cond=f.loop1.cond
        // body=f.loop1.body` (graph model 3.5, the LOOP card).
        const auto *cond = program.loopCondOf(*graph, nodeRef);
        const auto *body = program.loopBodyOf(*graph, nodeRef);
        std::string text =
            std::format("LOOP {}", camel::core::loopKindName(graph->loopKindOf(nodeRef)));
        if (cond) {
            text += " cond=" + cond->name();
        }
        return body ? text + " body=" + body->name() : text;
    }
    default:
        return "NODE";
    }
}

string recordType(const camel::runtime::GCNode &record) {
    switch (record.kind) {
    case camel::runtime::GCNodeKind::Data:
        return "DATA";
    case camel::runtime::GCNodeKind::Port:
        return "PORT";
    case camel::runtime::GCNodeKind::Cast:
        return "CAST";
    case camel::runtime::GCNodeKind::Copy:
        return "COPY";
    case camel::runtime::GCNodeKind::Fill:
        return "FILL";
    case camel::runtime::GCNodeKind::Accs:
        return "ACCS";
    case camel::runtime::GCNodeKind::Brch:
        return "BRCH";
    case camel::runtime::GCNodeKind::Join:
        return "JOIN";
    case camel::runtime::GCNodeKind::Call:
        return "CALL";
    case camel::runtime::GCNodeKind::Func:
        return "FUNC";
    case camel::runtime::GCNodeKind::Oper:
        return "OPER";
    case camel::runtime::GCNodeKind::Sync:
        return "SYNC";
    case camel::runtime::GCNodeKind::Gate:
        return "GATE";
    case camel::runtime::GCNodeKind::Loop:
        return "LOOP";
    default:
        return "NODE";
    }
}

} // namespace camel::passes::gir
