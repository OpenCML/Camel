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
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "gir.h"
#include "utils/scope.h"

using namespace std;

namespace GraphIR {

std::string to_string(NodeType type) {
    switch (type) {
    case NodeType::Select:
        return "Select";
    case NodeType::Access:
        return "Access";
    case NodeType::Struct:
        return "Struct";
    case NodeType::Source:
        return "Source";
    case NodeType::Return:
        return "Return";
    case NodeType::Operator:
        return "Operator";
    case NodeType::Function:
        return "Function";
    }
    ASSERT(false, "Unknown NodeType");
    return "Unknown";
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
            l.in("GIR").warn("Deep link check reached max jumps at node: {}.", node->toString());
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
    switch (type) {
    case LinkType::With:
        from->dataOutputs().push_back(to);
        to->withInputs().push_back(from);
        break;
    case LinkType::Norm:
        from->dataOutputs().push_back(to);
        to->normInputs().push_back(from);
        break;
    case LinkType::Ctrl:
        from->ctrlOutputs().push_back(to);
        to->ctrlInputs().push_back(from);
        break;
    }
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

node_ptr_t Graph::addPort(bool isWithArg) {
    DataIndex index = arena_->addConstant(nullptr, false);
    node_ptr_t portNode = SourceNode::create(shared_from_this(), index);
    ports_.push_back({portNode, isWithArg});
    return portNode;
}

void Graph::setOutput(const node_ptr_t &node) {
    ASSERT(output_ == nullptr, "Output node has already been set.");
    output_ = ReturnNode::create(shared_from_this(), node->index());
    Node::link(LinkType::Norm, node, output_);
}

/*
LiteralNode
*/

/*
StructNode
*/

/*
OperatorNode
*/

/*
SelectNode
*/

} // namespace GraphIR
