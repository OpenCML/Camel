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
 * Created: Mar. 29, 2026
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */
/*
 * GraphBuilder 的实现与仅被其使用的文件内辅助函数（clone/inline/finalize/layout 等）。
 * Graph 类型自身的生命周期、调试指纹与 arena 冻结逻辑见 graph.cpp。
 */

#include "camel/compile/gir.h"
#include "camel/core/context/frame.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/conv.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/source/manager.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>
#include <ranges>
#include <stdexcept>
#include <unordered_set>

using namespace camel::core::error;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

namespace {

using namespace camel::core::rtdata;

constexpr std::size_t kSourceContextExtraIndex = 3;
constexpr size_t kPtrBytes                     = sizeof(void *);

Node *requireMappedNode(
    Node *node, const std::unordered_map<Node *, Node *> &nodeMap, const char *message) {
    auto it = nodeMap.find(node);
    ASSERT(it != nodeMap.end(), message);
    return it->second;
}

node_vec_t mapNodeVecPreservingOrder(
    node_span_t nodes, const std::unordered_map<Node *, Node *> &nodeMap, bool allowMissing,
    const char *message) {
    node_vec_t mapped;
    mapped.reserve(nodes.size());
    for (Node *node : nodes) {
        auto it = nodeMap.find(node);
        if (it == nodeMap.end()) {
            ASSERT(allowMissing, message);
            continue;
        }
        mapped.push_back(it->second);
    }
    return mapped;
}

void overwriteFreshNodeAdjacencyPreservingOrder(
    const node_vec_t &nodes, const std::unordered_map<Node *, Node *> &nodeMap,
    bool allowMissingOutputs = false) {
    for (Node *oldNode : nodes) {
        Node *newNode =
            requireMappedNode(oldNode, nodeMap, "Mapped node not found when copying edges.");
        // BRCH/JOIN 等控制流节点把边序当作分支槽位语义，因此 fresh clone 需要按源图顺序
        // 直接覆写邻接表，不能依赖 unordered_map 迭代后的 link 副作用去“碰运气”恢复顺序。
        detail::NodeMutation::withInputs(newNode) = mapNodeVecPreservingOrder(
            oldNode->withInputs(),
            nodeMap,
            false,
            "Mapped WITH input not found when copying edges.");
        detail::NodeMutation::normInputs(newNode) = mapNodeVecPreservingOrder(
            oldNode->normInputs(),
            nodeMap,
            false,
            "Mapped NORM input not found when copying edges.");
        detail::NodeMutation::ctrlInputs(newNode) = mapNodeVecPreservingOrder(
            oldNode->ctrlInputs(),
            nodeMap,
            false,
            "Mapped CTRL input not found when copying edges.");
        detail::NodeMutation::withOutputs(newNode) = mapNodeVecPreservingOrder(
            oldNode->withOutputs(),
            nodeMap,
            allowMissingOutputs,
            "Mapped WITH output not found when copying edges.");
        detail::NodeMutation::normOutputs(newNode) = mapNodeVecPreservingOrder(
            oldNode->normOutputs(),
            nodeMap,
            allowMissingOutputs,
            "Mapped NORM output not found when copying edges.");
        detail::NodeMutation::ctrlOutputs(newNode) = mapNodeVecPreservingOrder(
            oldNode->ctrlOutputs(),
            nodeMap,
            allowMissingOutputs,
            "Mapped CTRL output not found when copying edges.");
    }
}

void appendMappedOutputsPreservingOrder(
    Node *oldNode, Node *newNode, const std::unordered_map<Node *, Node *> &nodeMap) {
    for (Node *out : oldNode->withOutputs()) {
        if (auto it = nodeMap.find(out); it != nodeMap.end()) {
            detail::NodeMutation::withOutputs(newNode).push_back(it->second);
        }
    }
    for (Node *out : oldNode->normOutputs()) {
        if (auto it = nodeMap.find(out); it != nodeMap.end()) {
            detail::NodeMutation::normOutputs(newNode).push_back(it->second);
        }
    }
    for (Node *out : oldNode->ctrlOutputs()) {
        if (auto it = nodeMap.find(out); it != nodeMap.end()) {
            detail::NodeMutation::ctrlOutputs(newNode).push_back(it->second);
        }
    }
}

node_vec_t
collectEntryRootsFromExit(const Graph &graph, const std::unordered_map<Node *, Node *> &nodeMap) {
    auto isEntryExecutable = [](NodeType type) {
        switch (type) {
        case NodeType::DATA:
        case NodeType::PORT:
        case NodeType::SYNC:
        case NodeType::GATE:
        case NodeType::DREF:
            return false;
        default:
            return true;
        }
    };

    std::unordered_set<Node *> reachable;
    std::vector<Node *> stack{graph.exitNode()};
    while (!stack.empty()) {
        Node *curr = stack.back();
        stack.pop_back();
        if (!curr || !reachable.insert(curr).second) {
            continue;
        }
        for (Node *in : curr->normInputs()) {
            if (in && &in->graph() == &graph) {
                stack.push_back(in);
            }
        }
        for (Node *in : curr->withInputs()) {
            if (in && &in->graph() == &graph) {
                stack.push_back(in);
            }
        }
        for (Node *in : curr->ctrlInputs()) {
            if (in && &in->graph() == &graph) {
                stack.push_back(in);
            }
        }
    }

    node_vec_t roots;
    for (Node *oldNode : graph.nodes()) {
        if (!reachable.contains(oldNode) || !isEntryExecutable(oldNode->type())) {
            continue;
        }
        bool hasNonDataPortInput = false;
        auto scan                = [&](const auto &inputs) {
            for (Node *in : inputs) {
                if (!in || &in->graph() != &graph) {
                    continue;
                }
                if (in->type() != NodeType::DATA && in->type() != NodeType::PORT) {
                    hasNonDataPortInput = true;
                    break;
                }
            }
        };
        scan(oldNode->normInputs());
        if (!hasNonDataPortInput) {
            scan(oldNode->withInputs());
        }
        if (!hasNonDataPortInput) {
            scan(oldNode->ctrlInputs());
        }
        if (hasNonDataPortInput) {
            continue;
        }
        roots.push_back(requireMappedNode(oldNode, nodeMap, "Missing mapped entry root."));
    }
    return roots;
}

node_vec_t collectConsumersByInput(const Graph &graph, Node *needle, LinkType type) {
    node_vec_t consumers;
    std::unordered_set<Node *> seen;
    if (!needle) {
        return consumers;
    }
    auto maybePush = [&](Node *owner) {
        if (!owner) {
            return;
        }
        const node_span_t inputs = [&]() -> node_span_t {
            switch (type) {
            case LinkType::Norm:
                return owner->normInputs();
            case LinkType::With:
                return owner->withInputs();
            case LinkType::Ctrl:
                return owner->ctrlInputs();
            }
            return {};
        }();
        if (std::ranges::find(inputs, needle) != inputs.end()) {
            if (seen.insert(owner).second) {
                consumers.push_back(owner);
            }
        }
    };
    for (Node *owner : graph.nodes()) {
        maybePush(owner);
    }
    maybePush(graph.exitNode());
    return consumers;
}

bool hasSubgraphReference(const Graph &graph, const graph_ptr_t &subGraph) {
    for (const auto &[_, subGraphs] : graph.subGraphs()) {
        if (subGraphs.contains(subGraph)) {
            return true;
        }
    }
    return false;
}

node_vec_t collectUnreachableNodes(const Graph &graph) {
    Node *exit = graph.exitNode();
    std::unordered_set<Node *> live;
    std::vector<Node *> stack{exit};
    while (!stack.empty()) {
        Node *curr = stack.back();
        stack.pop_back();
        if (!curr || !live.insert(curr).second) {
            continue;
        }
        for (Node *in : curr->withInputs()) {
            stack.push_back(in);
        }
        for (Node *in : curr->normInputs()) {
            stack.push_back(in);
        }
        for (Node *in : curr->ctrlInputs()) {
            stack.push_back(in);
        }
    }

    node_vec_t unreachable;
    for (Node *node : graph.nodes()) {
        if (!live.contains(node)) {
            unreachable.push_back(node);
        }
    }
    return unreachable;
}

slot_t makeStaticSlotFromData(const data_ptr_t &data, camel::core::mm::IAllocator &allocator) {
    ASSERT(data != nullptr, "Static data cannot be null.");
    if (data->type()->isGCTraced()) {
        Object *obj = makeGCRefFromGCTracedData(data, allocator);
        return toSlot<Object *>(obj);
    }
    if (data->type()->isPrimitive()) {
        return makeSlotFromPrimitiveData(data);
    }
    ASSERT(
        false,
        std::format(
            "Unsupported static data type '{}' for slot conversion.",
            data->type()->toString()));
    return NullSlot;
}

size_t estimateFrozenBytesForFinalize(const Graph &graph) {
    size_t edgeCount = 0;
    for (Node *node : graph.nodes()) {
        edgeCount +=
            node->normInputs().size() + node->withInputs().size() + node->ctrlInputs().size();
        edgeCount +=
            node->normOutputs().size() + node->withOutputs().size() + node->ctrlOutputs().size();
    }
    edgeCount += graph.normPorts().size() + graph.withPorts().size() + graph.closure().size();
    // 经验模型：邻接数组 + static slots + 元数据冗余，避免 finalize 期间频繁扩块。
    const size_t adjacencyBytes = edgeCount * kPtrBytes;
    const size_t staticBytes    = graph.staticDataSize() * sizeof(slot_t);
    const size_t nodeBytes      = graph.nodes().size() * 16;
    return adjacencyBytes + staticBytes + nodeBytes + (16 * 1024);
}
} // namespace

graph_ptr_t GraphBuilder::createGraph(
    FunctionType *funcType, const graph_ptr_t &graph, const std::string &name) {
    ASSERT(funcType->hasMetaInfo(), "Trying to create a Graph with incomplete FunctionType.");
    static int anonymousIdx = 0;
    std::string graphName   = name.empty() ? std::format("__{}__", anonymousIdx++) : name;
    const auto newGraph     = std::make_shared<Graph>(funcType, graph, graphName);
    if (graph) {
        GraphBuilder(graph).addSubGraph(newGraph);
    }
    const size_t withCount = funcType->withTypesCount();
    for (size_t i = 0; i < funcType->normTypesCount(); ++i) {
        Node *portNode = PortNode::create(
            *newGraph,
            funcType->normTypeAt(i),
            std::string(funcType->argNameAt(withCount + i)),
            funcType->normIsVarAt(i));
        GraphBuilder(newGraph).addPort(portNode, false);
    }
    for (size_t i = 0; i < withCount; ++i) {
        Node *portNode = PortNode::create(
            *newGraph,
            funcType->withTypeAt(i),
            std::string(funcType->argNameAt(i)),
            funcType->withIsVarAt(i));
        GraphBuilder(newGraph).addPort(portNode, true);
    }
    return newGraph;
}

std::shared_ptr<GraphBuilderState> GraphBuilder::snapshotStateFromGraph(Graph &graph) {
    auto st             = std::make_shared<GraphBuilderState>();
    st->subGraphs       = graph.subGraphs_;
    st->dependencies    = graph.dependencies_;
    st->normPorts       = graph.normPorts_;
    st->withPorts       = graph.withPorts_;
    st->closure         = graph.closure_;
    st->nodes           = graph.nodes_;
    st->staticDataArr   = graph.staticDataArr_;
    st->funcType        = graph.signature_.funcType;
    st->staticDataType  = graph.signature_.staticDataType;
    st->runtimeDataType = graph.signature_.runtimeDataType;
    st->closureType     = graph.signature_.closureType;
    st->runtimeDataSize = graph.signature_.runtimeDataSize;
    st->exitNode        = graph.exitNode_;
    st->looped          = graph.looped_;
    st->parameterized   = graph.parameterized_;
    return st;
}

GraphBuilderState &GraphBuilder::state() const {
    ASSERT(
        graph_->sealState_ != SealState::Sealed,
        std::format("Cannot access draft staging on sealed graph '{}'.", graph_->name_));
    if (!graph_->builderState_) {
        graph_->builderState_ = snapshotStateFromGraph(*graph_);
    }
    return *graph_->builderState_;
}

void GraphBuilder::syncStateToGraph() const {
    ASSERT(
        graph_->builderState_ != nullptr,
        std::format("Cannot sync graph '{}' without draft staging.", graph_->name_));
    ASSERT(
        graph_->sealState_ != SealState::Sealed,
        std::format("Cannot sync staging into sealed graph '{}'.", graph_->name_));
    GraphBuilderState &st              = state();
    graph_->subGraphs_                 = st.subGraphs;
    graph_->dependencies_              = st.dependencies;
    graph_->normPorts_                 = st.normPorts;
    graph_->withPorts_                 = st.withPorts;
    graph_->closure_                   = st.closure;
    graph_->nodes_                     = st.nodes;
    graph_->staticDataArr_             = st.staticDataArr;
    graph_->signature_.funcType        = st.funcType;
    graph_->signature_.staticDataType  = st.staticDataType;
    graph_->signature_.runtimeDataType = st.runtimeDataType;
    graph_->signature_.closureType     = st.closureType;
    graph_->signature_.runtimeDataSize = st.runtimeDataSize;
    graph_->exitNode_                  = st.exitNode;
    graph_->looped_                    = st.looped;
    graph_->parameterized_             = st.parameterized;
}

void GraphBuilder::assertBuildable(const char *action) const {
    ASSERT(graph_ != nullptr, "GraphBuilder has been consumed by sealGraph().");
    ASSERT(
        graph_->sealState_ != SealState::Sealed,
        std::format("Cannot {} finalized graph '{}'. Clone it first.", action, graph_->name_));
}

void GraphBuilder::markMutated() const {
    // mutation 仅允许发生在 draft 工作态：
    // - sealed 图会在 assertBuildable() 提前失败；
    // - staging 是唯一可变源，Graph 字段只作为兼容镜像。
    graph_->sealState_  = SealState::Draft;
    graph_->frameSize_  = 0;
    graph_->staticArea_ = nullptr;
    // 保持 draft 期 Graph 视图与 staging 同步，避免旧读路径读取到过期字段。
    syncStateToGraph();
}

Node *GraphBuilder::ownNode(Node *node) const {
    ASSERT(node != nullptr, "Cannot own null node.");
    graph_->ownedNodes_.push_back(node);
    return node;
}

data_idx_t GraphBuilder::addStaticSlot(slot_t slot) const {
    assertBuildable("append static data to");
    auto &st = state();
    st.staticDataArr.push_back(slot);
    if (st.staticDataArr.size() > static_cast<size_t>(std::numeric_limits<arr_size_t>::max())) {
        throw std::overflow_error("staticDataArr_ exceeds arr_size_t max value");
    }
    markMutated();
    return -static_cast<data_idx_t>(st.staticDataArr.size() - 1);
}

data_idx_t GraphBuilder::addStaticData(const data_ptr_t &data) const {
    return addStaticSlot(makeStaticSlotFromData(data, graph_->arena_->allocator()));
}

data_idx_t GraphBuilder::addRuntimeData() const {
    assertBuildable("append runtime data to");
    auto &st = state();
    if (st.runtimeDataSize > static_cast<size_t>(std::numeric_limits<arr_size_t>::max())) {
        throw std::overflow_error("runtimeDataSize_ exceeds arr_size_t max value");
    }
    markMutated();
    return static_cast<data_idx_t>(st.runtimeDataSize++);
}

void GraphBuilder::setStaticSlot(data_idx_t index, slot_t slot) const {
    assertBuildable("set static data on");
    ASSERT(index < 0, "Static data index must be negative.");
    size_t idx = static_cast<size_t>(-index);
    ASSERT(
        idx < state().staticDataArr.size(),
        std::format(
            "Static data index out of range when setting data of graph ({}) at index {}. "
            "(total size: {})",
            graph_->name_,
            index,
            state().staticDataArr.size()));
    state().staticDataArr[idx] = slot;
    markMutated();
}

void GraphBuilder::setStaticData(data_idx_t index, const data_ptr_t &data) const {
    setStaticSlot(index, makeStaticSlotFromData(data, graph_->arena_->allocator()));
}

void GraphBuilder::addNode(Node *node) const {
    assertBuildable("modify");
    state().nodes.push_back(node);
    markMutated();
}

void GraphBuilder::eraseNode(Node *node) const {
    ASSERT(node != nullptr, "Cannot erase null node.");
    assertBuildable("modify");
    if (auto *sc = graph_->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
        sc->unbindGirNodeDraftDebug(node);
    }
    node->detach();
    state().nodes.erase(
        std::remove(state().nodes.begin(), state().nodes.end(), node),
        state().nodes.end());
    graph_->nodeStableIds_.erase(node);
    graph_->nodePortNames_.erase(node);
    graph_->nodeAccsKeys_.erase(node);
    markMutated();
}

void GraphBuilder::addPort(Node *node, bool isWith) const {
    assertBuildable("modify");
    auto &st = state();
    if (isWith) {
        ASSERT(
            std::find(st.withPorts.begin(), st.withPorts.end(), node) == st.withPorts.end(),
            "With port node already exists in the graph.");
        st.withPorts.push_back(node);
    } else {
        ASSERT(
            std::find(st.normPorts.begin(), st.normPorts.end(), node) == st.normPorts.end(),
            "Norm port node already exists in the graph.");
        st.normPorts.push_back(node);
    }
    markMutated();
}

void GraphBuilder::addClosure(Node *node) const {
    assertBuildable("modify");
    auto &st = state();
    ASSERT(
        std::find(st.closure.begin(), st.closure.end(), node) == st.closure.end(),
        "Closure node already exists in the graph.");
    auto *portNode = tt::as_ptr<PortNode>(node);
    st.closure.push_back(node);
    st.funcType->addClosureRef(portNode->name());
    markMutated();
}

void GraphBuilder::parametrizeClosure() const {
    assertBuildable("parameterize closure in");
    auto &st = state();
    st.withPorts.insert(st.withPorts.begin(), st.closure.begin(), st.closure.end());
    st.closure.clear();
    st.parameterized = true;
    // 不再立即 rearrange：slot 编号和 layout 统一在 finalize 时一次性导出。
    markMutated();
}
void GraphBuilder::setOutput(Node *node) const {
    assertBuildable("set output on");
    auto &st = state();
    ASSERT(
        st.exitNode == nullptr,
        std::format("Graph {} already has an output node.", graph_->name_));

    Type *actualExitType = node->dataType();
    if (st.funcType->hasExitType()) {
        Type *declaredExitType = st.funcType->exitType();
        if (!declaredExitType->assignableFrom(actualExitType)) {
            throw DiagnosticBuilder::of(SemanticDiag::ReturnTypeMismatch)
                .commit(
                    actualExitType->toString(),
                    declaredExitType->toString(),
                    graph_->name_ + ": " + st.funcType->toString());
        }
    } else {
        st.funcType->setExitType(actualExitType);
    }

    st.exitNode = node;
    markMutated();
}
void GraphBuilder::addSubGraph(const graph_ptr_t &graph) const {
    assertBuildable("add subgraph to");
    ASSERT(graph.get() != graph_, "Cannot add itself as a subgraph.");
    ASSERT(!graph->name().empty(), "Cannot add an anonymous graph as a subgraph.");
    auto &st = state();
    if (st.subGraphs.find(graph->name()) == st.subGraphs.end()) {
        st.subGraphs[graph->name()] = std::unordered_set<graph_ptr_t>({graph});
    } else {
        auto &existing = st.subGraphs[graph->name()];
        ASSERT(
            existing.find(graph) == existing.end(),
            std::format("Subgraph with name '{}' already exists.", graph->mangledName()));
        existing.insert(graph);
        GetDefaultLogger().in("GIR").debug(
            "Added subgraph '{}' to graph '{}'.",
            graph->mangledName(),
            graph_->name_);
    }
    graph->outer_ = graph_->shared_from_this();
    markMutated();
}

void GraphBuilder::eraseSubGraph(const graph_ptr_t &graph) const {
    assertBuildable("remove subgraph from");
    ASSERT(graph.get() != graph_, "Cannot remove itself as a subgraph.");
    ASSERT(!graph->name().empty(), "Cannot remove an anonymous graph as a subgraph.");
    auto &st = state();
    if (st.subGraphs.find(graph->name()) != st.subGraphs.end()) {
        auto &existing = st.subGraphs[graph->name()];
        existing.erase(graph);
        GetDefaultLogger().in("GIR").debug(
            "Removed subgraph '{}' from graph '{}'.",
            graph->mangledName(),
            graph_->name_);
        if (existing.empty()) {
            st.subGraphs.erase(graph->name());
        }
        graph->outer_.reset();
    }
    markMutated();
}

void GraphBuilder::addDependency(const graph_ptr_t &graph) const {
    assertBuildable("add dependency to");
    if (graph.get() == graph_) {
        state().looped = true;
        return;
    }
    state().dependencies.insert(graph);
    graph->dependents_.insert(graph_->shared_from_this());
    GetDefaultLogger().in("GIR").debug(
        "Added dependency: Graph '{}' depends on graph '{}'.",
        graph_->name_,
        graph->name());
    markMutated();
}

void GraphBuilder::eraseDependency(const graph_ptr_t &graph) const {
    assertBuildable("remove dependency from");
    state().dependencies.erase(graph);
    graph->dependents_.erase(graph_->shared_from_this());
    GetDefaultLogger().in("GIR").debug(
        "Removed dependency: Graph '{}' no longer depends on graph '{}'.",
        graph_->name_,
        graph->name());
    markMutated();
}

// =============================================================================
// Graph 克隆与内联
// =============================================================================

graph_ptr_t GraphBuilder::cloneGraph(const graph_ptr_t &graph) {
    if (!graph) {
        return nullptr;
    }
    const Graph *src              = graph.get();
    graph_ptr_t newGraph          = std::make_shared<Graph>(src->funcType(), nullptr, src->name_);
    newGraph->looped_             = src->looped();
    newGraph->parameterized_      = src->parameterized();
    newGraph->signature_.funcType = src->funcType();
    newGraph->staticDataArr_.clear();
    newGraph->staticDataArr_.reserve(src->staticDataSize());
    newGraph->staticDataArr_.push_back(NullSlot);
    for (size_t i = 1; i < src->staticDataSize(); ++i) {
        newGraph->staticDataArr_.push_back(src->getStaticDataSlot(-static_cast<data_idx_t>(i)));
    }
    newGraph->signature_.runtimeDataSize = src->runtimeDataSize();
    newGraph->signature_.staticDataType  = const_cast<TupleType *>(src->staticDataType());
    newGraph->signature_.runtimeDataType = const_cast<TupleType *>(src->runtimeDataType());
    newGraph->signature_.closureType     = const_cast<TupleType *>(src->closureType());
    // clone 后总是回到 Draft 状态，可继续编辑并重新 seal。
    newGraph->frameSize_  = 0;
    newGraph->staticArea_ = nullptr;

    if (const TupleType *staticType = src->staticDataType()) {
        for (size_t i = 1; i < newGraph->staticDataArr_.size() && i < staticType->size(); ++i) {
            if (staticType->codeAt(i) != TypeCode::Function) {
                continue;
            }
            auto *oldFunc = fromSlot<::Function *>(newGraph->staticDataArr_[i]);
            if (!oldFunc) {
                continue;
            }
            auto *clonedFunc = static_cast<::Function *>(
                oldFunc->clone(newGraph->arena()->allocator(), staticType->typeAt(i), false));
            newGraph->staticDataArr_[i] = toSlot<::Function *>(clonedFunc);
        }
    }

    if (auto *sourceContext =
            src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
        newGraph->setExtra<camel::source::SourceContext, kSourceContextExtraIndex>(sourceContext);
        sourceContext->cloneGirGraphDebugInfo(src->stableId(), newGraph->stableId());
    }

    std::unordered_map<Node *, Node *> nodeMap;
    for (Node *port : src->withPorts()) {
        Node *newPort = port->clone(*newGraph);
        detail::NodeMutation::setDataType(newPort, port->dataType());
        nodeMap[port] = newPort;
        GraphBuilder(newGraph).addPort(newPort, true);
        if (auto *sourceContext =
                src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
            sourceContext->cloneGirNodeDebugBinding(port, newPort);
        }
    }
    for (Node *port : src->normPorts()) {
        Node *newPort = port->clone(*newGraph);
        detail::NodeMutation::setDataType(newPort, port->dataType());
        nodeMap[port] = newPort;
        GraphBuilder(newGraph).addPort(newPort, false);
        if (auto *sourceContext =
                src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
            sourceContext->cloneGirNodeDebugBinding(port, newPort);
        }
    }
    for (Node *closureNode : src->closure()) {
        Node *newClosureNode = closureNode->clone(*newGraph);
        detail::NodeMutation::setDataType(newClosureNode, closureNode->dataType());
        nodeMap[closureNode] = newClosureNode;
        GraphBuilder(newGraph).addClosure(newClosureNode);
        if (auto *sourceContext =
                src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
            sourceContext->cloneGirNodeDebugBinding(closureNode, newClosureNode);
        }
    }
    for (Node *node : src->nodes()) {
        Node *newNode = node->clone(*newGraph);
        detail::NodeMutation::setDataType(newNode, node->dataType());
        nodeMap[node] = newNode;
        if (auto *sourceContext =
                src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
            sourceContext->cloneGirNodeDebugBinding(node, newNode);
        }
    }

    overwriteFreshNodeAdjacencyPreservingOrder(src->withPorts(), nodeMap, true);
    overwriteFreshNodeAdjacencyPreservingOrder(src->normPorts(), nodeMap, true);
    overwriteFreshNodeAdjacencyPreservingOrder(src->closure(), nodeMap, true);
    overwriteFreshNodeAdjacencyPreservingOrder(src->nodes(), nodeMap, true);
    Node *srcExit   = src->exitNode();
    Node *newOutput = requireMappedNode(
        srcExit,
        nodeMap,
        "Output node not found in node map during graph cloning.");
    newGraph->exitNode_ = newOutput;
    if (newGraph->builderState_) {
        newGraph->builderState_->exitNode = newOutput;
    }

    // 浅拷贝策略：共享依赖与子图引用，不递归克隆。
    // 但子图仅保留“真实词法拥有”关系（sub->outer == src）。
    // 某些历史图可能在 subGraphs 中混入别名引用；若直接复制会污染 outer 层级，
    // 导致重写后出现错误嵌套（如全局函数被挂到分支 arm 子图下）。
    newGraph->subGraphs_.clear();
    for (const auto &[name, subGraphs] : src->subGraphs()) {
        std::unordered_set<graph_ptr_t> lexicalOwned;
        for (const auto &subGraph : subGraphs) {
            if (!subGraph) {
                continue;
            }
            const auto outer = subGraph->outer();
            if (outer && outer.get() == src) {
                lexicalOwned.insert(subGraph);
            }
        }
        if (!lexicalOwned.empty()) {
            newGraph->subGraphs_[name] = std::move(lexicalOwned);
        }
    }
    newGraph->dependencies_ = src->dependencies();
    for (const auto &dep : newGraph->dependencies_) {
        dep->dependents_.insert(newGraph);
    }
    ASSERT(
        newGraph->exitNode_ != nullptr,
        std::format(
            "cloneGraph produced graph '{}' without output (source='{}').",
            newGraph->name_,
            src->name()));
    return newGraph;
}

void GraphBuilder::finalize() const {
    if (graph_->sealState_ == SealState::Sealed) {
        return;
    }
    if (graph_->builderState_) {
        syncStateToGraph();
        // finalize 阶段仅以 Graph 固化字段为准，避免 staging 旧视图干扰布局与 frame 安装。
        graph_->builderState_.reset();
    }
    ASSERT(graph_->sealState_ == SealState::Draft, "Graph seal state is invalid.");
    graph_->sealState_ = SealState::Sealing;
    graph_->arena_->reserveFrozenBytes(estimateFrozenBytesForFinalize(*graph_));
    rearrange();
    if (auto *sc = graph_->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
        graph_->promoteNodeDebugIds(sc);
    } else {
        graph_->promoteNodeDebugIds(nullptr);
    }
    // finalize 之后立即冻结邻接，避免出现 finalized=true 但节点仍为 draft 邻接的分裂状态。
    auto &arena = *graph_->arena_;
    for (Node *owned : graph_->ownedNodes_) {
        if (owned && !owned->isFrozen()) {
            owned->freezeAdjacency(arena);
        }
    }
    graph_->packStaticSlotsToFrozen();
    graph_->installFinalFrameLayout();
    graph_->arena_->releaseDraftRegion();
    graph_->sealState_ = SealState::Sealed;
    graph_->builderState_.reset();
    EXEC_WHEN_DEBUG(
        const auto &m = graph_->arena_->metrics(); GetDefaultLogger().in("GIR").debug(
            "Seal graph '{}': peak={}B waste={}B blocks={} draftFreed={}B allocFail={}.",
            graph_->name(),
            m.peakBytes,
            m.wasteBytes,
            m.blockCount,
            m.draftFreedBytes,
            m.allocFailCount));
}

void GraphBuilder::sealGraph() {
    finalize();
    // consume 语义：seal 后 builder 不可再使用。
    if (graph_ != nullptr) {
        graph_->builderState_.reset();
    }
    graph_ = nullptr;
}

void GraphBuilder::sealGraphRecursively(const graph_ptr_t &graph) {
    if (!graph) {
        return;
    }
    std::unordered_set<Graph *> visited;
    std::function<void(Graph *)> sealDfs = [&](Graph *curr) {
        if (curr == nullptr || !visited.insert(curr).second) {
            return;
        }
        GraphBuilder(curr).sealGraph();
        for (const auto &[_, subGraphs] : curr->subGraphs()) {
            for (const auto &subGraph : subGraphs) {
                sealDfs(subGraph.get());
            }
        }
        for (const auto &dep : curr->dependencies()) {
            sealDfs(dep.get());
        }
    };
    sealDfs(graph.get());
}

InlineResult GraphBuilder::inlineCallable(Node *node, const InlineOptions &options) const {
    assertBuildable("inline into");
    InlineResult result;
    result.callNode = node;

    if (node->graph() != *graph_) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("GIR").debug(
                "Cannot inline node {} from different graph {} into graph {}.",
                node->toString(),
                node->graph().name(),
                graph_->name_));
        return result;
    }

    if (node->type() != NodeType::FUNC) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("GIR").debug(
                "Cannot inline non-FUNC node {} in graph {}.",
                node->toString(),
                graph_->name_));
        return result;
    }

    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("GIR").debug(
            "Inlining node {} in graph {}.",
            node->toString(),
            graph_->name_));

    auto *funcNode = tt::as_ptr<FuncNode>(node);
    if (!funcNode->bodyGraph()) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("GIR").warn(
                "Cannot inline FUNC node {} in graph {}: bodyGraph is null.",
                node->toString(),
                graph_->name_));
        return result;
    }
    auto &targetGraph = *funcNode->bodyGraph();
    auto *sourceContext =
        graph_->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>();
    std::unordered_map<Node *, Node *> nodeMap;
    const node_vec_t entryRootsBeforeClone = [&]() -> node_vec_t {
        std::unordered_map<Node *, Node *> identity;
        for (Node *n : targetGraph.nodes()) {
            identity.emplace(n, n);
        }
        return collectEntryRootsFromExit(targetGraph, identity);
    }();
    std::vector<std::pair<Node *, Node *>> portBindings;

    const auto &normPorts  = targetGraph.normPorts();
    const auto &withPorts  = targetGraph.withPorts();
    const auto &closure    = targetGraph.closure();
    const auto &normInputs = node->normInputs();
    const auto &withInputs = node->withInputs();
    auto collectFreeInputs = [&]() {
        std::unordered_set<Node *> declared;
        for (Node *p : normPorts) {
            declared.insert(p);
        }
        for (Node *p : withPorts) {
            declared.insert(p);
        }
        for (Node *c : closure) {
            declared.insert(c);
        }
        for (Node *n : targetGraph.nodes()) {
            declared.insert(n);
        }
        Node *exitAnchor = targetGraph.exitNode();
        if (&exitAnchor->graph() == &targetGraph) {
            declared.insert(exitAnchor);
        }

        std::unordered_set<Node *> seen;
        node_vec_t freeInputs;
        auto scanInputs = [&](Node *n) {
            auto pull = [&](const auto &inputs) {
                for (Node *in : inputs) {
                    if (declared.contains(in)) {
                        continue;
                    }
                    if (seen.insert(in).second) {
                        freeInputs.push_back(in);
                    }
                }
            };
            pull(n->normInputs());
            pull(n->withInputs());
            pull(n->ctrlInputs());
        };
        for (Node *n : targetGraph.nodes()) {
            scanInputs(n);
        }
        exitAnchor = targetGraph.exitNode();
        if (!declared.contains(exitAnchor) && seen.insert(exitAnchor).second) {
            freeInputs.push_back(exitAnchor);
        }
        scanInputs(exitAnchor);
        return freeInputs;
    };
    node_vec_t freeInputs = collectFreeInputs();

    if (normPorts.size() == normInputs.size() &&
        (withPorts.size() + closure.size() + freeInputs.size()) == withInputs.size()) {
        for (size_t i = 0; i < normPorts.size(); ++i) {
            portBindings.emplace_back(normPorts[i], normInputs[i]);
        }
        for (size_t i = 0; i < withPorts.size(); ++i) {
            portBindings.emplace_back(withPorts[i], withInputs[i]);
        }
        for (size_t i = 0; i < closure.size(); ++i) {
            portBindings.emplace_back(closure[i], withInputs[withPorts.size() + i]);
        }
        for (size_t i = 0; i < freeInputs.size(); ++i) {
            portBindings.emplace_back(
                freeInputs[i],
                withInputs[withPorts.size() + closure.size() + i]);
        }
    } else {
        // 兼容旧 API 迁移期：当 norm/with 分桶不一致时，退化为按参数总序绑定。
        node_vec_t allPorts = targetGraph.ports();
        allPorts.insert(allPorts.end(), closure.begin(), closure.end());
        allPorts.insert(allPorts.end(), freeInputs.begin(), freeInputs.end());
        node_vec_t allInputs;
        allInputs.reserve(normInputs.size() + withInputs.size());
        allInputs.insert(allInputs.end(), normInputs.begin(), normInputs.end());
        allInputs.insert(allInputs.end(), withInputs.begin(), withInputs.end());
        if (allPorts.size() != allInputs.size()) {
            EXEC_WHEN_DEBUG(
                GetDefaultLogger().in("GIR").warn(
                    "Cannot inline FUNC node {} in graph {}: port/input count mismatch (ports={}, "
                    "norm={}, with={}).",
                    node->toString(),
                    graph_->name_,
                    allPorts.size(),
                    normInputs.size(),
                    withInputs.size()));
            return InlineResult{};
        }
        for (size_t i = 0; i < allPorts.size(); ++i) {
            portBindings.emplace_back(allPorts[i], allInputs[i]);
        }
    }

    const bool needParameterGates = !node->ctrlInputs().empty() || !node->ctrlOutputs().empty();
    std::unordered_map<Node *, Node *> gateByInput;
    node_vec_t parameterGateTargets;
    if (needParameterGates) {
        parameterGateTargets.reserve(portBindings.size());
    }
    for (const auto &[port, input] : portBindings) {
        if (!needParameterGates) {
            nodeMap[port] = input;
            continue;
        }
        auto gateIt = gateByInput.find(input);
        Node *gatedInput;
        if (gateIt == gateByInput.end()) {
            auto *nrefNode = GateNode::create(*graph_);
            detail::NodeMutation::setDataType(nrefNode, input->dataType());
            Node::link(LinkType::Norm, input, nrefNode);
            gateByInput.emplace(input, nrefNode);
            parameterGateTargets.push_back(nrefNode);
            gatedInput = nrefNode;
        } else {
            gatedInput = gateIt->second;
        }
        nodeMap[port] = gatedInput;
    }

    for (Node *n : targetGraph.nodes()) {
        Node *clonedNode = n->clone(*graph_);
        nodeMap[n]       = clonedNode;
        if (sourceContext) {
            sourceContext->cloneGirNodeDebugBinding(n, clonedNode);
        }
    }

    overwriteFreshNodeAdjacencyPreservingOrder(targetGraph.nodes(), nodeMap, true);
    for (Node *port : targetGraph.normPorts()) {
        appendMappedOutputsPreservingOrder(
            port,
            requireMappedNode(port, nodeMap, "Norm port not found in node map during inlining."),
            nodeMap);
    }
    for (Node *port : targetGraph.withPorts()) {
        appendMappedOutputsPreservingOrder(
            port,
            requireMappedNode(port, nodeMap, "With port not found in node map during inlining."),
            nodeMap);
    }
    Node *targetOutput = targetGraph.exitNode();
    if (auto outIt = nodeMap.find(targetOutput); outIt != nodeMap.end()) {
        result.valueExit = outIt->second;
    } else {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("GIR").warn(
                "Cannot inline FUNC node {} in graph {}: target output anchor is not mapped.",
                node->toString(),
                graph_->name_));
        return InlineResult{};
    }
    const node_vec_t normConsumers = collectConsumersByInput(*graph_, node, LinkType::Norm);
    const node_vec_t withConsumers = collectConsumersByInput(*graph_, node, LinkType::With);
    const node_vec_t ctrlConsumers = collectConsumersByInput(*graph_, node, LinkType::Ctrl);
    node_vec_t entryTargets;
    if (!parameterGateTargets.empty()) {
        entryTargets = parameterGateTargets;
    } else {
        entryTargets.reserve(entryRootsBeforeClone.size());
        for (Node *oldRoot : entryRootsBeforeClone) {
            Node *mapped =
                requireMappedNode(oldRoot, nodeMap, "Mapped entry root not found after inlining.");
            if (std::ranges::find(entryTargets, mapped) == entryTargets.end()) {
                entryTargets.push_back(mapped);
            }
        }
    }
    if (entryTargets.empty()) {
        // 当子图仅由 DATA/PORT 直达值出口锚点时，反向推导可能得不到可执行入口根。
        // 此时将 valueExit 作为隐式入口目标，再按统一收敛逻辑求 ctrlEntry。
        entryTargets.push_back(result.valueExit);
    }
    if (entryTargets.size() > 1) {
        auto *entrySync          = SyncNode::create(*graph_);
        result.insertedEntrySync = true;
        for (Node *targetEntry : entryTargets) {
            Node::link(LinkType::Ctrl, entrySync, targetEntry);
        }
        result.ctrlEntry = entrySync;
    }
    if (entryTargets.size() == 1) {
        result.ctrlEntry = entryTargets.front();
    }

    Node *completionCtrl = result.valueExit;
    ASSERT(completionCtrl != nullptr, "Inlined callable has no control completion anchor.");

    for (auto *out : normConsumers) {
        Node::replaceInput(LinkType::Norm, out, node, result.valueExit);
    }
    for (auto *out : withConsumers) {
        Node::replaceInput(LinkType::With, out, node, result.valueExit);
    }

    if (!ctrlConsumers.empty()) {
        ASSERT(
            completionCtrl != nullptr,
            "Inlined callable with control consumers has no control exit.");
        for (auto *out : ctrlConsumers) {
            Node::replaceInput(LinkType::Ctrl, out, node, completionCtrl);
        }
    }

    if (options.importReferencedGraphs) {
        for (const auto &[_, subGraphs] : targetGraph.subGraphs()) {
            for (const auto &subGraph : subGraphs) {
                if (hasSubgraphReference(*graph_, subGraph)) {
                    continue;
                }
                GraphBuilder(graph_).addDependency(subGraph);
                result.importedSubgraphs.push_back(subGraph);
                result.importedDependencies.push_back(subGraph);
            }
        }
        for (const auto &dep : targetGraph.dependencies()) {
            if (graph_->dependencies().contains(dep)) {
                continue;
            }
            GraphBuilder(graph_).addDependency(dep);
            result.importedDependencies.push_back(dep);
        }
    }

    markMutated();
    return result;
}

void GraphBuilder::pruneUnreachable() const {
    assertBuildable("prune unreachable nodes from");
    node_vec_t unreachable = collectUnreachableNodes(*graph_);
    for (Node *node : unreachable) {
        eraseNode(node);
    }
    if (!unreachable.empty()) {
        markMutated();
    }
}

LayoutResult GraphBuilder::computeLayout(const Graph &graph) {
    LayoutResult result;
    data_idx_t stcIdx = -1, rtmIdx = 1;
    result.staticDataArr = {NullSlot};
    type_vec_t staticDataTypes{Type::Void()}, runtimeDataTypes{Type::Void()}, closureTypes;

    // 用于 exitNode 查找其 input 的新 index
    std::unordered_map<Node *, data_idx_t> indexMap;

    auto assignIndex = [&](Node *node, data_idx_t idx) {
        result.nodeIndices.emplace_back(node, idx);
        indexMap[node] = idx;
    };

    for (Node *node : graph.normPorts()) {
        assignIndex(node, rtmIdx++);
        runtimeDataTypes.push_back(node->dataType());
    }
    for (Node *node : graph.withPorts()) {
        assignIndex(node, rtmIdx++);
        runtimeDataTypes.push_back(node->dataType());
    }
    for (Node *node : graph.closure()) {
        assignIndex(node, rtmIdx++);
        runtimeDataTypes.push_back(node->dataType());
        closureTypes.push_back(node->dataType());
    }
    for (Node *node : graph.nodes()) {
        NodeType type = node->type();
        ASSERT(type != NodeType::DREF, "DREF nodes should not exist in finalized graph.");
        if (type == NodeType::DATA) {
            auto *dataNode = tt::as_ptr<DataNode>(node);
            result.staticDataArr.push_back(dataNode->dataSlot());
            assignIndex(dataNode, stcIdx--);
            staticDataTypes.push_back(dataNode->dataType());
        } else {
            if (type == NodeType::SYNC || type == NodeType::GATE) {
                continue;
            }
            assignIndex(node, rtmIdx++);
            runtimeDataTypes.push_back(node->dataType());
        }
    }

    result.runtimeDataSize = rtmIdx;
    result.staticDataType  = TupleType::create(std::move(staticDataTypes));
    result.runtimeDataType = TupleType::create(std::move(runtimeDataTypes));
    result.closureType     = TupleType::create(std::move(closureTypes));
    return result;
}

void GraphBuilder::applyLayout(Graph &graph, const LayoutResult &layout) {
    for (auto [node, idx] : layout.nodeIndices) {
        detail::NodeMutation::setIndex(node, idx);
    }
    graph.signature_.runtimeDataSize = layout.runtimeDataSize;
    graph.staticDataArr_             = layout.staticDataArr;
    graph.signature_.staticDataType  = layout.staticDataType;
    graph.signature_.runtimeDataType = layout.runtimeDataType;
    graph.signature_.closureType     = layout.closureType;
    graph.frameSize_                 = 0;
    graph.staticArea_                = nullptr;
}

void GraphBuilder::rearrange() const {
    GetDefaultLogger().in("GIR").debug("Rearranging graph {}.", graph_->name_);
    auto layout = computeLayout(*graph_);
    applyLayout(*graph_, layout);
}
} // namespace camel::compile::gir
