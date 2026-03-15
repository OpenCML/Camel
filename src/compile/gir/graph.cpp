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
 * Updated: Mar. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/compile/gir.h"
#include "camel/core/context/frame.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/conv.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/source/manager.h"

#include <atomic>

using namespace camel::core::error;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

namespace {

using namespace camel::core::rtdata;

data_ptr_t materializeStaticSlot(slot_t slot, Type *type);
data_ptr_t remapDataGraphRefs(
    const data_ptr_t &data, const std::unordered_map<const Graph *, graph_ptr_t> &graphMap);
constexpr std::size_t kSourceContextExtraIndex = 3;

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
        NodeMutation::withInputs(newNode) = mapNodeVecPreservingOrder(
            oldNode->withInputs(),
            nodeMap,
            false,
            "Mapped WITH input not found when copying edges.");
        NodeMutation::normInputs(newNode) = mapNodeVecPreservingOrder(
            oldNode->normInputs(),
            nodeMap,
            false,
            "Mapped NORM input not found when copying edges.");
        NodeMutation::ctrlInputs(newNode) = mapNodeVecPreservingOrder(
            oldNode->ctrlInputs(),
            nodeMap,
            false,
            "Mapped CTRL input not found when copying edges.");
        NodeMutation::withOutputs(newNode) = mapNodeVecPreservingOrder(
            oldNode->withOutputs(),
            nodeMap,
            allowMissingOutputs,
            "Mapped WITH output not found when copying edges.");
        NodeMutation::normOutputs(newNode) = mapNodeVecPreservingOrder(
            oldNode->normOutputs(),
            nodeMap,
            allowMissingOutputs,
            "Mapped NORM output not found when copying edges.");
        NodeMutation::ctrlOutputs(newNode) = mapNodeVecPreservingOrder(
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
            NodeMutation::withOutputs(newNode).push_back(it->second);
        }
    }
    for (Node *out : oldNode->normOutputs()) {
        if (auto it = nodeMap.find(out); it != nodeMap.end()) {
            NodeMutation::normOutputs(newNode).push_back(it->second);
        }
    }
    for (Node *out : oldNode->ctrlOutputs()) {
        if (auto it = nodeMap.find(out); it != nodeMap.end()) {
            NodeMutation::ctrlOutputs(newNode).push_back(it->second);
        }
    }
}

data_ptr_t materializeStaticObject(Object *obj, Type *type) {
    if (obj == nullptr) {
        return Data::null();
    }

    switch (type->code()) {
    case TypeCode::String:
        return std::make_shared<StringData>(static_cast<::String *>(obj)->toString());

    case TypeCode::Tuple: {
        auto *tuple          = static_cast<::Tuple *>(obj);
        auto *tupleType      = tt::as_ptr<TupleType>(type);
        data_vec_t dataItems = {};
        dataItems.reserve(tupleType->size());
        for (size_t i = 0; i < tupleType->size(); ++i) {
            dataItems.push_back(materializeStaticSlot(tuple->get<slot_t>(i), tupleType->typeAt(i)));
        }
        return TupleData::create(type, std::move(dataItems));
    }

    case TypeCode::Array: {
        auto *array          = static_cast<::Array *>(obj);
        auto *arrayType      = tt::as_ptr<ArrayType>(type);
        data_vec_t dataItems = {};
        dataItems.reserve(array->size());
        for (size_t i = 0; i < array->size(); ++i) {
            dataItems.push_back(
                materializeStaticSlot(array->get<slot_t>(i), arrayType->elemType()));
        }
        return ArrayData::from(arrayType->elemType(), dataItems);
    }

    case TypeCode::Struct: {
        auto *st       = static_cast<::Struct *>(obj);
        auto *structTy = tt::as_ptr<StructType>(type);
        std::map<std::string, data_ptr_t> fields;
        for (size_t i = 0; i < structTy->size(); ++i) {
            fields.emplace(
                std::string(structTy->fieldName(i)),
                materializeStaticSlot(st->get<slot_t>(i), structTy->typeAt(i)));
        }
        return StructData::create(std::move(fields));
    }

    case TypeCode::Function: {
        auto *fn            = static_cast<::Function *>(obj);
        auto result         = FunctionData::create(*fn->graph());
        const auto *closure = fn->tuple();
        if (closure && closure->size() > 0) {
            const auto *closureType = fn->graph()->closureType();
            data_vec_t closureData  = {};
            closureData.reserve(closure->size());
            for (size_t i = 0; i < closure->size(); ++i) {
                closureData.push_back(
                    materializeStaticSlot(closure->get<slot_t>(i), closureType->typeAt(i)));
            }
            result->resolve(closureData);
        }
        return result;
    }

    default:
        ASSERT(false, std::format("Unsupported GC-traced static type '{}'.", type->toString()));
        return Data::null();
    }
}

data_ptr_t materializeStaticSlot(slot_t slot, Type *type) {
    switch (type->code()) {
    case TypeCode::Void:
        return Data::null();
    case TypeCode::Int32:
        return std::make_shared<IntData>(fromSlot<Int32>(slot));
    case TypeCode::Int64:
        return std::make_shared<LongData>(fromSlot<Int64>(slot));
    case TypeCode::Float32:
        return std::make_shared<FloatData>(fromSlot<Float32>(slot));
    case TypeCode::Float64:
        return std::make_shared<DoubleData>(fromSlot<Float64>(slot));
    case TypeCode::Bool:
        return std::make_shared<BoolData>(fromSlot<Bool>(slot));
    case TypeCode::Byte:
        return std::make_shared<PrimaryData<char>>(static_cast<char>(fromSlot<Byte>(slot)));
    default:
        if (type->isGCTraced()) {
            return materializeStaticObject(fromSlot<Object *>(slot), type);
        }
        ASSERT(false, std::format("Unsupported static slot type '{}'.", type->toString()));
        return Data::null();
    }
}

data_ptr_t remapDataGraphRefs(
    const data_ptr_t &data, const std::unordered_map<const Graph *, graph_ptr_t> &graphMap) {
    if (!data) {
        return data;
    }

    switch (data->type()->code()) {
    case TypeCode::Function: {
        auto funcData = tt::as_shared<FunctionData>(data);
        auto it       = graphMap.find(&funcData->graph());
        if (it == graphMap.end()) {
            return data;
        }
        auto clonedFunc = FunctionData::create(*it->second);
        if (funcData->resolved()) {
            data_vec_t closure;
            closure.reserve(funcData->closure().size());
            for (const auto &elem : funcData->closure()) {
                closure.push_back(remapDataGraphRefs(elem, graphMap));
            }
            if (!closure.empty()) {
                clonedFunc->resolve(closure);
            }
        }
        return clonedFunc;
    }

    case TypeCode::Tuple: {
        auto tupleData = tt::as_shared<TupleData>(data);
        data_vec_t elems;
        elems.reserve(tupleData->raw().size());
        for (const auto &elem : tupleData->raw()) {
            elems.push_back(remapDataGraphRefs(elem, graphMap));
        }
        return TupleData::create(data->type(), std::move(elems));
    }

    case TypeCode::Array: {
        auto arrayData = tt::as_shared<ArrayData>(data);
        data_vec_t elems;
        elems.reserve(arrayData->raw().size());
        for (const auto &elem : arrayData->raw()) {
            elems.push_back(remapDataGraphRefs(elem, graphMap));
        }
        auto *arrayType = tt::as_ptr<ArrayType>(data->type());
        return ArrayData::from(arrayType->elemType(), elems);
    }

    case TypeCode::Struct: {
        auto structData = tt::as_shared<StructData>(data);
        std::map<std::string, data_ptr_t> fields;
        for (const auto &[name, value] : structData->raw()) {
            fields.emplace(name, remapDataGraphRefs(value, graphMap));
        }
        return StructData::create(std::move(fields));
    }

    default:
        return data;
    }
}

std::string makeGraphStableId(const std::string &name) {
    // DebugMap / debugger / runtime diagnostics all key graph origins by stableId.
    // So this id must remain invariant across rearrange(), bytecode lowering, and
    // other passes that may change graph contents or function type details.
    static std::atomic<uint64_t> nextId = 1;
    const uint64_t id                   = nextId++;
    return std::format("graph:{}#{}", name.empty() ? "<anonymous>" : name, id);
}

} // namespace

// =============================================================================
// Graph 创建与节点管理
// =============================================================================

std::string Graph::makeStableId(const std::string &name) { return makeGraphStableId(name); }

Graph::Graph(FunctionType *funcType, const graph_ptr_t &graph, const std::string &name)
    : name_(name), stableId_(makeStableId(name)), outer_(graph),
      arena_(graph ? graph->arena() : std::make_shared<GraphArena>(64 * 1024)) {
    signature_.funcType        = funcType;
    signature_.staticDataType  = TupleType::create();
    signature_.runtimeDataType = TupleType::create();
    signature_.closureType     = TupleType::create();
    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("GIR").debug(
            "Created Graph: {}",
            name_.empty() ? "<anonymous>" : name_));
}

Graph::~Graph() {
    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("GIR").debug(
            "Destroyed Graph: {}",
            name_.empty() ? "<anonymous>" : name_));
}

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

void GraphBuilder::assertBuildable(const char *action) const {
    ASSERT(graph_ != nullptr, "GraphBuilder has been consumed by sealGraph().");
    ASSERT(
        !graph_->finalized_,
        std::format("Cannot {} finalized graph '{}'. Clone it first.", action, graph_->name_));
}

void GraphBuilder::markMutated() const {
    // 只失效布局缓存，不改变 finalized 标记。
    // 在当前模型下，只有非 finalized 的图（初始构造或 draft clone）才会走到这里，
    // 因为 finalized 图的 mutation 会被 assertBuildable 拦截。
    graph_->setFrameMeta(nullptr);
}

Node *GraphBuilder::ownNode(node_uptr_t node) const {
    Node *raw = node.get();
    graph_->ownedNodes_.push_back(std::move(node));
    return raw;
}

data_idx_t GraphBuilder::addStaticData(const data_ptr_t &data) const {
    assertBuildable("append static data to");
    graph_->staticDataArr_.push_back(data);
    if (graph_->staticDataArr_.size() >
        static_cast<size_t>(std::numeric_limits<arr_size_t>::max())) {
        throw std::overflow_error("staticDataArr_ exceeds arr_size_t max value");
    }
    markMutated();
    return -static_cast<data_idx_t>(graph_->staticDataArr_.size() - 1);
}

data_idx_t GraphBuilder::addRuntimeData() const {
    if (graph_->signature_.runtimeDataSize >
        static_cast<size_t>(std::numeric_limits<arr_size_t>::max())) {
        throw std::overflow_error("runtimeDataSize_ exceeds arr_size_t max value");
    }
    markMutated();
    return static_cast<data_idx_t>(graph_->signature_.runtimeDataSize++);
}

void GraphBuilder::setStaticData(data_idx_t index, const data_ptr_t &data) const {
    assertBuildable("set static data on");
    ASSERT(index < 0, "Static data index must be negative.");
    size_t idx = static_cast<size_t>(-index);
    ASSERT(
        idx < graph_->staticDataArr_.size(),
        std::format(
            "Static data index out of range when setting data of graph ({}) at index {}. "
            "(total size: {})",
            graph_->name_,
            index,
            graph_->staticDataArr_.size()));
    graph_->staticDataArr_[idx] = data;
    markMutated();
}

void GraphBuilder::addNode(Node *node) const {
    assertBuildable("modify");
    graph_->nodes_.push_back(node);
    markMutated();
}

void GraphBuilder::eraseNode(Node *node) const {
    ASSERT(node != nullptr, "Cannot erase null node.");
    assertBuildable("modify");
    node->detach();
    graph_->nodes_.erase(
        std::remove(graph_->nodes_.begin(), graph_->nodes_.end(), node),
        graph_->nodes_.end());
    graph_->nodeStableIds_.erase(node);
    graph_->nodePortNames_.erase(node);
    graph_->nodeAccsKeys_.erase(node);
    markMutated();
}

void GraphBuilder::addPort(Node *node, bool isWith) const {
    assertBuildable("modify");
    if (isWith) {
        ASSERT(
            std::find(graph_->withPorts_.begin(), graph_->withPorts_.end(), node) ==
                graph_->withPorts_.end(),
            "With port node already exists in the graph.");
        graph_->withPorts_.push_back(node);
    } else {
        ASSERT(
            std::find(graph_->normPorts_.begin(), graph_->normPorts_.end(), node) ==
                graph_->normPorts_.end(),
            "Norm port node already exists in the graph.");
        graph_->normPorts_.push_back(node);
    }
    markMutated();
}

void GraphBuilder::addClosure(Node *node) const {
    assertBuildable("modify");
    ASSERT(
        std::find(graph_->closure_.begin(), graph_->closure_.end(), node) == graph_->closure_.end(),
        "Closure node already exists in the graph.");
    auto *portNode = tt::as_ptr<PortNode>(node);
    graph_->closure_.push_back(node);
    graph_->signature_.funcType->addClosureRef(portNode->name());
    markMutated();
}

void GraphBuilder::parametrizeClosure() const {
    assertBuildable("parameterize closure in");
    graph_->withPorts_.insert(
        graph_->withPorts_.begin(),
        graph_->closure_.begin(),
        graph_->closure_.end());
    graph_->closure_.clear();
    graph_->parameterized_ = true;
    // 不再立即 rearrange：slot 编号和 layout 统一在 finalize 时一次性导出。
    markMutated();
}

Node *Graph::exitNode() const {
    ASSERT(exitNode_ != nullptr, std::format("Graph {} has no exit node.", name_));
    return exitNode_;
}
Node *Graph::outputNode() const {
    ASSERT(exitNode_ != nullptr, std::format("Graph {} has no exit node.", name_));
    return exitNode_->normInputs().front();
}

void GraphBuilder::setOutput(Node *node) const {
    assertBuildable("set output on");
    ASSERT(
        graph_->exitNode_ == nullptr,
        std::format("Graph {} already has an output node.", graph_->name_));

    Type *actualExitType = node->dataType();
    if (graph_->signature_.funcType->hasExitType()) {
        Type *declaredExitType = graph_->signature_.funcType->exitType();
        if (!declaredExitType->assignableFrom(actualExitType)) {
            throw DiagnosticBuilder::of(SemanticDiag::ReturnTypeMismatch)
                .commit(
                    actualExitType->toString(),
                    declaredExitType->toString(),
                    graph_->name_ + ": " + graph_->signature_.funcType->toString());
        }
    } else {
        graph_->signature_.funcType->setExitType(actualExitType);
    }

    graph_->exitNode_ = ExitNode::create(*graph_, node->dataType(), node->index());
    Node::link(LinkType::Norm, node, graph_->exitNode_);
    markMutated();
}

// =============================================================================
// Graph 查询与数据段
// =============================================================================

const std::string &Graph::nodeStableId(const Node *node) const {
    auto it = nodeStableIds_.find(node);
    ASSERT(it != nodeStableIds_.end(), "Node stableId not found in Graph's centralized storage.");
    return it->second;
}

const std::string &Graph::nodePortName(const Node *node) const {
    auto it = nodePortNames_.find(node);
    ASSERT(it != nodePortNames_.end(), "Port name not found in Graph's centralized storage.");
    return it->second;
}

const std::string &Graph::nodeAccsKey(const Node *node) const {
    auto it = nodeAccsKeys_.find(node);
    ASSERT(
        it != nodeAccsKeys_.end(),
        "AccsNode string key not found in Graph's centralized storage.");
    return it->second;
}

OperatorIndex *Graph::registerOperIndex(std::shared_ptr<OperatorIndex> idx) {
    auto *raw               = idx.get();
    operIndexRegistry_[raw] = std::move(idx);
    return raw;
}

std::shared_ptr<OperatorIndex> Graph::lookupOperIndex(const OperatorIndex *raw) const {
    auto it = operIndexRegistry_.find(raw);
    ASSERT(it != operIndexRegistry_.end(), "OperatorIndex not registered in Graph.");
    return it->second;
}

FunctionData *Graph::registerFuncData(func_ptr_t fd) {
    auto *raw              = fd.get();
    funcDataRegistry_[raw] = std::move(fd);
    return raw;
}

func_ptr_t Graph::lookupFuncData(const FunctionData *raw) const {
    auto it = funcDataRegistry_.find(raw);
    ASSERT(it != funcDataRegistry_.end(), "FunctionData not registered in Graph.");
    return it->second;
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

camel::core::context::FrameMeta *Graph::frameMeta() const {
    return getExtra<camel::core::context::FrameMeta, kFrameMetaExtraIndex_>();
}

data_ptr_t Graph::materializeStaticData(data_idx_t index) const {
    ASSERT(index < 0, "Static data index must be negative.");
    const size_t idx = static_cast<size_t>(-index);
    ASSERT(
        idx < staticDataArr_.size(),
        std::format(
            "Static data index out of range when materializing graph ({}) at index {}. "
            "(total size: {})",
            name_,
            index,
            staticDataArr_.size()));

    // 编译期 staticDataArr_ 是权威来源：它保存原始 data_ptr_t，
    // 支持无损 clone/remap（例如 lambda lowering 中的引用占位符）。
    // 仅当编译期条目为空时，才回退到 FrameMeta::staticArea 的 runtime slot 视图。
    if (staticDataArr_[idx] != nullptr) {
        return staticDataArr_[idx];
    }

    if (auto *meta = frameMeta(); meta != nullptr) {
        ASSERT(
            idx < meta->staticArea->size(),
            std::format(
                "Static data index out of range when materializing graph ({}) at index {}. "
                "(total size: {})",
                name_,
                index,
                meta->staticArea->size()));
        return materializeStaticSlot(
            meta->staticArea->get<slot_t>(idx),
            signature_.staticDataType->typeAt(idx));
    }
    return getStaticData(index);
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

std::optional<std::unordered_set<graph_ptr_t>>
Graph::getSubGraphsByName(const std::string &name) const {
    auto it = subGraphs_.find(name);
    if (it != subGraphs_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void GraphBuilder::addSubGraph(const graph_ptr_t &graph) const {
    assertBuildable("add subgraph to");
    ASSERT(graph.get() != graph_, "Cannot add itself as a subgraph.");
    ASSERT(!graph->name().empty(), "Cannot add an anonymous graph as a subgraph.");
    if (graph_->subGraphs_.find(graph->name()) == graph_->subGraphs_.end()) {
        graph_->subGraphs_[graph->name()] = std::unordered_set<graph_ptr_t>({graph});
    } else {
        auto &existing = graph_->subGraphs_[graph->name()];
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
    if (graph_->subGraphs_.find(graph->name()) != graph_->subGraphs_.end()) {
        auto &existing = graph_->subGraphs_[graph->name()];
        existing.erase(graph);
        GetDefaultLogger().in("GIR").debug(
            "Removed subgraph '{}' from graph '{}'.",
            graph->mangledName(),
            graph_->name_);
        if (existing.empty()) {
            graph_->subGraphs_.erase(graph->name());
        }
        graph->outer_.reset();
    }
    markMutated();
}

void GraphBuilder::addDependency(const graph_ptr_t &graph) const {
    assertBuildable("add dependency to");
    if (graph.get() == graph_) {
        graph_->looped_ = true;
        return;
    }
    graph_->dependencies_.insert(graph);
    graph->dependents_.insert(graph_->shared_from_this());
    GetDefaultLogger().in("GIR").debug(
        "Added dependency: Graph '{}' depends on graph '{}'.",
        graph_->name_,
        graph->name());
    markMutated();
}

void GraphBuilder::eraseDependency(const graph_ptr_t &graph) const {
    assertBuildable("remove dependency from");
    graph_->dependencies_.erase(graph);
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
    std::unordered_map<const Graph *, graph_ptr_t> graphMap;
    std::function<graph_ptr_t(const Graph *, const graph_ptr_t &)> cloneGraph =
        [&](const Graph *src, const graph_ptr_t &newOuter) -> graph_ptr_t {
        if (auto it = graphMap.find(src); it != graphMap.end()) {
            return it->second;
        }

        graph_ptr_t newGraph = std::make_shared<Graph>(src->funcType(), newOuter, src->name_);
        if (newOuter) {
            GraphBuilder(newOuter).addSubGraph(newGraph);
        }
        graphMap[src]                        = newGraph;
        newGraph->looped_                    = src->looped_;
        newGraph->parameterized_             = src->parameterized_;
        newGraph->signature_.funcType        = src->signature_.funcType;
        newGraph->staticDataArr_             = src->staticDataArr_;
        newGraph->signature_.runtimeDataSize = src->signature_.runtimeDataSize;
        newGraph->signature_.staticDataType  = src->signature_.staticDataType;
        newGraph->signature_.runtimeDataType = src->signature_.runtimeDataType;
        newGraph->signature_.closureType     = src->signature_.closureType;
        // clone 出的图默认 finalized_ = false，可直接编辑，无需显式 unfrozen。
        newGraph->setFrameMeta(nullptr);

        if (auto *sourceContext =
                src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
            newGraph->setExtra<camel::source::SourceContext, kSourceContextExtraIndex>(
                sourceContext);
            sourceContext->cloneGirGraphDebugInfo(src->stableId(), newGraph->stableId());
        }

        std::unordered_map<Node *, Node *> nodeMap;
        for (Node *port : src->withPorts_) {
            Node *newPort = port->clone(*newGraph);
            nodeMap[port] = newPort;
            newGraph->withPorts_.push_back(newPort);
            if (auto *sourceContext =
                    src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
                sourceContext->cloneGirNodeDebugInfo(port->stableId(), newPort->stableId());
            }
        }
        for (Node *port : src->normPorts_) {
            Node *newPort = port->clone(*newGraph);
            nodeMap[port] = newPort;
            newGraph->normPorts_.push_back(newPort);
            if (auto *sourceContext =
                    src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
                sourceContext->cloneGirNodeDebugInfo(port->stableId(), newPort->stableId());
            }
        }
        for (Node *closureNode : src->closure_) {
            Node *newClosureNode = closureNode->clone(*newGraph);
            nodeMap[closureNode] = newClosureNode;
            newGraph->closure_.push_back(newClosureNode);
            if (auto *sourceContext =
                    src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
                sourceContext->cloneGirNodeDebugInfo(
                    closureNode->stableId(),
                    newClosureNode->stableId());
            }
        }
        for (Node *node : src->nodes_) {
            Node *newNode = node->clone(*newGraph);
            nodeMap[node] = newNode;
            if (auto *sourceContext =
                    src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
                sourceContext->cloneGirNodeDebugInfo(node->stableId(), newNode->stableId());
            }
        }

        ASSERT(src->exitNode_ != nullptr, "Cloning a graph without output node.");
        Node *outputNode = src->exitNode_->normInputs().front();
        Node *newOutput  = requireMappedNode(
            outputNode,
            nodeMap,
            "Output node not found in node map during graph cloning.");
        Node *newExitNode = ExitNode::create(*newGraph, newOutput->dataType(), newOutput->index());
        nodeMap[src->exitNode_] = newExitNode;
        overwriteFreshNodeAdjacencyPreservingOrder(src->withPorts_, nodeMap);
        overwriteFreshNodeAdjacencyPreservingOrder(src->normPorts_, nodeMap);
        overwriteFreshNodeAdjacencyPreservingOrder(src->closure_, nodeMap);
        overwriteFreshNodeAdjacencyPreservingOrder(src->nodes_, nodeMap);
        overwriteFreshNodeAdjacencyPreservingOrder({src->exitNode_}, nodeMap);
        newGraph->exitNode_ = newExitNode;
        if (auto *sourceContext =
                src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
            sourceContext->cloneGirNodeDebugInfo(
                src->exitNode_->stableId(),
                newExitNode->stableId());
        }

        for (const auto &[_, subGraphs] : src->subGraphs_) {
            for (const auto &subGraph : subGraphs) {
                cloneGraph(subGraph.get(), newGraph);
            }
        }
        for (const auto &dep : src->dependencies_) {
            cloneGraph(dep.get(), nullptr);
        }

        return newGraph;
    };

    auto cloned = cloneGraph(graph.get(), nullptr);
    for (const auto &[srcGraph, newGraph] : graphMap) {
        (void)srcGraph;
        for (auto &staticData : newGraph->staticDataArr_) {
            staticData = remapDataGraphRefs(staticData, graphMap);
        }
        for (Node *node : newGraph->nodes_) {
            if (node->type() != NodeType::FUNC) {
                continue;
            }
            auto *funcNode = tt::as_ptr<FuncNode>(node);
            funcNode->setFunc(
                *newGraph,
                tt::as_shared<FunctionData>(remapDataGraphRefs(funcNode->funcShared(), graphMap)));
        }
    }
    for (const auto &[srcGraph, newGraph] : graphMap) {
        for (const auto &dep : srcGraph->dependencies_) {
            GraphBuilder(newGraph).addDependency(graphMap.at(dep.get()));
        }
    }
    return cloned;
}

void GraphBuilder::finalize() const {
    if (graph_->finalized_) {
        return;
    }
    rearrange();
    camel::core::context::installFrameMetaInfoForGraph(graph_);
    graph_->finalized_ = true;
}

void GraphBuilder::sealGraph() {
    finalize();

    // --- Freeze 阶段：将所有节点的邻接 vectors 搬迁到 arena ---
    // finalize 后节点布局已确定（rearrange + FrameMeta 已安装），
    // 此时将 draft vectors 的数据复制到 arena 定长数组，然后释放 vectors。
    auto &arena = *graph_->arena_;
    for (auto &owned : graph_->ownedNodes_) {
        if (owned && !owned->isFrozen()) {
            owned->freezeAdjacency(arena);
        }
    }

    // consume 语义：seal 后 builder 不可再使用
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
        for (const auto &[_, subGraphs] : curr->subGraphs_) {
            for (const auto &subGraph : subGraphs) {
                sealDfs(subGraph.get());
            }
        }
        for (const auto &dep : curr->dependencies_) {
            sealDfs(dep.get());
        }
    };
    sealDfs(graph.get());
}

camel::core::context::FrameMeta *GraphBuilder::ensureFrameMeta() const {
    if (auto *meta = graph_->frameMeta()) {
        return meta;
    }
    finalize();
    return graph_->frameMeta();
}

Node *GraphBuilder::inlineNode(Node *node, bool forceSync) const {
    assertBuildable("inline into");
    if (node->graph() != *graph_) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("GIR").debug(
                "Cannot inline node {} from different graph {} into graph {}.",
                node->toString(),
                node->graph().name(),
                graph_->name_));
        return nullptr;
    }

    if (node->type() != NodeType::FUNC) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("GIR").debug(
                "Cannot inline non-FUNC node {} in graph {}.",
                node->toString(),
                graph_->name_));
        return nullptr;
    }

    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("GIR").debug(
            "Inlining node {} in graph {}.",
            node->toString(),
            graph_->name_));

    auto *funcNode    = tt::as_ptr<FuncNode>(node);
    auto &targetGraph = funcNode->func()->graph();
    auto *sourceContext =
        graph_->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>();

    Node *syncNode = SyncNode::create(*graph_);
    std::unordered_map<Node *, Node *> nodeMap;

    const auto &normPorts  = targetGraph.normPorts();
    const auto &normInputs = node->normInputs();
    ASSERT(
        normPorts.size() == normInputs.size(),
        "Number of norm ports and norm inputs do not match for inlining.");
    for (size_t i = 0; i < normPorts.size(); ++i) {
        if (forceSync) {
            Node *nrefNode = NRefNode::create(*graph_);
            Node::link(LinkType::Ctrl, syncNode, nrefNode);
            Node::link(LinkType::Norm, normInputs[i], nrefNode);
            nodeMap[normPorts[i]] = nrefNode;
        } else {
            nodeMap[normPorts[i]] = normInputs[i];
        }
    }

    const auto &withPorts  = targetGraph.withPorts();
    const auto &withInputs = node->withInputs();
    ASSERT(
        withPorts.size() == withInputs.size(),
        "Number of with ports and with inputs do not match for inlining.");
    for (size_t i = 0; i < withPorts.size(); ++i) {
        if (forceSync) {
            Node *nrefNode = NRefNode::create(*graph_);
            Node::link(LinkType::Ctrl, syncNode, nrefNode);
            Node::link(LinkType::Norm, withInputs[i], nrefNode);
            nodeMap[withPorts[i]] = nrefNode;
        } else {
            nodeMap[withPorts[i]] = withInputs[i];
        }
    }

    ASSERT(targetGraph.closure().empty(), "Cannot inline a graph with closure.");

    for (Node *n : targetGraph.nodes()) {
        Node *clonedNode = n->clone(*graph_);
        nodeMap[n]       = clonedNode;
        if (sourceContext) {
            sourceContext->cloneGirNodeDebugInfo(n->stableId(), clonedNode->stableId());
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
    for (Node *oldNode : targetGraph.nodes()) {
        if (forceSync && oldNode->inDegree() == 0) {
            Node *newNode = requireMappedNode(
                oldNode,
                nodeMap,
                "Mapped node not found when wiring sync during inlining.");
            Node::link(LinkType::Ctrl, syncNode, newNode);
        }
    }

    Node *targetOutput = targetGraph.exitNode()->normInputs().front();
    Node *inlinedOutput =
        requireMappedNode(targetOutput, nodeMap, "Target output node not found during inlining.");

    for (auto *out : node->normOutputs()) {
        Node::link(LinkType::Norm, inlinedOutput, out);
    }
    for (auto *out : node->withOutputs()) {
        Node::link(LinkType::With, inlinedOutput, out);
    }
    for (auto *out : node->ctrlOutputs()) {
        Node::link(LinkType::Ctrl, inlinedOutput, out);
    }

    markMutated();
    return syncNode;
}

LayoutResult GraphBuilder::computeLayout(const Graph &graph) {
    LayoutResult result;
    data_idx_t stcIdx = -1, rtmIdx = 1;
    result.staticDataArr = {Data::null()};
    type_vec_t staticDataTypes{Type::Void()}, runtimeDataTypes{Type::Void()}, closureTypes;

    // 用于 exitNode 查找其 input 的新 index
    std::unordered_map<Node *, data_idx_t> indexMap;

    auto assignIndex = [&](Node *node, data_idx_t idx) {
        result.nodeIndices.emplace_back(node, idx);
        indexMap[node] = idx;
    };

    for (Node *node : graph.normPorts_) {
        assignIndex(node, rtmIdx++);
        runtimeDataTypes.push_back(node->dataType());
    }
    for (Node *node : graph.withPorts_) {
        assignIndex(node, rtmIdx++);
        runtimeDataTypes.push_back(node->dataType());
    }
    for (Node *node : graph.closure_) {
        assignIndex(node, rtmIdx++);
        runtimeDataTypes.push_back(node->dataType());
        closureTypes.push_back(node->dataType());
    }
    for (Node *node : graph.nodes_) {
        NodeType type = node->type();
        ASSERT(type != NodeType::DREF, "DREF nodes should not exist in finalized graph.");
        if (type == NodeType::DATA) {
            auto *dataNode = tt::as_ptr<DataNode>(node);
            result.staticDataArr.push_back(dataNode->data());
            assignIndex(dataNode, stcIdx--);
            staticDataTypes.push_back(dataNode->dataType());
        } else {
            if (type == NodeType::SYNC || type == NodeType::NREF) {
                continue;
            }
            assignIndex(node, rtmIdx++);
            runtimeDataTypes.push_back(node->dataType());
        }
    }

    if (graph.exitNode_) {
        Node *exitInput    = graph.exitNode_->normInputs().front();
        auto it            = indexMap.find(exitInput);
        data_idx_t exitIdx = (it != indexMap.end()) ? it->second : exitInput->index();
        assignIndex(graph.exitNode_, exitIdx);
    }

    result.runtimeDataSize = rtmIdx;
    result.staticDataType  = TupleType::create(std::move(staticDataTypes));
    result.runtimeDataType = TupleType::create(std::move(runtimeDataTypes));
    result.closureType     = TupleType::create(std::move(closureTypes));
    return result;
}

void GraphBuilder::applyLayout(Graph &graph, const LayoutResult &layout) {
    for (auto [node, idx] : layout.nodeIndices) {
        node->setIndex(idx);
    }
    graph.signature_.runtimeDataSize = layout.runtimeDataSize;
    graph.staticDataArr_             = layout.staticDataArr;
    graph.signature_.staticDataType  = layout.staticDataType;
    graph.signature_.runtimeDataType = layout.runtimeDataType;
    graph.signature_.closureType     = layout.closureType;
    graph.setFrameMeta(nullptr);
}

void GraphBuilder::rearrange() const {
    GetDefaultLogger().in("GIR").debug("Rearranging graph {}.", graph_->name_);
    auto layout = computeLayout(*graph_);
    applyLayout(*graph_, layout);
}

} // namespace camel::compile::gir
