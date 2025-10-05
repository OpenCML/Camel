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
 * Created: Aug. 17, 2024
 * Updated: Oct. 05, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "gir.h"
#include "utils/scope.h"

using namespace std;

namespace GraphIR {

std::string to_string(NodeType type) {
    switch (type) {
    case NodeType::DATA:
        return "DATA";
    case NodeType::PORT:
        return "PORT";
    case NodeType::CAST:
        return "CAST";
    case NodeType::COPY:
        return "COPY";
    case NodeType::FILL:
        return "FILL";
    case NodeType::ACCS:
        return "ACCS";
    case NodeType::BRCH:
        return "BRCH";
    case NodeType::JOIN:
        return "JOIN";
    case NodeType::CALL:
        return "CALL";
    case NodeType::BIND:
        return "BIND";
    case NodeType::FUNC:
        return "FUNC";
    case NodeType::OPER:
        return "OPER";
    case NodeType::EXIT:
        return "EXIT";
    }
    ASSERT(false, "Unknown NodeType");
    return "Unknown";
}

std::string to_string(LinkType type) {
    switch (type) {
    case LinkType::Norm:
        return "Norm";
    case LinkType::With:
        return "With";
    case LinkType::Ctrl:
        return "Ctrl";
    }
    ASSERT(false, "Unknown LinkType");
    return "Unknown";
}

/*
Graph
*/

void Graph::setFuncType(const func_type_ptr_t &type) {
    ASSERT(funcType_ == nullptr, "Function type has already been set.");
    funcType_ = type;
}

func_type_ptr_t Graph::funcType() const {
    ASSERT(funcType_ != nullptr, "Graph has not been set to a function type.");
    return funcType_;
}

void Graph::addNode(const node_ptr_t &node) { nodes_.push_back(node); }

void Graph::addPort(const node_ptr_t &node, bool isWith) {
    ports_.push_back(node);
    if (isWith) {
        withPortCnt_++;
    }
}

void Graph::addCapture(const node_ptr_t &node) {
    ASSERT(&node->graph() != this, "Cannot capture a node from the same graph.");
    capture_.insert(node);
    node->graph().exposure_.insert(node);
}

void Graph::setOutput(const node_ptr_t &node) {
    ASSERT(output_ == nullptr, "Output node has already been set.");
    output_ = ExitNode::create(*this, node->index());
    Node::link(LinkType::Norm, node, output_);
}

graph_ptr_t Graph::clone() const {
    graph_ptr_t newGraph = Graph::create(outer_.lock(), name_);
    newGraph->looped_ = looped_;

    newGraph->funcType_ = funcType_;
    newGraph->staticDataArr_ = staticDataArr_;
    newGraph->runtimeDataSize_ = runtimeDataSize_;

    for (const auto &subGraph : subGraphs_) {
        newGraph->addSubGraph(subGraph.second->clone());
    }
    for (const auto &dep : dependencies_) {
        newGraph->addDependency(dep);
    }

    std::unordered_map<Node *, node_ptr_t> nodeMap;

    for (const auto &port : ports_) {
        const auto &newPort = port->clone(*newGraph);
        nodeMap[port.get()] = newPort;
        newGraph->ports_.push_back(newPort);
    }
    for (const auto &node : nodes_) {
        const auto &newNode = node->clone(*newGraph);
        nodeMap[newNode.get()] = newNode;
    }

    // 重新建立节点之间的连接
    for (const auto &[oldNodePtr, newNodePtr] : nodeMap) {
        for (const auto &withInput : oldNodePtr->withInputs()) {
            if (nodeMap.find(withInput.get()) == nodeMap.end()) { // capture
                Node::link(LinkType::With, withInput, newNodePtr);
            } else {
                Node::link(LinkType::With, nodeMap[withInput.get()], newNodePtr);
            }
        }
        for (const auto &normInput : oldNodePtr->normInputs()) {
            if (nodeMap.find(normInput.get()) == nodeMap.end()) { // capture
                Node::link(LinkType::Norm, normInput, newNodePtr);
            } else {
                Node::link(LinkType::Norm, nodeMap[normInput.get()], newNodePtr);
            }
        }
        for (const auto &ctrlInput : oldNodePtr->ctrlInputs()) {
            if (nodeMap.find(ctrlInput.get()) == nodeMap.end()) { // capture
                Node::link(LinkType::Ctrl, ctrlInput, newNodePtr);
            } else {
                Node::link(LinkType::Ctrl, nodeMap[ctrlInput.get()], newNodePtr);
            }
        }
    }

    if (output_) {
        newGraph->setOutput(nodeMap[output_.get()]);
    }

    return newGraph;
}

/*
Node
*/

bool Node::hasDeepLinkedTo(const node_ptr_t &node, size_t maxJumps) const {
    if (maxJumps == 0) {
        return false;
    }

    // 使用 DFS 进行递归检查
    std::unordered_set<const void *> visited; // 用于避免重复访问
    std::function<bool(const node_ptr_t &, size_t)> dfs;

    dfs = [&](const node_ptr_t &current, size_t jumpsLeft) -> bool {
        ASSERT(current, "Current node is null in DFS.");
        if (jumpsLeft == 0) {
            EXEC_WHEN_DEBUG(l.in("GIR").warn(
                "Deep link check reached max jumps at node: {}.",
                node->toString()));
            return false;
        }

        // 标记当前节点为已访问
        visited.insert(current.get());

        // 检查当前节点的所有输出
        for (const auto &out : current->dataOutputs_) {
            if (out == node) {
                return true;
            }
            if (visited.find(out.get()) == visited.end()) {
                if (dfs(out, jumpsLeft - 1)) {
                    return true;
                }
            }
        }

        for (const auto &out : current->ctrlOutputs_) {
            if (out == node) {
                return true;
            }
            if (visited.find(out.get()) == visited.end()) {
                if (dfs(out, jumpsLeft - 1)) {
                    return true;
                }
            }
        }

        return false;
    };

    // 从当前节点出发进行递归搜索
    for (const auto &out : dataOutputs_) {
        if (dfs(out, maxJumps - 1)) {
            return true;
        }
    }
    for (const auto &out : ctrlOutputs_) {
        if (dfs(out, maxJumps - 1)) {
            return true;
        }
    }

    return false;
}

bool Node::hasLinkedTo(const node_ptr_t &node) const {
    for (const auto &out : dataOutputs_) {
        if (out == node) {
            return true;
        }
    }
    for (const auto &out : ctrlOutputs_) {
        if (out == node) {
            return true;
        }
    }
    return false;
}

void Node::link(LinkType type, const node_ptr_t &from, const node_ptr_t &to) {
    ASSERT(from && to, "Cannot link null nodes.");
    ASSERT(from != to, "Cannot link a node to itself.");
    EXEC_WHEN_DEBUG(l.in("GIR").debug(
        "Linking nodes: {} ->({}) {}",
        from->toString(),
        to_string(type),
        to->toString()));
    switch (type) {
    case LinkType::With:
        ASSERT(
            std::find(from->dataOutputs().begin(), from->dataOutputs().end(), to) ==
                from->dataOutputs().end(),
            "Nodes are already linked (with).");
        from->dataOutputs().push_back(to);
        to->withInputs().push_back(from);
        break;
    case LinkType::Norm:
        ASSERT(
            std::find(from->dataOutputs().begin(), from->dataOutputs().end(), to) ==
                from->dataOutputs().end(),
            "Nodes are already linked (norm).");
        from->dataOutputs().push_back(to);
        to->normInputs().push_back(from);
        break;
    case LinkType::Ctrl:
        ASSERT(
            std::find(from->ctrlOutputs().begin(), from->ctrlOutputs().end(), to) ==
                from->ctrlOutputs().end(),
            "Nodes are already linked (ctrl).");
        from->ctrlOutputs().push_back(to);
        to->ctrlInputs().push_back(from);
        break;
    }
    // setting capture
    if (&from->graph() != &to->graph()) {
        Graph *curr = &to->graph();
        while (curr != nullptr && &from->graph() != curr) {
            // the referenced node is from an outer scope, need to mark it as captured
            curr->addCapture(from);
            curr = curr->outer().get();
        }
    }
}

} // namespace GraphIR
