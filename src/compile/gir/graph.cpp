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
 * Updated: Apr. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/compile/gir.h"
#include "camel/compile/gir/static_function.h"
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
#include "runtime/graph_build.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <functional>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

using namespace camel::core::error;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

namespace {

using namespace camel::core::rtdata;
constexpr std::size_t kSourceContextExtraIndex = 3;

uint64_t debugHashBytes(const void *data, size_t n) {
    uint64_t h    = 14695981039346656037ULL;
    const auto *p = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t debugHashString(std::string_view s) { return debugHashBytes(s.data(), s.size()); }

uint64_t debugMix64(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

void debugHashSortedInputIndices(const Node *n, uint64_t laneTag, uint64_t &w0, uint64_t &w1) {
    std::vector<int32_t> idx;
    auto nodeIndexForHash = [](const Node *x) -> int32_t {
        if (!x) {
            return 0;
        }
        if (x->type() == NodeType::SYNC || x->type() == NodeType::DREF) {
            return 0;
        }
        return static_cast<int32_t>(x->index());
    };
    auto pushSpan = [&](node_span_t sp) {
        for (Node *x : sp) {
            idx.push_back(nodeIndexForHash(x));
        }
    };
    pushSpan(n->normInputs());
    pushSpan(n->withInputs());
    pushSpan(n->ctrlInputs());
    pushSpan(n->normOutputs());
    pushSpan(n->withOutputs());
    pushSpan(n->ctrlOutputs());
    std::sort(idx.begin(), idx.end());
    w0 = debugMix64(w0, laneTag);
    for (int32_t v : idx) {
        w0 = debugMix64(w0, static_cast<uint64_t>(static_cast<uint32_t>(v)));
        w1 = debugMix64(w1, debugMix64(static_cast<uint64_t>(v), laneTag));
    }
}

} // namespace

// Must stay out of the anonymous namespace: Graph::makeStableId is defined
// outside the block and must remain visible in this translation unit.
static std::string makeGraphStableId(const std::string &name) {
    // DebugMap / debugger / runtime diagnostics all key graph origins by stableId.
    // So this id must remain invariant across rearrange(), bytecode lowering, and
    // other passes that may change graph contents or function type details.
    static std::atomic<uint64_t> nextId = 1;
    const uint64_t id                   = nextId++;
    return std::format("graph:{}#{}", name.empty() ? "<anonymous>" : name, id);
}

// =============================================================================
// Graph creation and node management.
// =============================================================================

std::string Graph::makeStableId(const std::string &name) { return makeGraphStableId(name); }

namespace {
constexpr size_t kDefaultGraphArenaBytes = 256 * 1024;
}

Graph::Graph(FunctionType *funcType, const graph_ptr_t &graph, const std::string &name)
    : name_(name), stableId_(makeStableId(name)), outer_(graph),
      arena_(std::make_shared<GraphArena>(kDefaultGraphArenaBytes)) {
    signature_.funcType        = funcType;
    signature_.staticDataType  = TupleType::create();
    signature_.runtimeDataType = TupleType::create();
    signature_.closureType     = TupleType::create();
    EXEC_WHEN_DEBUG(
        CAMEL_LOG_DEBUG_S("GIR", "Created Graph: {}", name_.empty() ? "<anonymous>" : name_));
}

Graph::~Graph() {
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "GIR",
        "Destroying Graph at {:p} (name='{}').",
        static_cast<const void *>(this),
        name_.empty() ? "<anonymous>" : name_));
}

// -----------------------------------------------------------------------------
// Two phases of node debug entity IDs.
//
// 1) Construction time: only a placeholder draft:{seq} string exists so
//    Node::debugEntityId() can be queried at any time; the SourceContext draft
//    binding (bindGirNodeDraftDebug) is keyed by Node* and does not rely on
//    this string.
// 2) Once layout is refreshed for runtime encoding, indices are fixed and we
//    compute a content-addressed fingerprint stored in nodeStableIds_ (like
//    gnode:{032x}); the SourceContext binding is promoted to that stable ID.
// -----------------------------------------------------------------------------

void Graph::installProvisionalNodeStableId(Graph &graph, const Node *node) {
    // During the draft phase we only need a temporary in-graph entity ID; it
    // will be promoted to a gnode: fingerprint once layout is stabilized.
    const uint64_t seq = graph.provisionalDebugIdSeed_++;
    graph.nodeStableIds_[node] =
        std::format("draft:{}:{}", graph.stableId_, static_cast<unsigned long long>(seq));
}

NodeDebugFingerprint
Graph::computeNodeDebugFingerprintForNode(Node *node, uint64_t tieBreaker) const {
    // Split the 128-bit fingerprint into word0/word1; mix with an FNV-style
    // hash so the fingerprint stays stable for the same graph, layout, and
    // structure, independent of hash-table traversal order.
    uint64_t w0          = debugHashString(stableId_);
    uint64_t w1          = debugMix64(0, static_cast<uint64_t>(static_cast<int>(node->type())));
    data_idx_t stableIdx = 0;
    if (node->type() != NodeType::SYNC && node->type() != NodeType::DREF) {
        stableIdx = node->index();
    }
    w1 = debugMix64(
        w1,
        static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(stableIdx))));
    // Flatten and sort all neighboring runtime/static slots on LinkType edges
    // before mixing them in, so the fingerprint captures who is connected.
    debugHashSortedInputIndices(node, 0xA5A5A5A5A5A5A5A5ULL, w0, w1);

    switch (node->type()) {
    case NodeType::PORT:
        // Ports with the same name in different graphs must still hash differently.
        w0 = debugMix64(w0, debugHashString(nodePortName(node)));
        break;
    case NodeType::ACCS: {
        // Numeric indices have no Graph-side string; only string keys participate in hashing.
        auto it = nodeAccsKeys_.find(node);
        if (it != nodeAccsKeys_.end()) {
            w0 = debugMix64(w0, debugHashString(it->second));
        }
        break;
    }
    case NodeType::OPER:
        // Use the operator identity to distinguish different OPER nodes.
        w0 = debugMix64(
            w0,
            reinterpret_cast<uint64_t>(
                reinterpret_cast<uintptr_t>(tt::as_ptr<OperNode>(node)->oper())));
        break;
    case NodeType::FUNC:
        // Use the subgraph stableId to distinguish FUNC nodes pointing at different bodies.
        w0 = debugMix64(w0, debugHashString(tt::as_ptr<FuncNode>(node)->bodyGraph()->stableId()));
        break;
    case NodeType::DATA: {
        // Static slot content identity, tied to constants, function values, and similar bindings.
        slot_t sl = tt::as_ptr<DataNode>(node)->dataSlot();
        w0        = debugMix64(w0, debugHashBytes(&sl, sizeof(sl)));
        break;
    }
    case NodeType::SYNC:
    case NodeType::GATE:
        // These nodes often have no standalone dataIndex semantics in the layout,
        // so use the address to break symmetry and avoid accidental merging.
        w0 = debugMix64(
            w0,
            reinterpret_cast<uint64_t>(reinterpret_cast<uintptr_t>(const_cast<Node *>(node))));
        break;
    default:
        break;
    }

    // Increment in a fixed traversal order within the graph to further reduce
    // the chance of accidental 128-bit collisions between nodes.
    w1 = debugMix64(w1, tieBreaker);
    return {w0, w1};
}

void Graph::promoteNodeDebugIds(camel::source::SourceContext *sourceContext) {
    uint64_t seq = 0;
    std::unordered_set<Node *> promoted;
    auto promoteOne = [&](Node *n) {
        if (n == nullptr) {
            return;
        }
        if (!promoted.insert(n).second) {
            return;
        }
        NodeDebugFingerprint fp = computeNodeDebugFingerprintForNode(n, seq++);
        std::string entityId    = fp.toEntityId();
        nodeStableIds_[n]       = std::move(entityId);
        if (sourceContext == nullptr) {
            return;
        }
        // Only nodes that were registered with a draft binding during the GCT
        // phase will be written into the DebugMap / semantic table here.
        const std::string &id = nodeStableIds_[n];
        sourceContext->sealPromoteGirNodeDebug(n, id);
    };

    // The order matches the rough hierarchy used by computeLayout for ports,
    // closures, and body nodes so the tie breaker stays reproducible.
    for (Node *n : normPorts_) {
        promoteOne(n);
    }
    for (Node *n : withPorts_) {
        promoteOne(n);
    }
    for (Node *n : closure_) {
        promoteOne(n);
    }
    for (Node *n : nodes_) {
        promoteOne(n);
    }
    promoteOne(exitNode_);
}

Node *Graph::exitNode() const {
    ASSERT(exitNode_ != nullptr, std::format("Graph {} has no output node.", name_));
    return exitNode_;
}
Node *Graph::outputNode() const { return exitNode(); }

// =============================================================================
// Graph queries and data segments
// =============================================================================

const std::string &Graph::nodeDebugEntityId(const Node *node) const {
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

graph_ptr_t
Graph::create(FunctionType *funcType, const graph_ptr_t &outer, const std::string &name) {
    ASSERT(funcType->hasMetaInfo(), "Trying to create a Graph with incomplete FunctionType.");
    static int anonymousIdx     = 0;
    const std::string graphName = name.empty() ? std::format("__{}__", anonymousIdx++) : name;
    auto graph                  = std::shared_ptr<Graph>(new Graph(funcType, outer, graphName));
    if (outer) {
        outer->addSubGraph(graph);
    }
    const size_t withCount = funcType->withTypesCount();
    for (size_t i = 0; i < funcType->normTypesCount(); ++i) {
        Node *portNode = PortNode::create(
            *graph,
            funcType->normTypeAt(i),
            std::string(funcType->argNameAt(withCount + i)),
            funcType->normIsVarAt(i));
        graph->addPort(portNode, false);
    }
    for (size_t i = 0; i < withCount; ++i) {
        Node *portNode = PortNode::create(
            *graph,
            funcType->withTypeAt(i),
            std::string(funcType->argNameAt(i)),
            funcType->withIsVarAt(i));
        graph->addPort(portNode, true);
    }
    return graph;
}

void Graph::markMutated() {
    hasPackedStaticData_  = false;
    packedStaticData_     = nullptr;
    packedStaticDataSize_ = 0;
    frameSize_            = 0;
    staticArea_           = nullptr;
}

Node *Graph::ownNode(Node *node) {
    ASSERT(node != nullptr, "Cannot own null node.");
    ownedNodes_.push_back(node);
    return node;
}

data_idx_t Graph::addStaticSlot(slot_t slot) {
    staticDataArr_.push_back(slot);
    if (staticDataArr_.size() > static_cast<size_t>(std::numeric_limits<arr_size_t>::max())) {
        throw std::overflow_error("staticDataArr_ exceeds arr_size_t max value");
    }
    markMutated();
    return -static_cast<data_idx_t>(staticDataArr_.size() - 1);
}

data_idx_t Graph::addStaticData(const data_ptr_t &data) {
    ASSERT(data != nullptr, "Static data cannot be null.");
    slot_t slot = NullSlot;
    if (data->type()->isGCTraced()) {
        Object *obj = makeGCRefFromGCTracedData(data, arena_->allocator());
        slot        = toSlot<Object *>(obj);
    } else if (data->type()->isPrimitive()) {
        slot = makeSlotFromPrimitiveData(data);
    } else {
        ASSERT(
            false,
            std::format(
                "Unsupported static data type '{}' for slot conversion.",
                data->type()->toString()));
    }
    return addStaticSlot(slot);
}

data_idx_t Graph::addRuntimeData() {
    if (signature_.runtimeDataSize > static_cast<size_t>(std::numeric_limits<arr_size_t>::max())) {
        throw std::overflow_error("runtimeDataSize_ exceeds arr_size_t max value");
    }
    markMutated();
    return static_cast<data_idx_t>(signature_.runtimeDataSize++);
}

void Graph::setStaticSlot(data_idx_t index, slot_t slot) {
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
    staticDataArr_[idx] = slot;
    markMutated();
}

void Graph::setStaticData(data_idx_t index, const data_ptr_t &data) {
    ASSERT(data != nullptr, "Static data cannot be null.");
    slot_t slot = NullSlot;
    if (data->type()->isGCTraced()) {
        Object *obj = makeGCRefFromGCTracedData(data, arena_->allocator());
        slot        = toSlot<Object *>(obj);
    } else if (data->type()->isPrimitive()) {
        slot = makeSlotFromPrimitiveData(data);
    } else {
        ASSERT(
            false,
            std::format(
                "Unsupported static data type '{}' for slot conversion.",
                data->type()->toString()));
    }
    setStaticSlot(index, slot);
}

void Graph::addNode(Node *node) {
    nodes_.push_back(node);
    markMutated();
}

void Graph::eraseNode(Node *node) {
    ASSERT(node != nullptr, "Cannot erase null node.");
    if (auto *sc = getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
        sc->unbindGirNodeDraftDebug(node);
    }
    node->detach();
    nodes_.erase(std::remove(nodes_.begin(), nodes_.end(), node), nodes_.end());
    nodeStableIds_.erase(node);
    nodePortNames_.erase(node);
    nodeAccsKeys_.erase(node);
    markMutated();
}

void Graph::addPort(Node *node, bool isWith) {
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
    markMutated();
}

void Graph::addClosure(Node *node) {
    ASSERT(
        std::find(closure_.begin(), closure_.end(), node) == closure_.end(),
        "Closure node already exists in the graph.");
    auto *portNode = tt::as_ptr<PortNode>(node);
    closure_.push_back(node);
    signature_.funcType->addClosureRef(portNode->name());
    markMutated();
}

void Graph::parametrizeClosure() {
    withPorts_.insert(withPorts_.begin(), closure_.begin(), closure_.end());
    closure_.clear();
    parameterized_ = true;
    markMutated();
}

void Graph::setOutput(Node *node) {
    ASSERT(exitNode_ == nullptr, std::format("Graph {} already has an output node.", name_));
    Type *actualExitType = node->dataType();
    if (signature_.funcType->hasExitType()) {
        Type *declaredExitType = signature_.funcType->exitType();
        if (!declaredExitType->assignableFrom(actualExitType)) {
            throw DiagnosticBuilder::of(SemanticDiag::ReturnTypeMismatch)
                .commit(
                    actualExitType->toString(),
                    declaredExitType->toString(),
                    name_ + ": " + signature_.funcType->toString());
        }
    } else {
        signature_.funcType->setExitType(actualExitType);
    }
    exitNode_ = node;
    markMutated();
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
    }
    try {
        graph->outer_ = shared_from_this();
    } catch (const std::bad_weak_ptr &) {
        throw std::runtime_error(
            std::format("Graph::addSubGraph owner '{}' is not managed by shared_ptr.", name_));
    }
    markMutated();
}

void Graph::eraseSubGraph(const graph_ptr_t &graph) {
    ASSERT(graph.get() != this, "Cannot remove itself as a subgraph.");
    ASSERT(!graph->name().empty(), "Cannot remove an anonymous graph as a subgraph.");
    if (subGraphs_.find(graph->name()) != subGraphs_.end()) {
        auto &existing = subGraphs_[graph->name()];
        existing.erase(graph);
        if (existing.empty()) {
            subGraphs_.erase(graph->name());
        }
        if (const auto currentOuter = graph->outer(); currentOuter && currentOuter.get() == this) {
            graph->outer_.reset();
        }
    }
    markMutated();
}

void Graph::addDependency(const graph_ptr_t &graph) {
    if (graph.get() == this) {
        looped_ = true;
        return;
    }
    dependencies_.insert(graph);
    try {
        graph->dependents_.insert(shared_from_this());
    } catch (const std::bad_weak_ptr &) {
        throw std::runtime_error(
            std::format("Graph::addDependency owner '{}' is not managed by shared_ptr.", name_));
    }
    markMutated();
}

void Graph::eraseDependency(const graph_ptr_t &graph) {
    dependencies_.erase(graph);
    try {
        graph->dependents_.erase(shared_from_this());
    } catch (const std::bad_weak_ptr &) {
        throw std::runtime_error(
            std::format("Graph::eraseDependency owner '{}' is not managed by shared_ptr.", name_));
    }
    markMutated();
}

void Graph::touch() { markMutated(); }

namespace {

struct GraphLayoutSnapshot {
    std::vector<std::pair<Node *, data_idx_t>> nodeIndices;
    static_slot_vec_t staticDataArr = {NullSlot};
    TupleType *staticDataType       = nullptr;
    TupleType *runtimeDataType      = nullptr;
    TupleType *closureType          = nullptr;
    size_t runtimeDataSize          = 1;
};

GraphLayoutSnapshot computeGraphLayoutSnapshot(const Graph &graph) {
    GraphLayoutSnapshot result;
    data_idx_t stcIdx = -1;
    data_idx_t rtmIdx = 1;
    type_vec_t staticDataTypes{Type::Void()};
    type_vec_t runtimeDataTypes{Type::Void()};
    type_vec_t closureTypes;

    auto assignIndex = [&](Node *node, data_idx_t idx) {
        result.nodeIndices.emplace_back(node, idx);
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
        const NodeType type = node->type();
        ASSERT(
            type != NodeType::DREF,
            "DREF nodes should not exist in an encodable compile graph.");
        if (type == NodeType::DATA) {
            auto *dataNode = tt::as_ptr<DataNode>(node);
            result.staticDataArr.push_back(dataNode->dataSlot());
            assignIndex(dataNode, stcIdx--);
            staticDataTypes.push_back(dataNode->dataType());
            continue;
        }
        if (type == NodeType::SYNC || type == NodeType::GATE) {
            continue;
        }
        assignIndex(node, rtmIdx++);
        runtimeDataTypes.push_back(node->dataType());
    }

    result.runtimeDataSize = rtmIdx;
    result.staticDataType  = TupleType::create(std::move(staticDataTypes));
    result.runtimeDataType = TupleType::create(std::move(runtimeDataTypes));
    result.closureType     = TupleType::create(std::move(closureTypes));
    return result;
}

} // namespace

void Graph::refreshDerivedLayout() {
    const auto layout = computeGraphLayoutSnapshot(*this);
    for (auto [node, idx] : layout.nodeIndices) {
        detail::NodeMutation::setIndex(node, idx);
    }
    signature_.runtimeDataSize = layout.runtimeDataSize;
    staticDataArr_             = layout.staticDataArr;
    signature_.staticDataType  = layout.staticDataType;
    signature_.runtimeDataType = layout.runtimeDataType;
    signature_.closureType     = layout.closureType;
    frameSize_                 = 0;
    staticArea_                = nullptr;
    packStaticSlotsToFrozen();
    installFinalFrameLayout();
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
        nodes().size(),
        subGraphs().size(),
        dependencies().size(),
        dependents_.size());
}

void Graph::packStaticSlotsToFrozen() {
    if (hasPackedStaticData_) {
        return;
    }
    packedStaticDataSize_ = staticDataArr_.size();
    if (packedStaticDataSize_ == 0) {
        return;
    }
    packedStaticData_ = static_cast<slot_t *>(
        arena_->allocFrozen(sizeof(slot_t) * packedStaticDataSize_, alignof(slot_t)));
    std::memcpy(packedStaticData_, staticDataArr_.data(), sizeof(slot_t) * packedStaticDataSize_);
    hasPackedStaticData_ = true;
    // Release build-time container capacity after layout export; runtime uses
    // packedStaticData_ instead.
    staticDataArr_.clear();
    staticDataArr_.shrink_to_fit();
}

void Graph::installFinalFrameLayout() {
    if (staticArea_ != nullptr && frameSize_ != 0) {
        return;
    }
    const TupleType *runtimeDataTy = runtimeDataType();
    const TupleType *staticDataTy  = staticDataType();
    ASSERT(runtimeDataTy != nullptr && staticDataTy != nullptr, "Graph layout is incomplete.");
    // Per the user constraint, the static area lives in autoSpace (GC), and the
    // Graph only holds the Tuple*.
    auto &allocator = camel::core::mm::autoSpace();
    ::Tuple *area   = ::Tuple::create(staticDataTy->size(), allocator);
    for (size_t i = 1; i < staticDataTy->size(); ++i) {
        area->set<slot_t>(i, getStaticDataSlot(-static_cast<data_idx_t>(i)));
    }
    staticArea_ = area;
    frameSize_  = sizeof(camel::core::context::Frame) + sizeof(slot_t) * runtimeDataTy->size();
}

slot_t Graph::getStaticDataSlot(data_idx_t index) const {
    ASSERT(index < 0, "Static data index must be negative.");
    size_t idx             = static_cast<size_t>(-index);
    const size_t totalSize = staticDataSize();
    if (idx >= totalSize) {
        throw std::out_of_range(
            std::format(
                "Static data index out of range when getting data of graph ({}) at index {}. "
                "(total size: {})",
                name_,
                index,
                totalSize));
    }
    ASSERT(
        idx < totalSize,
        std::format(
            "Static data index out of range when getting data of graph ({}) at index {}. "
            "(total size: {})",
            name_,
            index,
            totalSize));
    if (hasPackedStaticData_) {
        return packedStaticData_[idx];
    }
    return staticDataArr_[idx];
}

// =============================================================================
// Graph subgraphs and dependencies.
// =============================================================================

std::optional<std::unordered_set<graph_ptr_t>>
Graph::getSubGraphsByName(const std::string &name) const {
    const auto &sg = subGraphs();
    auto it        = sg.find(name);
    if (it != sg.end()) {
        return it->second;
    }
    return std::nullopt;
}

namespace {

using camel::core::rtdata::fromSlot;
} // namespace
} // namespace camel::compile::gir
