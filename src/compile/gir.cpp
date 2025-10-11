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
 * Updated: Oct. 11, 2025
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
    case NodeType::DREF:
        return "DREF";
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
    output_ = ExitNode::create(*this, node->dataType(), node->index());
    Node::link(LinkType::Norm, node, output_);
}

graph_ptr_t Graph::clone() const {
    graph_ptr_t newGraph = Graph::create(outer_.lock(), name_);
    newGraph->looped_ = looped_;

    newGraph->funcType_ = funcType_;
    newGraph->staticDataArr_ = staticDataArr_;
    newGraph->runtimeDataSize_ = runtimeDataSize_;

    for (const auto &[name, subGraphs] : subGraphs_) {
        for (const auto &subGraph : subGraphs) {
            newGraph->addSubGraph(subGraph->clone());
        }
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
        for (const auto &out : current->withOutputs_) {
            if (out == node) {
                return true;
            }
            if (visited.find(out.get()) == visited.end()) {
                if (dfs(out, jumpsLeft - 1)) {
                    return true;
                }
            }
        }

        for (const auto &out : current->normOutputs_) {
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
    for (const auto &out : withOutputs_) {
        if (dfs(out, maxJumps - 1)) {
            return true;
        }
    }
    for (const auto &out : normOutputs_) {
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
    for (const auto &out : withOutputs_) {
        if (out == node) {
            return true;
        }
    }
    for (const auto &out : normOutputs_) {
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

/**
 * 保证两个节点之间不会有多条边
 * 但不保证不会成环，需要在调用时做成环检测
 * 涉及到外部节点时，自动设置捕获
 */
void Node::link(LinkType type, const node_ptr_t &from, const node_ptr_t &to) {
    ASSERT(
        from->nodeType_ != NodeType::DREF,
        "DREF nodes cannot be linked as input to other nodes.");
    ASSERT(from && to, "Cannot link null nodes.");
    ASSERT(from != to, "Cannot link a node to itself.");
    EXEC_WHEN_DEBUG(l.in("GIR").debug(
        "Linking nodes: {} -{}-> {}",
        from->toString(),
        (type == LinkType::With ? "W" : (type == LinkType::Norm ? "N" : "C")),
        to->toString()));

    switch (type) {
    case LinkType::With:
        ASSERT(
            std::find(from->withOutputs_.begin(), from->withOutputs_.end(), to) ==
                from->withOutputs_.end(),
            "Nodes are already linked (with).");
        from->withOutputs_.push_back(to);
        to->withInputs_.push_back(from);
        break;
    case LinkType::Norm:
        ASSERT(
            std::find(from->normOutputs_.begin(), from->normOutputs_.end(), to) ==
                from->normOutputs_.end(),
            "Nodes are already linked (norm).");
        from->normOutputs_.push_back(to);
        to->normInputs_.push_back(from);
        break;
    case LinkType::Ctrl:
        ASSERT(
            std::find(from->ctrlOutputs_.begin(), from->ctrlOutputs_.end(), to) ==
                from->ctrlOutputs_.end(),
            "Nodes are already linked (ctrl).");
        from->ctrlOutputs_.push_back(to);
        to->ctrlInputs_.push_back(from);
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

/**
 * 解除两个节点之间的连接
 * 由于link保证了不会有多条边，因此unlink只需要解除一条边
 * 默认不允许跨图解除连接
 * 如果需要强制解除跨图连接，须设置force=true
 */
bool Node::unlink(const node_ptr_t &from, const node_ptr_t &to, bool force) {
    ASSERT(from && to, "Cannot unlink null nodes.");
    ASSERT(from != to, "Cannot unlink a node from itself.");
    EXEC_WHEN_DEBUG(
        l.in("GIR").debug("Unlinking nodes: {} -X- {}", from->toString(), to->toString()));

    if (&from->graph() != &to->graph()) {
        if (force) {
            auto &toGraphCapture = to->graph().capture_;
            if (toGraphCapture.find(from) != toGraphCapture.end()) {
                toGraphCapture.erase(from);
                bool fromStillExposed = false;
                for (const auto &out : from->outputs()) {
                    if (&out->graph() != &from->graph() && &out->graph() != &to->graph()) {
                        // 被引用的节点既不是from所在图的节点，也不是to所在图的节点
                        // 说明from仍然被其他图捕获，不能移除exposure
                        fromStillExposed = true;
                        break;
                    }
                }
                if (!fromStillExposed) {
                    // 没有任何其他图捕获from，可以将其从exposure中移除
                    from->graph().exposure_.erase(from);
                }
            }
        } else {
            ASSERT(
                false,
                std::format(
                    "Cannot unlink nodes from different graphs: {} -X- {}. "
                    "Use force=true to override.",
                    from->toString(),
                    to->toString()));
            return false;
        }
    }

    auto &toNormInputs = to->normInputs_;
    if (std::find(toNormInputs.begin(), toNormInputs.end(), from) != toNormInputs.end()) {
        toNormInputs.erase(
            std::remove(toNormInputs.begin(), toNormInputs.end(), from),
            toNormInputs.end());

        auto &fromNormOutputs = from->normOutputs_;
        fromNormOutputs.erase(
            std::remove(fromNormOutputs.begin(), fromNormOutputs.end(), to),
            fromNormOutputs.end());

        return true;
    }

    auto &toWithInputs = to->withInputs_;
    if (std::find(toWithInputs.begin(), toWithInputs.end(), from) != toWithInputs.end()) {
        toWithInputs.erase(
            std::remove(toWithInputs.begin(), toWithInputs.end(), from),
            toWithInputs.end());

        auto &fromWithOutputs = from->withOutputs_;
        fromWithOutputs.erase(
            std::remove(fromWithOutputs.begin(), fromWithOutputs.end(), to),
            fromWithOutputs.end());

        return true;
    }

    auto &toCtrlInputs = to->ctrlInputs_;
    if (std::find(toCtrlInputs.begin(), toCtrlInputs.end(), from) != toCtrlInputs.end()) {
        toCtrlInputs.erase(
            std::remove(toCtrlInputs.begin(), toCtrlInputs.end(), from),
            toCtrlInputs.end());

        auto &fromCtrlOutputs = from->ctrlOutputs_;
        fromCtrlOutputs.erase(
            std::remove(fromCtrlOutputs.begin(), fromCtrlOutputs.end(), to),
            fromCtrlOutputs.end());

        return true;
    }

    ASSERT(false, "Try to unlink nodes that are not linked.");
    return false;
}

bool Node::replace(const node_ptr_t &oldNode, const node_ptr_t &newNode, bool force) {
    ASSERT(oldNode && newNode, "Cannot replace null nodes.");
    ASSERT(oldNode != newNode, "Cannot replace a node with itself.");
    EXEC_WHEN_DEBUG(
        l.in("GIR").debug("Replacing node: {} -> {}", oldNode->toString(), newNode->toString()));

    {
        auto tempWithInputs = oldNode->withInputs_;
        for (const auto &in : tempWithInputs) {
            Node::link(LinkType::With, in, newNode);
        }

        auto tempNormInputs = oldNode->normInputs_;
        for (const auto &in : tempNormInputs) {
            Node::link(LinkType::Norm, in, newNode);
        }

        auto tempCtrlInputs = oldNode->ctrlInputs_;
        for (const auto &in : tempCtrlInputs) {
            Node::link(LinkType::Ctrl, in, newNode);
        }

        auto tempWithOutputs = oldNode->withOutputs_;
        for (const auto &out : tempWithOutputs) {
            Node::link(LinkType::With, newNode, out);
        }

        auto tempNormOutputs = oldNode->normOutputs_;
        for (const auto &out : tempNormOutputs) {
            Node::link(LinkType::Norm, newNode, out);
        }

        auto tempCtrlOutputs = oldNode->ctrlOutputs_;
        for (const auto &out : tempCtrlOutputs) {
            Node::link(LinkType::Ctrl, newNode, out);
        }
    }

    return oldNode->detach(force);
}

/**
 * 默认不允许解除被其他图捕获的暴露节点的连接
 * 如果需要强制解除被暴露的节点，须设置force=true
 */
bool Node::detach(bool force) {
    if (!force) {
        const auto &exposure = graph_.exposure();
        if (exposure.find(shared_from_this()) != exposure.end()) {
            ASSERT(
                false,
                std::format(
                    "Cannot detach an exposed node: {}. Use force=true to override.",
                    toString()));
            return false;
        }
    }

    node_ptr_t self = shared_from_this();

    {
        auto tempWithInputs = withInputs_;
        for (auto &input : tempWithInputs) {
            if (!unlink(input, self, force)) {
                return false;
            }
        }
        auto tempNormInputs = normInputs_;
        for (auto &input : tempNormInputs) {
            if (!unlink(input, self, force)) {
                return false;
            }
        }
        auto tempCtrlInputs = ctrlInputs_;
        for (auto &input : tempCtrlInputs) {
            if (!unlink(input, self, force)) {
                return false;
            }
        }

        auto tempWithOutputs = withOutputs_;
        for (auto &output : tempWithOutputs) {
            if (!unlink(self, output, force)) {
                return false;
            }
        }
        auto tempNormOutputs = normOutputs_;
        for (auto &output : tempNormOutputs) {
            if (!unlink(self, output, force)) {
                return false;
            }
        }
        auto tempCtrlOutputs = ctrlOutputs_;
        for (auto &output : tempCtrlOutputs) {
            if (!unlink(self, output, force)) {
                return false;
            }
        }
    }

    return true;
}

} // namespace GraphIR
