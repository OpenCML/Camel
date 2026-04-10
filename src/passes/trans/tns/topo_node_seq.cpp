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
 * Created: Sep. 05, 2025
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "topo_node_seq.h"

#include "camel/execute/graph_runtime_support.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/reachable.h"
#include "camel/utils/type.h"

#include <format>
#include <iomanip>
#include <sstream>

using namespace std;
using namespace camel::core::context;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCAccsBody;
using camel::runtime::GCGraph;
using camel::runtime::GCNode;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;

namespace {

const GCNode *requireNode(GCGraph *graph, gc_node_ref_t nodeRef) {
    const auto *node = graph ? graph->node(nodeRef) : nullptr;
    ASSERT(node != nullptr, "Topo dump resolved to a null runtime node.");
    return node;
}

} // namespace

TopoNodeSeqDumpPass::TopoNodeSeqDumpPass(const context_ptr_t &ctx) : GraphTranslatePass(ctx) {}

string TopoNodeSeqDumpPass::pointerToIdent(const void *ptr, const char *prefix) {
    return getPtrRepr(prefix, reinterpret_cast<uintptr_t>(ptr), showRawPtr);
}

string TopoNodeSeqDumpPass::getPtrRepr(const string &prefix, uintptr_t ptrVal, bool showRawPtr) {
    string prefixStr(prefix);
    if (!showRawPtr) {
        auto &mapForPrefix = ptrsMap_[prefixStr];
        if (mapForPrefix.find(ptrVal) == mapForPrefix.end()) {
            mapForPrefix[ptrVal] = ptrCnt_[prefixStr]++;
        }
        ptrVal = mapForPrefix[ptrVal];
    }

    int hexDigits  = 1;
    uintptr_t temp = ptrVal;
    while (temp >>= 4) {
        ++hexDigits;
    }

    int width = ((hexDigits + 1) / 2) * 2;

    stringstream ss;
    ss << prefix << hex << uppercase << setw(width) << setfill('0') << ptrVal << dec << nouppercase;
    return ss.str();
}

GCGraph *TopoNodeSeqDumpPass::apply(GCGraph *graph, std::ostream &os) {
    ASSERT(graph != nullptr, "Topo dump requires a non-null runtime root graph.");

    auto sortedGraphs = camel::runtime::collectReachableGraphs(graph);
    ostringstream oss;

    for (auto *runtimeGraph : sortedGraphs) {
        auto sortedNodes     = camel::execute::buildReachableExecutionTopoIndices(runtimeGraph);
        auto formatNodeIdent = [&](gc_node_ref_t nodeRef) {
            return pointerToIdent(requireNode(runtimeGraph, nodeRef));
        };
        auto formatInputs = [&](std::span<const gc_node_ref_t> inputs) {
            string text;
            for (gc_node_ref_t inputRef : inputs) {
                if (!text.empty()) {
                    text += ", ";
                }
                text += formatNodeIdent(inputRef);
            }
            return text;
        };
        auto describeRuntimeNode = [&](gc_node_ref_t nodeRef, const GCNode &node) {
            switch (node.kind) {
            case GCNodeKind::Func: {
                auto *callee          = runtimeGraph->directCalleeGraphOf(nodeRef);
                string name           = callee ? callee->name() : "<null>";
                string res            = format("CALL: {}", name);
                const auto normInputs = runtimeGraph->normInputsOf(nodeRef);
                const auto withInputs = runtimeGraph->withInputsOf(nodeRef);
                if (!normInputs.empty()) {
                    res += format(", nargs=[{}]", formatInputs(normInputs));
                }
                if (!withInputs.empty()) {
                    res += format(", wargs=[{}]", formatInputs(withInputs));
                }
                return res;
            }
            case GCNodeKind::Call: {
                string res            = "CALL: <indirect>";
                const auto normInputs = runtimeGraph->normInputsOf(nodeRef);
                const auto withInputs = runtimeGraph->withInputsOf(nodeRef);
                if (!normInputs.empty()) {
                    res += format(", nargs=[{}]", formatInputs(normInputs));
                }
                if (!withInputs.empty()) {
                    res += format(", wargs=[{}]", formatInputs(withInputs));
                }
                return res;
            }
            case GCNodeKind::Oper: {
                const auto *body      = runtimeGraph->nodeBodyAs<GCOperBody>(nodeRef);
                string res            = format("CALL: <{}>", std::string(body->uri()));
                const auto normInputs = runtimeGraph->normInputsOf(nodeRef);
                const auto withInputs = runtimeGraph->withInputsOf(nodeRef);
                if (!normInputs.empty()) {
                    res += format(", nargs=[{}]", formatInputs(normInputs));
                }
                if (!withInputs.empty()) {
                    res += format(", wargs=[{}]", formatInputs(withInputs));
                }
                return res;
            }
            case GCNodeKind::Brch: {
                const auto normInputs = runtimeGraph->normInputsOf(nodeRef);
                const auto arms       = runtimeGraph->ctrlOutputsOf(nodeRef);
                string res            = format(
                    "BRCH: {}",
                    normInputs.empty() ? "<missing-cond>" : formatNodeIdent(normInputs.front()));
                if (!arms.empty()) {
                    res += " ? ";
                    for (size_t i = 0; i < arms.size(); ++i) {
                        if (i != 0) {
                            res += ": ";
                        }
                        res += formatNodeIdent(arms[i]);
                    }
                }
                return res;
            }
            case GCNodeKind::Join: {
                const auto normInputs = runtimeGraph->normInputsOf(nodeRef);
                const auto withInputs = runtimeGraph->withInputsOf(nodeRef);
                string res            = "JOIN:";
                if (!normInputs.empty()) {
                    res += format(" brch={}", formatNodeIdent(normInputs.front()));
                }
                if (!withInputs.empty()) {
                    res += format(" arms=[{}]", formatInputs(withInputs));
                }
                return res;
            }
            case GCNodeKind::Accs: {
                const auto *body = runtimeGraph->nodeBodyAs<GCAccsBody>(nodeRef);
                if (body->accsKind == camel::runtime::GCAccsKind::StructKey) {
                    return format("ACCS: .{}", std::string(body->key()));
                }
                return format("ACCS: .{}", body->value);
            }
            case GCNodeKind::Fill: {
                string res            = "FILL:";
                const auto normInputs = runtimeGraph->normInputsOf(nodeRef);
                const auto withInputs = runtimeGraph->withInputsOf(nodeRef);
                if (!normInputs.empty()) {
                    res += format(" src={}", formatNodeIdent(normInputs.front()));
                }
                if (!withInputs.empty()) {
                    res += format(" with=[{}]", formatInputs(withInputs));
                }
                return res;
            }
            default:
                return format("NODE: kind={} slot={}", static_cast<int>(node.kind), node.dataIndex);
            }
        };

        oss << "FUNC: " << runtimeGraph->name();
        for (gc_node_ref_t portRef : runtimeGraph->normPorts()) {
            oss << ", " << pointerToIdent(requireNode(runtimeGraph, portRef));
        }
        for (gc_node_ref_t portRef : runtimeGraph->withPorts()) {
            oss << ", " << pointerToIdent(requireNode(runtimeGraph, portRef));
        }
        for (gc_node_ref_t closureRef : runtimeGraph->closureNodes()) {
            oss << ", " << pointerToIdent(requireNode(runtimeGraph, closureRef));
        }
        oss << "\n";

        for (gc_node_ref_t nodeRef : sortedNodes) {
            const auto *node = requireNode(runtimeGraph, nodeRef);
            oss << "    [" << pointerToIdent(node) << "] " << describeRuntimeNode(nodeRef, *node)
                << "\n";
        }

        const auto *returnNode = runtimeGraph->returnNode();
        if (returnNode != nullptr) {
            oss << "RETN: " << pointerToIdent(returnNode) << "\n\n";
        } else if (const auto *exitNode = runtimeGraph->exitNode(); exitNode != nullptr) {
            oss << "RETN: " << pointerToIdent(exitNode) << "\n\n";
        } else {
            oss << "RETN: <none>\n\n";
        }
    }

    oss << format("CALL: {}", graph->name()) << "\n";
    os << oss.str();
    return nullptr;
}
