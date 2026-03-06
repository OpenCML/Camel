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
 * Updated: Mar. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/compile/gir/nodes.h"
#include "camel/utils/log.h"
#include <functional>

namespace GraphIR {

// =============================================================================
// Node 索引与连通性
// =============================================================================

data_idx_t Node::index() const {
    ASSERT(nodeType_ != NodeType::SYNC, "SYNC node has no data index.");
    ASSERT(nodeType_ != NodeType::DREF, "DREF node has no data index.");
    if (nodeType_ == NodeType::NREF) {
        return normInputs_.front()->index();
    }
    return dataIndex_;
}

bool Node::hasDeepLinkedTo(Node *node, size_t maxJumps) const {
    if (maxJumps == 0) {
        return false;
    }

    std::unordered_set<Node *> visited;
    std::function<bool(Node *, size_t)> dfs;

    dfs = [&](Node *current, size_t jumpsLeft) -> bool {
        ASSERT(current, "Current node is null in DFS.");
        if (jumpsLeft == 0) {
            EXEC_WHEN_DEBUG(
                GetDefaultLogger().in("GIR").warn(
                    "Deep link check reached max jumps at node: {}.",
                    node->toString()));
            return false;
        }

        visited.insert(current);

        for (auto *out : current->withOutputs_) {
            if (out == node)
                return true;
            if (visited.find(out) == visited.end()) {
                if (dfs(out, jumpsLeft - 1))
                    return true;
            }
        }
        for (auto *out : current->normOutputs_) {
            if (out == node)
                return true;
            if (visited.find(out) == visited.end()) {
                if (dfs(out, jumpsLeft - 1))
                    return true;
            }
        }
        for (auto *out : current->ctrlOutputs_) {
            if (out == node)
                return true;
            if (visited.find(out) == visited.end()) {
                if (dfs(out, jumpsLeft - 1))
                    return true;
            }
        }
        return false;
    };

    for (auto *out : withOutputs_) {
        if (dfs(out, maxJumps - 1))
            return true;
    }
    for (auto *out : normOutputs_) {
        if (dfs(out, maxJumps - 1))
            return true;
    }
    for (auto *out : ctrlOutputs_) {
        if (dfs(out, maxJumps - 1))
            return true;
    }
    return false;
}

bool Node::hasLinkedTo(Node *node) const {
    for (auto *out : withOutputs_) {
        if (out == node)
            return true;
    }
    for (auto *out : normOutputs_) {
        if (out == node)
            return true;
    }
    for (auto *out : ctrlOutputs_) {
        if (out == node)
            return true;
    }
    return false;
}

// =============================================================================
// Node 连边与替换
// =============================================================================

void Node::link(LinkType type, Node *from, Node *to) {
    ASSERT(
        from->nodeType_ != NodeType::DREF,
        "DREF nodes cannot be linked as input to other nodes.");
    ASSERT(from && to, "Cannot link null nodes.");
    ASSERT(from != to, "Cannot link a node to itself.");
    ASSERT(
        &from->graph() == &to->graph(),
        std::format(
            "Cannot link nodes from different graphs: {} -{}-> {}.",
            from->toString(),
            (type == LinkType::With ? "W" : (type == LinkType::Norm ? "N" : "C")),
            to->toString()));

    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("GIR").debug(
            "Linking nodes: {} -{}-> {}",
            from->toString(),
            (type == LinkType::With ? "W" : (type == LinkType::Norm ? "N" : "C")),
            to->toString()));

    switch (type) {
    case LinkType::With:
        from->withOutputs_.push_back(to);
        to->withInputs_.push_back(from);
        break;
    case LinkType::Norm:
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
}

bool Node::unlink(Node *from, Node *to) {
    ASSERT(from && to, "Cannot unlink null nodes.");
    ASSERT(from != to, "Cannot unlink a node from itself.");
    ASSERT(
        &from->graph() == &to->graph(),
        std::format(
            "Cannot unlink nodes from different graphs: {} -X- {}.",
            from->toString(),
            to->toString()));

    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("GIR").debug(
            "Unlinking nodes: {} -X- {}",
            from->toString(),
            to->toString()));

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

bool Node::replace(Node *oldNode, Node *newNode) {
    ASSERT(oldNode && newNode, "Cannot replace null nodes.");
    ASSERT(oldNode != newNode, "Cannot replace a node with itself.");
    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("GIR").debug(
            "Replacing node: {} -> {}",
            oldNode->toString(),
            newNode->toString()));

    for (const auto &in : oldNode->withInputs_) {
        Node::link(LinkType::With, in, newNode);
    }
    for (const auto &in : oldNode->normInputs_) {
        Node::link(LinkType::Norm, in, newNode);
    }
    for (const auto &in : oldNode->ctrlInputs_) {
        Node::link(LinkType::Ctrl, in, newNode);
    }
    for (const auto &out : oldNode->withOutputs_) {
        Node::link(LinkType::With, newNode, out);
    }
    for (const auto &out : oldNode->normOutputs_) {
        Node::link(LinkType::Norm, newNode, out);
    }
    for (const auto &out : oldNode->ctrlOutputs_) {
        Node::link(LinkType::Ctrl, newNode, out);
    }
    return oldNode->detach();
}

bool Node::detach() {
    auto tempWithInputs = withInputs_;
    for (auto *input : tempWithInputs) {
        if (!unlink(input, this))
            return false;
    }
    auto tempNormInputs = normInputs_;
    for (auto *input : tempNormInputs) {
        if (!unlink(input, this))
            return false;
    }
    auto tempCtrlInputs = ctrlInputs_;
    for (auto *input : tempCtrlInputs) {
        if (!unlink(input, this))
            return false;
    }
    auto tempWithOutputs = withOutputs_;
    for (auto *output : tempWithOutputs) {
        if (!unlink(this, output))
            return false;
    }
    auto tempNormOutputs = normOutputs_;
    for (auto *output : tempNormOutputs) {
        if (!unlink(this, output))
            return false;
    }
    auto tempCtrlOutputs = ctrlOutputs_;
    for (auto *output : tempCtrlOutputs) {
        if (!unlink(this, output))
            return false;
    }
    return true;
}

} // namespace GraphIR
