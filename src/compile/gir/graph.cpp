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
 * Updated: Feb. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "compile/gir.h"
#include "error/diagnostics/diagnostics.h"

namespace GraphIR {

// =============================================================================
// Graph 创建与节点管理
// =============================================================================

graph_ptr_t
Graph::create(FunctionType *funcType, const graph_ptr_t &graph, const std::string &name) {
    ASSERT(funcType->hasMetaInfo(), "Trying to create a Graph with incomplete FunctionType.");
    static int anonymousIdx = 0;
    std::string graphName   = name.empty() ? std::format("__{}__", anonymousIdx++) : name;
    const auto newGraph     = std::make_shared<Graph>(funcType, graph, graphName);
    if (graph) {
        graph->addSubGraph(newGraph);
    }
    const size_t withCount = funcType->withTypesCount();
    for (size_t i = 0; i < funcType->normTypesCount(); ++i) {
        node_ptr_t portNode = PortNode::create(
            *newGraph,
            funcType->normTypeAt(i),
            std::string(funcType->argNameAt(withCount + i)),
            funcType->normIsVarAt(i));
        newGraph->addPort(portNode, false);
    }
    for (size_t i = 0; i < withCount; ++i) {
        node_ptr_t portNode = PortNode::create(
            *newGraph,
            funcType->withTypeAt(i),
            std::string(funcType->argNameAt(i)),
            funcType->withIsVarAt(i));
        newGraph->addPort(portNode, true);
    }
    return newGraph;
}

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

const node_ptr_t &Graph::exitNode() const {
    ASSERT(exitNode_ != nullptr, std::format("Graph {} has no exit node.", name_));
    return exitNode_;
}
const node_ptr_t &Graph::outputNode() const {
    ASSERT(exitNode_ != nullptr, std::format("Graph {} has no exit node.", name_));
    return exitNode_->normInputs().front();
}

void Graph::setOutput(const node_ptr_t &node) {
    ASSERT(exitNode_ == nullptr, std::format("Graph {} already has an output node.", name_));

    Type *actualExitType = node->dataType();
    if (funcType_->hasExitType()) {
        Type *declaredExitType = funcType_->exitType();
        if (!actualExitType->assignable(declaredExitType)) {
            throw DiagnosticBuilder::of(SemanticDiag::ReturnTypeMismatch)
                .commit(
                    actualExitType->toString(),
                    declaredExitType->toString(),
                    name_ + ": " + funcType_->toString());
        }
    } else {
        funcType_->setExitType(actualExitType);
    }

    exitNode_ = ExitNode::create(*this, node->dataType(), node->index());
    Node::link(LinkType::Norm, node, exitNode_);
}

// =============================================================================
// Graph 查询与数据段
// =============================================================================

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

// =============================================================================
// Graph 子图与依赖
// =============================================================================

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

// =============================================================================
// Graph 克隆与内联
// =============================================================================

graph_ptr_t Graph::clone() const {
    graph_ptr_t newGraph =
        Graph::create(tt::as_ptr<FunctionType>(funcType_->clone()), outer_.lock(), name_);
    newGraph->looped_          = looped_;
    newGraph->parameterized_   = parameterized_;
    newGraph->funcType_        = funcType_;
    newGraph->staticDataArr_   = staticDataArr_;
    newGraph->runtimeDataSize_ = runtimeDataSize_;
    newGraph->staticDataType_  = staticDataType_;
    newGraph->runtimeDataType_ = runtimeDataType_;
    newGraph->closureType_     = closureType_;

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
        const auto &newNode    = node->clone(*newGraph);
        nodeMap[newNode.get()] = newNode;
    }

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
    const auto &newOutput  = nodeMap[outputNode.get()];
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
    auto &targetGraph    = funcNode->func()->graph();

    node_ptr_t syncNode = SyncNode::create(*this);
    std::unordered_map<Node *, node_ptr_t> nodeMap;

    const auto &normPorts  = targetGraph.normPorts();
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

    const auto &withPorts  = targetGraph.withPorts();
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
        nodeMap[n.get()]       = clonedNode;
    }

    for (const auto &[oldNodePtr, newNodePtr] : nodeMap) {
        if (forceSync && oldNodePtr->inDegree() == 0) {
            if (oldNodePtr->type() == NodeType::PORT) {
                continue;
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

    data_idx_t stcIdx = -1, rtmIdx = 1;
    data_vec_t newStaticDataArr{Data::null()};
    type_vec_t staticDataTypes{Type::Void()}, runtimeDataTypes{Type::Void()}, closureTypes;

    for (auto &node : normPorts_) {
        node->setIndex(rtmIdx++);
        runtimeDataTypes.push_back(node->dataType());
    }
    for (auto &node : withPorts_) {
        node->setIndex(rtmIdx++);
        runtimeDataTypes.push_back(node->dataType());
    }
    for (auto &node : closure_) {
        node->setIndex(rtmIdx++);
        runtimeDataTypes.push_back(node->dataType());
        closureTypes.push_back(node->dataType());
    }
    for (auto &node : nodes_) {
        NodeType type = node->type();
        ASSERT(type != NodeType::DREF, "DREF nodes should not exist in finalized graph.");
        if (type == NodeType::DATA) {
            const auto &dataNode = tt::as_shared<DataNode>(node);
            newStaticDataArr.push_back(dataNode->data());
            dataNode->setIndex(stcIdx--);
            staticDataTypes.push_back(dataNode->dataType());
        } else {
            if (type == NodeType::SYNC || type == NodeType::NREF) {
                continue;
            }
            node->setIndex(rtmIdx++);
            runtimeDataTypes.push_back(node->dataType());
        }
    }

    if (exitNode_) {
        exitNode_->setIndex(exitNode_->normInputs().front()->index());
    }

    runtimeDataSize_ = rtmIdx;
    staticDataArr_   = std::move(newStaticDataArr);
    staticDataType_  = TupleType::create(std::move(staticDataTypes));
    runtimeDataType_ = TupleType::create(std::move(runtimeDataTypes));
    closureType_     = TupleType::create(std::move(closureTypes));

    dirty_ = false;
}

} // namespace GraphIR
