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
#include "camel/common/algo/topo.h"
#include "camel/utils/log.h"
#include "camel/utils/type.h"

#include <iomanip>
#include <sstream>

using namespace std;
using namespace GIR;
using namespace camel::core::context;

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

graph_ptr_t TopoNodeSeqDumpPass::apply(graph_ptr_t &graph, std::ostream &os) {
    auto optMainGraph = graph->getSubGraphsByName("main");
    ASSERT(optMainGraph.has_value(), "Main graph not found.");
    auto mainGraphSet = optMainGraph.value();
    ASSERT(!mainGraphSet.empty(), "Main graph set is empty.");
    ASSERT(mainGraphSet.size() == 1, "Multiple main graphs found.");
    auto mainGraph = *mainGraphSet.begin();

    // Collect all dependent subgraphs.
    auto sortedGraphs =
        findReachable(mainGraph, [](const graph_ptr_t &g) { return g->dependencies(); });

    ostringstream oss;

    // Print node sequences in order.
    for (const auto &g : sortedGraphs) {
        // Topologically sort the nodes.
        Node *exitNode   = g->exitNode();
        auto sortedNodes = findReachable(
            exitNode,
            [](Node *n) {
                vector<Node *> ins;
                ins.reserve(n->dataInputs().size() + n->ctrlInputs().size());
                for (const auto &in : n->ctrlInputs()) {
                    if (&in->graph() == &n->graph()) // Only consider nodes in the same graph.
                        ins.push_back(in);
                }
                for (const auto &in : n->dataInputs()) {
                    if (&in->graph() == &n->graph()) // Only consider nodes in the same graph.
                        ins.push_back(in);
                }
                return ins;
            },
            false);
        EXEC_WHEN_DEBUG({
            CAMEL_LOG_DEBUG_S("Topo", "Topologically sorted nodes for graph {}:", graph->name());
            for (const auto &n : sortedNodes) {
                CAMEL_LOG_DEBUG_S("Topo", "  {}", n->toString());
            }
            size_t totalNodeCnt = g->nodes().size() + g->ports().size() + g->closure().size();
            auto contains       = [](node_span_t nodes, Node *target) {
                return std::find(nodes.begin(), nodes.end(), target) != nodes.end();
            };
            const bool exitCounted =
                contains(g->nodes(), exitNode) || contains(g->normPorts(), exitNode) ||
                contains(g->withPorts(), exitNode) || contains(g->closure(), exitNode);
            const size_t expectedTopoCnt = totalNodeCnt + (exitCounted ? 0 : 1);
            if (sortedNodes.size() != expectedTopoCnt) {
                GIR::node_vec_t unreachableNodes;
                for (Node *n : g->nodes()) {
                    if (n != exitNode &&
                        std::find(sortedNodes.begin(), sortedNodes.end(), n) == sortedNodes.end()) {
                        unreachableNodes.push_back(n);
                    }
                }
                std::string nodeStrs;
                for (const auto &node : unreachableNodes) {
                    if (!nodeStrs.empty()) {
                        nodeStrs += ", ";
                    }
                    nodeStrs += node->toString();
                }
                CAMEL_LOG_WARN_S(
                    "Topo",
                    "Unreachable nodes in graph {} detected: {}",
                    g->name(),
                    nodeStrs);
            }
        });
        // Print the function signature, including parameter info.
        oss << "FUNC: " << g->name();
        for (const auto &portNode : g->ports()) {
            oss << ", " << pointerToIdent(portNode);
        }
        oss << "\n";
        // Print subgraph node information.
        for (const auto &n : sortedNodes) {
            string res;
            switch (n->type()) {
            case NodeType::FUNC: {
                auto *func  = tt::as_ptr<FuncNode>(n);
                string name = func->bodyGraph()->name();
                res         = format("CALL: {}", name);
                for (const auto &inputNode : n->dataInputs()) {
                    res += format(", {}", pointerToIdent(inputNode));
                }
                break;
            }
            case NodeType::OPER: {
                auto oper   = tt::as_ptr<OperNode>(n);
                string name = oper->oper()->name();
                res         = format("CALL: <{}>", name);
                for (const auto &inputNode : n->dataInputs()) {
                    res += format(", {}", pointerToIdent(inputNode));
                }
                break;
            }
            case NodeType::BRCH: {
                auto brchNode    = tt::as_ptr<BrchNode>(n);
                const auto &ins  = brchNode->dataInputs();
                const auto &outs = brchNode->ctrlOutputs();
                res              = format(
                    "BRCH: {}? {}: {}",
                    pointerToIdent(ins[0]),
                    pointerToIdent(outs[0]),
                    pointerToIdent(outs[1]));
                break;
            }
            case NodeType::JOIN: {
                auto joinNode   = tt::as_ptr<JoinNode>(n);
                const auto &ins = joinNode->withInputs();
                res = format("JOIN: {}, {}", pointerToIdent(ins[0]), pointerToIdent(ins[1]));
                break;
            }
            default:
                res = format("NODE: {}", n->toString());
            }
            oss << "    [" << pointerToIdent(n) << "] " << res << "\n";
        }
        // Print the return node.
        oss << "RETN: " << pointerToIdent(g->exitNode()) << "\n\n";
    }

    oss << format("CALL: {}", graph->name()) << "\n";

    os << oss.str();

    return GIR::Graph::null();
}
