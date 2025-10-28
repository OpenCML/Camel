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
 * Updated: Oct. 28, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "gir.h"
#include "error/diagnostics/diagnostics.h"
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
    case NodeType::SYNC:
        return "SYNC";
    case NodeType::NREF:
        return "NREF";
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

graph_ptr_t
Graph::create(const func_type_ptr_t &funcType, const graph_ptr_t &graph, const std::string &name) {
    ASSERT(funcType->hasCompileInfo(), "Trying to create a Graph with incomplete FunctionType.");
    static int anonymousIdx = 0;
    std::string graphName = name.empty() ? std::format("__{}__", anonymousIdx++) : name;
    const auto newGraph = std::make_shared<Graph>(funcType, graph, graphName);
    if (graph) {
        graph->addSubGraph(newGraph);
    }
    for (const auto &[name, type, isVar] : funcType->normArgsInfo()) {
        node_ptr_t portNode = PortNode::create(*newGraph, type, name, isVar);
        newGraph->addPort(portNode, false);
    }
    for (const auto &[name, type, isVar] : funcType->withArgsInfo()) {
        node_ptr_t portNode = PortNode::create(*newGraph, type, name, isVar);
        newGraph->addPort(portNode, true);
    }
    return newGraph;
}

func_type_ptr_t Graph::funcType() const { return funcType_; }

void Graph::addNode(const node_ptr_t &node) {
    nodes_.push_back(node);
    dirty_ = true;
}

void Graph::delNode(const node_ptr_t &node) {
    nodes_.erase(std::remove(nodes_.begin(), nodes_.end(), node), nodes_.end());
    dirty_ = true;
}

void Graph::addPort(const node_ptr_t &node, bool isWith) {
    if (isWith) {
        ASSERT(
            std::find(withPorts_.begin(), withPorts_.end(), node) == withPorts_.end(),
            "With port node already exists in the graph.");
        withPorts_.push_back(node);
    } else {
        ASSERT(
            std::find(normPorts_.begin(), normPorts_.end(), node) == normPorts_.end(),
            "Norm port node already exists in the graph.");
        normPorts_.push_back(node);
    }
    dirty_ = true;
}

void Graph::addClosure(const node_ptr_t &node) {
    ASSERT(
        std::find(closure_.begin(), closure_.end(), node) == closure_.end(),
        "Closure node already exists in the graph.");
    const auto &portNode = tt::as_shared<PortNode>(node);
    closure_.push_back(node);
    funcType_->addClosureRef(portNode->name());
    dirty_ = true;
}

void Graph::parametrizeClosure() {
    withPorts_.insert(withPorts_.begin(), closure_.begin(), closure_.end());
    closure_.clear();
    parameterized_ = true;
    rearrange();
}

void Graph::setOutput(const node_ptr_t &node) {
    ASSERT(exitNode_ == nullptr, std::format("Graph {} already has an output node.", name_));

    type_ptr_t actualExitType = node->dataType();
    if (funcType_->hasExitType()) {
        type_ptr_t declaredExitType = funcType_->exitType();
        if (!actualExitType->assignable(declaredExitType)) {
            throw DiagnosticBuilder::of(SemanticDiag::ReturnTypeMismatch)
                .commit(
                    actualExitType->toString(),
                    declaredExitType->toString(),
                    name_ + ": " + funcType_->toString());
        }
    } else {
        // If the function has no declared return type, set it to the actual return type
        funcType_->setExitType(actualExitType);
    }

    exitNode_ = ExitNode::create(*this, node->dataType(), node->index());
    Node::link(LinkType::Norm, node, exitNode_);
}

std::string Graph::location() const {
    if (outer_.expired()) {
        return name_.empty() ? "<anonymous>" : name_;
    }
    return outer_.lock()->location() + "::" + (name_.empty() ? "<anonymous>" : name_);
}

graph_ptr_t Graph::outer() const {
    if (outer_.expired()) {
        return nullptr;
    }
    return outer_.lock();
}

std::string Graph::toString() const {
    return std::format(
        "Graph({}, nodes: {}, subgraphs: {}, deps: {}, outs: {})",
        name_.empty() ? "<anonymous>" : name_,
        nodes_.size(),
        subGraphs_.size(),
        dependencies_.size(),
        dependents_.size());
}

data_idx_t Graph::addStaticData(const data_ptr_t &data) {
    staticDataArr_.push_back(data);
    if (staticDataArr_.size() > static_cast<size_t>(std::numeric_limits<arr_size_t>::max())) {
        throw std::overflow_error("staticDataArr_ exceeds arr_size_t max value");
    }
    return -static_cast<data_idx_t>(staticDataArr_.size() - 1);
}

data_idx_t Graph::addRuntimeData() {
    if (runtimeDataSize_ > static_cast<size_t>(std::numeric_limits<arr_size_t>::max())) {
        throw std::overflow_error("runtimeDataSize_ exceeds arr_size_t max value");
    }
    return static_cast<data_idx_t>(runtimeDataSize_++);
}

void Graph::setStaticData(data_idx_t index, const data_ptr_t &data) {
    ASSERT(index < 0, "Static data index must be negative.");
    size_t idx = static_cast<size_t>(-index);
    ASSERT(
        idx < staticDataArr_.size(),
        std::format(
            "Static data index out of range when setting data of graph ({}) at index {}. "
            "(total size: {})",
            name_,
            index,
            staticDataArr_.size()));
    staticDataArr_[idx] = data;
}

data_ptr_t Graph::getStaticData(data_idx_t index) const {
    ASSERT(index < 0, "Static data index must be negative.");
    size_t idx = static_cast<size_t>(-index);
    ASSERT(
        idx < staticDataArr_.size(),
        std::format(
            "Static data index out of range when getting data of graph ({}) at index {}. "
            "(total size: {})",
            name_,
            index,
            staticDataArr_.size()));
    return staticDataArr_[idx];
}

std::optional<std::unordered_set<graph_ptr_t>> Graph::getSubGraphsByName(const std::string &name) {
    if (subGraphs_.find(name) != subGraphs_.end()) {
        return subGraphs_[name];
    }
    return std::nullopt;
}

void Graph::addSubGraph(const graph_ptr_t &graph) {
    ASSERT(graph.get() != this, "Cannot add itself as a subgraph.");
    ASSERT(!graph->name().empty(), "Cannot add an anonymous graph as a subgraph.");
    if (subGraphs_.find(graph->name()) == subGraphs_.end()) {
        subGraphs_[graph->name()] = std::unordered_set<graph_ptr_t>({graph});
    } else {
        auto &existing = subGraphs_[graph->name()];
        ASSERT(
            existing.find(graph) == existing.end(),
            std::format("Subgraph with name '{}' already exists.", graph->mangledName()));
        existing.insert(graph);
        l.in("GIR").debug("Added subgraph '{}' to graph '{}'.", graph->mangledName(), name_);
    }
    graph->outer_ = shared_from_this();
}

void Graph::delSubGraph(const graph_ptr_t &graph) {
    ASSERT(graph.get() != this, "Cannot remove itself as a subgraph.");
    ASSERT(!graph->name().empty(), "Cannot remove an anonymous graph as a subgraph.");
    if (subGraphs_.find(graph->name()) != subGraphs_.end()) {
        auto &existing = subGraphs_[graph->name()];
        existing.erase(graph);
        l.in("GIR").debug("Removed subgraph '{}' from graph '{}'.", graph->mangledName(), name_);
        if (existing.empty()) {
            subGraphs_.erase(graph->name());
        }
        graph->outer_.reset();
    }
}

void Graph::addDependency(const graph_ptr_t &graph) {
    if (graph.get() == this) {
        this->looped_ = true;
        // Here we do not add itself to dependencies_ to avoid self-references
        // but only mark it as a looped graph
        return;
    }
    dependencies_.insert(graph);
    graph->dependents_.insert(shared_from_this());
    l.in("GIR").debug("Added dependency: Graph '{}' depends on graph '{}'.", name_, graph->name());
}

void Graph::delDependency(const graph_ptr_t &graph) {
    dependencies_.erase(graph);
    graph->dependents_.erase(shared_from_this());
    l.in("GIR").debug(
        "Removed dependency: Graph '{}' no longer depends on graph '{}'.",
        name_,
        graph->name());
}

graph_ptr_t Graph::clone() const {
    graph_ptr_t newGraph =
        Graph::create(tt::as_shared<FunctionType>(funcType_->clone()), outer_.lock(), name_);
    newGraph->looped_ = looped_;
    newGraph->parameterized_ = parameterized_;

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

    for (const auto &port : withPorts_) {
        const auto &newPort = port->clone(*newGraph);
        nodeMap[port.get()] = newPort;
        newGraph->withPorts_.push_back(newPort);
    }
    for (const auto &port : normPorts_) {
        const auto &newPort = port->clone(*newGraph);
        nodeMap[port.get()] = newPort;
        newGraph->normPorts_.push_back(newPort);
    }
    for (const auto &closureNode : closure_) {
        const auto &newClosureNode = closureNode->clone(*newGraph);
        nodeMap[closureNode.get()] = newClosureNode;
        newGraph->closure_.push_back(newClosureNode);
    }
    for (const auto &node : nodes_) {
        const auto &newNode = node->clone(*newGraph);
        nodeMap[newNode.get()] = newNode;
    }

    // Re-establish connections between nodes
    for (const auto &[oldNodePtr, newNodePtr] : nodeMap) {
        for (const auto &withInput : oldNodePtr->withInputs()) {
            Node::link(LinkType::With, nodeMap[withInput.get()], newNodePtr);
        }
        for (const auto &normInput : oldNodePtr->normInputs()) {
            Node::link(LinkType::Norm, nodeMap[normInput.get()], newNodePtr);
        }
        for (const auto &ctrlInput : oldNodePtr->ctrlInputs()) {
            Node::link(LinkType::Ctrl, nodeMap[ctrlInput.get()], newNodePtr);
        }
    }

    ASSERT(exitNode_ != nullptr, "Cloning a graph without output node.");
    const auto &outputNode = exitNode_->normInputs().front();
    const auto &newOutput = nodeMap[outputNode.get()];
    const auto &newExitNode =
        ExitNode::create(*newGraph, newOutput->dataType(), newOutput->index());
    Node::link(LinkType::Norm, newOutput, newExitNode);
    newGraph->exitNode_ = newExitNode;

    return newGraph;
}

node_ptr_t Graph::inlineNode(const node_ptr_t &node, bool forceSync) {
    if (node->graph() != *this) {
        EXEC_WHEN_DEBUG(l.in("GIR").debug(
            "Cannot inline node {} from different graph {} into graph {}.",
            node->toString(),
            node->graph().name(),
            name_));
        return nullptr;
    }

    if (node->type() != NodeType::FUNC) {
        EXEC_WHEN_DEBUG(l.in("GIR").debug(
            "Cannot inline non-FUNC node {} in graph {}.",
            node->toString(),
            name_));
        return nullptr;
    }

    EXEC_WHEN_DEBUG(l.in("GIR").debug("Inlining node {} in graph {}.", node->toString(), name_));

    const auto &funcNode = tt::as_shared<FuncNode>(node);
    auto &targetGraph = funcNode->func()->graph();

    // sync node
    node_ptr_t syncNode = SyncNode::create(*this);

    std::unordered_map<Node *, node_ptr_t> nodeMap;

    // Process PORT nodes
    // If forceSync is enabled,
    // internal nodes are not allowed to directly connect to external nodes.
    // Instead, a new NREF node is created as an intermediary,
    // ensuring that all internal nodes execute after the sync node.
    const auto &normPorts = targetGraph.normPorts();
    const auto &normInputs = node->normInputs();
    ASSERT(
        normPorts.size() == normInputs.size(),
        "Number of norm ports and norm inputs do not match for inlining.");
    for (size_t i = 0; i < normPorts.size(); ++i) {
        if (forceSync) {
            node_ptr_t nrefNode = NRefNode::create(*this);
            Node::link(LinkType::Ctrl, syncNode, nrefNode);
            Node::link(LinkType::Norm, normInputs[i], nrefNode);
            nodeMap[normPorts[i].get()] = nrefNode;
        } else {
            nodeMap[normPorts[i].get()] = normInputs[i];
        }
    }

    const auto &withPorts = targetGraph.withPorts();
    const auto &withInputs = node->withInputs();
    ASSERT(
        withPorts.size() == withInputs.size(),
        "Number of with ports and with inputs do not match for inlining.");
    for (size_t i = 0; i < withPorts.size(); ++i) {
        if (forceSync) {
            node_ptr_t nrefNode = NRefNode::create(*this);
            Node::link(LinkType::Ctrl, syncNode, nrefNode);
            Node::link(LinkType::Norm, withInputs[i], nrefNode);
            nodeMap[withPorts[i].get()] = nrefNode;
        } else {
            nodeMap[withPorts[i].get()] = withInputs[i];
        }
    }

    ASSERT(targetGraph.closure().empty(), "Cannot inline a graph with closure.");

    for (const auto &n : targetGraph.nodes()) {
        const auto &clonedNode = n->clone(*this);
        nodeMap[n.get()] = clonedNode;
    }

    for (const auto &[oldNodePtr, newNodePtr] : nodeMap) {
        if (forceSync && oldNodePtr->inDegree() == 0) {
            // Ensure all zero-input nodes in the original graph execute after the sync node.
            // This guarantees the sync node is the initial node of the entire subgraph.
            if (oldNodePtr->type() == NodeType::PORT) {
                continue; // PORT nodes have already been processed
            }
            Node::link(LinkType::Ctrl, syncNode, newNodePtr);
        }

        for (const auto &withInput : oldNodePtr->withInputs()) {
            ASSERT(
                nodeMap.find(withInput.get()) != nodeMap.end(),
                "Input node not found in node map during inlining.");
            const auto &t = nodeMap[withInput.get()];
            Node::link(LinkType::With, t, newNodePtr);
        }
        for (const auto &normInput : oldNodePtr->normInputs()) {
            ASSERT(
                nodeMap.find(normInput.get()) != nodeMap.end(),
                "Input node not found in node map during inlining.");
            const auto &t = nodeMap[normInput.get()];
            Node::link(LinkType::Norm, t, newNodePtr);
        }
        for (const auto &ctrlInput : oldNodePtr->ctrlInputs()) {
            ASSERT(
                nodeMap.find(ctrlInput.get()) != nodeMap.end(),
                "Input node not found in node map during inlining.");
            const auto &t = nodeMap[ctrlInput.get()];
            Node::link(LinkType::Ctrl, t, newNodePtr);
        }
    }

    // Re-link outputs
    const auto &targetOutput = targetGraph.exitNode()->normInputs().front();
    ASSERT(nodeMap.find(targetOutput.get()) != nodeMap.end(), "Target output node not found.");
    const auto &inlinedOutput = nodeMap[targetOutput.get()];

    for (const auto &out : node->normOutputs()) {
        Node::link(LinkType::Norm, inlinedOutput, out);
    }
    for (const auto &out : node->withOutputs()) {
        Node::link(LinkType::With, inlinedOutput, out);
    }
    for (const auto &out : node->ctrlOutputs()) {
        Node::link(LinkType::Ctrl, inlinedOutput, out);
    }

    return syncNode;
}

void Graph::rearrange() {
    if (!dirty_) {
        l.in("GIR").debug("Graph {} is not dirty, no need to rearrange.", name_);
        return;
    }

    l.in("GIR").debug("Rearranging graph {}.", name_);

    // 0 reserved for null
    data_idx_t idx = 1;
    // index 0 reserved for null
    data_vec_t newStaticDataArr{nullptr};

    for (auto &node : normPorts_) {
        node->setIndex(idx++);
    }
    for (auto &node : withPorts_) {
        node->setIndex(idx++);
    }
    for (auto &node : closure_) {
        node->setIndex(idx++);
    }
    for (auto &node : nodes_) {
        NodeType type = node->type();
        ASSERT(type != NodeType::DREF, "DREF nodes should not exist in finalized graph.");
        if (type == NodeType::DATA) {
            const auto &dataNode = tt::as_shared<DataNode>(node);
            newStaticDataArr.push_back(dataNode->data());
            dataNode->setIndex(-(newStaticDataArr.size() - 1));
        } else {
            if (type == NodeType::SYNC || type == NodeType::NREF) {
                // Skip nodes without data
                continue;
            }
            node->setIndex(idx++);
        }
    }

    runtimeDataSize_ = idx;
    staticDataArr_ = std::move(newStaticDataArr);

    dirty_ = false;
}

/*
Node
*/

data_idx_t Node::index() const {
    // SYNC, DREF, and NREF nodes do not have data indices.
    // For NREF nodes, the data index is derived from their input node's index.
    ASSERT(nodeType_ != NodeType::SYNC, "SYNC node has no data index.");
    ASSERT(nodeType_ != NodeType::DREF, "DREF node has no data index.");
    if (nodeType_ == NodeType::NREF) {
        return normInputs_.front()->index();
    }
    return dataIndex_;
}

bool Node::hasDeepLinkedTo(const node_ptr_t &node, size_t maxJumps) const {
    if (maxJumps == 0) {
        return false;
    }

    // Use DFS to perform recursive checks
    // Used to avoid revisiting nodes
    std::unordered_set<const void *> visited;
    std::function<bool(const node_ptr_t &, size_t)> dfs;

    dfs = [&](const node_ptr_t &current, size_t jumpsLeft) -> bool {
        ASSERT(current, "Current node is null in DFS.");
        if (jumpsLeft == 0) {
            EXEC_WHEN_DEBUG(l.in("GIR").warn(
                "Deep link check reached max jumps at node: {}.",
                node->toString()));
            return false;
        }

        // Mark the current node as visited
        visited.insert(current.get());

        // Check all outputs of the current node
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

    // Perform recursive search starting from the current node.
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

void Node::link(LinkType type, const node_ptr_t &from, const node_ptr_t &to) {
    ASSERT(
        from->nodeType_ != NodeType::DREF,
        "DREF nodes cannot be linked as input to other nodes.");
    ASSERT(from && to, "Cannot link null nodes.");
    ASSERT(from != to, "Cannot link a node to itself.");
    ASSERT(
        &from->graph() == &to->graph(),
        std::format(
            "Cannot link nodes from different graphs: {} -{}-> {}. ",
            from->toString(),
            (type == LinkType::With ? "W" : (type == LinkType::Norm ? "N" : "C")),
            to->toString()));

    EXEC_WHEN_DEBUG(l.in("GIR").debug(
        "Linking nodes: {} -{}-> {}",
        from->toString(),
        (type == LinkType::With ? "W" : (type == LinkType::Norm ? "N" : "C")),
        to->toString()));

    switch (type) {
        // With link and Norm link allow multiple edges to exist.
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

bool Node::unlink(const node_ptr_t &from, const node_ptr_t &to) {
    ASSERT(from && to, "Cannot unlink null nodes.");
    ASSERT(from != to, "Cannot unlink a node from itself.");
    ASSERT(
        &from->graph() == &to->graph(),
        std::format(
            "Cannot unlink nodes from different graphs: {} -X- {}. ",
            from->toString(),
            to->toString()));

    EXEC_WHEN_DEBUG(
        l.in("GIR").debug("Unlinking nodes: {} -X- {}", from->toString(), to->toString()));

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

bool Node::replace(const node_ptr_t &oldNode, const node_ptr_t &newNode) {
    ASSERT(oldNode && newNode, "Cannot replace null nodes.");
    ASSERT(oldNode != newNode, "Cannot replace a node with itself.");
    EXEC_WHEN_DEBUG(
        l.in("GIR").debug("Replacing node: {} -> {}", oldNode->toString(), newNode->toString()));

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
    node_ptr_t self = shared_from_this();

    {
        auto tempWithInputs = withInputs_;
        for (auto &input : tempWithInputs) {
            if (!unlink(input, self)) {
                return false;
            }
        }
        auto tempNormInputs = normInputs_;
        for (auto &input : tempNormInputs) {
            if (!unlink(input, self)) {
                return false;
            }
        }
        auto tempCtrlInputs = ctrlInputs_;
        for (auto &input : tempCtrlInputs) {
            if (!unlink(input, self)) {
                return false;
            }
        }

        auto tempWithOutputs = withOutputs_;
        for (auto &output : tempWithOutputs) {
            if (!unlink(self, output)) {
                return false;
            }
        }
        auto tempNormOutputs = normOutputs_;
        for (auto &output : tempNormOutputs) {
            if (!unlink(self, output)) {
                return false;
            }
        }
        auto tempCtrlOutputs = ctrlOutputs_;
        for (auto &output : tempCtrlOutputs) {
            if (!unlink(self, output)) {
                return false;
            }
        }
    }

    return true;
}

} // namespace GraphIR
