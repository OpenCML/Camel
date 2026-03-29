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
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
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
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <functional>
#include <stdexcept>
#include <string_view>

using namespace camel::core::error;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

namespace {

using namespace camel::core::rtdata;

slot_t makeStaticSlotFromData(const data_ptr_t &data, camel::core::mm::IAllocator &allocator);

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

node_vec_t
collectEntryRootsFromExit(const Graph &graph, const std::unordered_map<Node *, Node *> &nodeMap) {
    auto isEntryExecutable = [](NodeType type) {
        switch (type) {
        case NodeType::DATA:
        case NodeType::PORT:
        case NodeType::SYNC:
        case NodeType::GATE:
        case NodeType::DREF:
        case NodeType::EXIT:
            return false;
        default:
            return true;
        }
    };

    std::unordered_set<Node *> reachable;
    std::vector<Node *> stack;
    if (graph.hasOutput()) {
        stack.push_back(graph.exitNode());
    }
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

void validateSingleControlCompletionLeaf(const Graph &graph) {
    Node *exitNode  = graph.exitNode();
    Node *valueExit = exitNode->normInputs().front();
    if (!valueExit) {
        ASSERT(false, std::format("Graph '{}' has null value-exit input.", graph.name()));
    }
    ASSERT(
        exitNode->ctrlInputs().size() <= 1,
        std::format(
            "Graph '{}' violates control completion contract: EXIT has {} ctrl inputs (expected <= "
            "1).",
            graph.name(),
            exitNode->ctrlInputs().size()));
}

node_vec_t collectConsumersByInput(const Graph &graph, Node *needle, LinkType type) {
    node_vec_t consumers;
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
            consumers.push_back(owner);
        }
    };
    for (Node *owner : graph.nodes()) {
        maybePush(owner);
    }
    if (graph.hasOutput()) {
        maybePush(graph.exitNode());
    }
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

void validateSymmetricAdjacency(LinkType type, Node *from, Node *to) {
    switch (type) {
    case LinkType::Norm:
        ASSERT(
            std::ranges::find(to->normInputs(), from) != to->normInputs().end(),
            "NORM adjacency is not symmetric.");
        break;
    case LinkType::With:
        ASSERT(
            std::ranges::find(to->withInputs(), from) != to->withInputs().end(),
            "WITH adjacency is not symmetric.");
        break;
    case LinkType::Ctrl:
        ASSERT(
            std::ranges::find(to->ctrlInputs(), from) != to->ctrlInputs().end(),
            "CTRL adjacency is not symmetric.");
        break;
    }
}

void validateNodeAdjacency(Node *node) {
    for (Node *out : node->normOutputs()) {
        validateSymmetricAdjacency(LinkType::Norm, node, out);
    }
    for (Node *out : node->withOutputs()) {
        validateSymmetricAdjacency(LinkType::With, node, out);
    }
    for (Node *out : node->ctrlOutputs()) {
        validateSymmetricAdjacency(LinkType::Ctrl, node, out);
    }
    for (Node *input : node->normInputs()) {
        bool linkedBack =
            std::ranges::find(input->normOutputs(), node) != input->normOutputs().end();
        (void)linkedBack;
        ASSERT(linkedBack, "NORM reverse adjacency is not symmetric.");
    }
    for (Node *input : node->withInputs()) {
        bool linkedBack =
            std::ranges::find(input->withOutputs(), node) != input->withOutputs().end();
        (void)linkedBack;
        ASSERT(linkedBack, "WITH reverse adjacency is not symmetric.");
    }
    for (Node *input : node->ctrlInputs()) {
        bool linkedBack =
            std::ranges::find(input->ctrlOutputs(), node) != input->ctrlOutputs().end();
        (void)linkedBack;
        ASSERT(linkedBack, "CTRL reverse adjacency is not symmetric.");
    }
}

void validateFuncGraphReference(const Graph &graph, Node *node) {
    if (node->type() != NodeType::FUNC) {
        return;
    }
    auto *funcNode = tt::as_ptr<FuncNode>(node);
    auto bodyGraph = funcNode->bodyGraph() ? funcNode->bodyGraph()->shared_from_this() : nullptr;
    ASSERT(bodyGraph != nullptr, "FUNC node refers to a null body graph.");
    if (bodyGraph.get() == &graph) {
        // 自递归不要求显式放入 dependencies，GraphBuilder::addDependency(self)
        // 只会标记 looped=true，用于表示递归环。
        ASSERT(
            graph.looped(),
            std::format("Recursive graph '{}' is not marked looped.", graph.name()));
        return;
    }
    ASSERT(
        graph.dependencies().contains(bodyGraph),
        std::format(
            "FUNC node '{}' refers to graph '{}' without dependency registration in '{}'.",
            node->toString(),
            bodyGraph->name(),
            graph.name()));
}

node_vec_t collectUnreachableNodes(const Graph &graph) {
    if (!graph.hasOutput()) {
        return {};
    }
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

std::string makeGraphStableId(const std::string &name) {
    // DebugMap / debugger / runtime diagnostics all key graph origins by stableId.
    // So this id must remain invariant across rearrange(), bytecode lowering, and
    // other passes that may change graph contents or function type details.
    static std::atomic<uint64_t> nextId = 1;
    const uint64_t id                   = nextId++;
    return std::format("graph:{}#{}", name.empty() ? "<anonymous>" : name, id);
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

// =============================================================================
// Graph 创建与节点管理
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
        GetDefaultLogger().in("GIR").debug(
            "Created Graph: {}",
            name_.empty() ? "<anonymous>" : name_));
}

Graph::~Graph() {
    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("GIR").debug(
            "Destroying Graph at {:p} (name='{}').",
            static_cast<const void *>(this),
            name_.empty() ? "<anonymous>" : name_));
}

// -----------------------------------------------------------------------------
// 节点「调试实体 id」两阶段
//
// 1) 构造时（draft）：只有占位串 draft:{地址}，供 Node::debugEntityId() 在任意时刻可查询；
//    与 SourceContext 的草稿绑定（bindGirNodeDraftDebug）按 Node* 关联，不依赖此字符串。
// 2) finalize/rearrange 之后：布局与 dataIndex 已确定，再计算内容寻址指纹并写入
//    nodeStableIds_（形如 gnode:{032x}），同时 sealPromoteGirNodeDebug 把 DebugMap 等
//    键从草稿迁到该实体 id。
// -----------------------------------------------------------------------------

void Graph::installProvisionalNodeStableId(Graph &graph, const Node *node) {
    // 用对象地址保证进程内唯一；与最终 gnode: 指纹无关，seal 时会被覆盖。
    graph.nodeStableIds_[node] = std::format(
        "draft:{:x}",
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(node)));
}

NodeDebugFingerprint
Graph::computeNodeDebugFingerprintForNode(Node *node, uint64_t tieBreaker) const {
    // 128-bit 分成 word0/word1；混合 FNV 风格哈希，目标是「同图、同布局、同结构」则指纹稳定，
    // 不因遍历哈希表顺序而飘（邻接侧已排序槽位索引）。
    uint64_t w0          = debugHashString(stableId_);
    uint64_t w1          = debugMix64(0, static_cast<uint64_t>(static_cast<int>(node->type())));
    data_idx_t stableIdx = 0;
    if (node->type() != NodeType::SYNC && node->type() != NodeType::DREF) {
        stableIdx = node->index();
    }
    w1 = debugMix64(
        w1,
        static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(stableIdx))));
    // 把所有 LinkType 上的邻居 runtime/static 槽位拉平、排序后混入，体现「连了谁」。
    debugHashSortedInputIndices(node, 0xA5A5A5A5A5A5A5A5ULL, w0, w1);

    switch (node->type()) {
    case NodeType::PORT:
        // 同名端口在不同图中应对应不同指纹。
        w0 = debugMix64(w0, debugHashString(nodePortName(node)));
        break;
    case NodeType::ACCS: {
        // 数字下标无 Graph 侧字符串；仅有字符串 key 时才参与哈希。
        auto it = nodeAccsKeys_.find(node);
        if (it != nodeAccsKeys_.end()) {
            w0 = debugMix64(w0, debugHashString(it->second));
        }
        break;
    }
    case NodeType::OPER:
        // 用算子身份区分不同 OPER（槽位与边可能相似）。
        w0 = debugMix64(
            w0,
            reinterpret_cast<uint64_t>(
                reinterpret_cast<uintptr_t>(tt::as_ptr<OperNode>(node)->oper())));
        break;
    case NodeType::FUNC:
        // 子图 stableId 区分指向不同函数体的 FUNC 节点。
        w0 = debugMix64(w0, debugHashString(tt::as_ptr<FuncNode>(node)->bodyGraph()->stableId()));
        break;
    case NodeType::DATA: {
        // 静态槽位内容身份（与常量/函数值等绑定相关）。
        slot_t sl = tt::as_ptr<DataNode>(node)->dataSlot();
        w0        = debugMix64(w0, debugHashBytes(&sl, sizeof(sl)));
        break;
    }
    case NodeType::SYNC:
    case NodeType::GATE:
        // 此类节点在 layout 里常无独立 dataIndex 语义，用地址打破对称、避免误合并。
        w0 = debugMix64(
            w0,
            reinterpret_cast<uint64_t>(reinterpret_cast<uintptr_t>(const_cast<Node *>(node))));
        break;
    default:
        break;
    }

    // 同图内按固定遍历序递增，进一步降低不同节点 128-bit 偶然碰撞的概率。
    w1 = debugMix64(w1, tieBreaker);
    return {w0, w1};
}

void Graph::promoteNodeDebugIds(camel::source::SourceContext *sourceContext) {
    uint64_t seq    = 0;
    auto promoteOne = [&](Node *n) {
        if (n == nullptr) {
            return;
        }
        NodeDebugFingerprint fp = computeNodeDebugFingerprintForNode(n, seq++);
        std::string entityId    = fp.toEntityId();
        nodeStableIds_[n]       = std::move(entityId);
        if (sourceContext == nullptr) {
            return;
        }
        // 仅当该节点在 GCT 阶段登记过草稿绑定时，sealPromote 才会写入 DebugMap / 语义表。
        const std::string &id = nodeStableIds_[n];
        sourceContext->sealPromoteGirNodeDebug(n, id);
    };

    // 顺序与 computeLayout 中端口/闭包/主体节点的大致层次一致，保证 tieBreaker 可复现。
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
    if (!graph_->builderState_) {
        graph_->builderState_ = snapshotStateFromGraph(*graph_);
    }
    return *graph_->builderState_;
}

void GraphBuilder::syncStateToGraph() const {
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
    // 只失效布局缓存，不改变 finalized 标记。
    // 在当前模型下，只有非 finalized 的图（初始构造或 draft clone）才会走到这里，
    // 因为 finalized 图的 mutation 会被 assertBuildable 拦截。
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

Node *Graph::exitNode() const {
    Node *exit = activeState() ? activeState()->exitNode : exitNode_;
    ASSERT(exit != nullptr, std::format("Graph {} has no exit node.", name_));
    return exit;
}
Node *Graph::outputNode() const {
    Node *exit = activeState() ? activeState()->exitNode : exitNode_;
    ASSERT(exit != nullptr, std::format("Graph {} has no exit node.", name_));
    return exit->normInputs().front();
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

    st.exitNode = ExitNode::create(*graph_, node->dataType(), node->index());
    Node::link(LinkType::Norm, node, st.exitNode);
    markMutated();
}

// =============================================================================
// Graph 查询与数据段
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
    // seal 后释放构图期容器容量；运行时改走 packedStaticData_。
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
    // 按用户约束：静态区统一落在 autoSpace（GC）里；Graph 仅持有 Tuple*。
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
// Graph 子图与依赖
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
        newPort->setDataType(port->dataType());
        nodeMap[port] = newPort;
        GraphBuilder(newGraph).addPort(newPort, true);
        if (auto *sourceContext =
                src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
            sourceContext->cloneGirNodeDebugBinding(port, newPort);
        }
    }
    for (Node *port : src->normPorts()) {
        Node *newPort = port->clone(*newGraph);
        newPort->setDataType(port->dataType());
        nodeMap[port] = newPort;
        GraphBuilder(newGraph).addPort(newPort, false);
        if (auto *sourceContext =
                src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
            sourceContext->cloneGirNodeDebugBinding(port, newPort);
        }
    }
    for (Node *closureNode : src->closure()) {
        Node *newClosureNode = closureNode->clone(*newGraph);
        newClosureNode->setDataType(closureNode->dataType());
        nodeMap[closureNode] = newClosureNode;
        GraphBuilder(newGraph).addClosure(newClosureNode);
        if (auto *sourceContext =
                src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
            sourceContext->cloneGirNodeDebugBinding(closureNode, newClosureNode);
        }
    }
    for (Node *node : src->nodes()) {
        Node *newNode = node->clone(*newGraph);
        newNode->setDataType(node->dataType());
        nodeMap[node] = newNode;
        if (auto *sourceContext =
                src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
            sourceContext->cloneGirNodeDebugBinding(node, newNode);
        }
    }

    // clone 过程分两步补齐 EXIT 映射：当图直接返回 PORT/closure 时，端口输出会先指向尚未映射的
    // EXIT，故端口阶段也允许临时缺失，随后在 EXIT 映射完成后统一补回。
    overwriteFreshNodeAdjacencyPreservingOrder(src->withPorts(), nodeMap, true);
    overwriteFreshNodeAdjacencyPreservingOrder(src->normPorts(), nodeMap, true);
    overwriteFreshNodeAdjacencyPreservingOrder(src->closure(), nodeMap, true);
    overwriteFreshNodeAdjacencyPreservingOrder(src->nodes(), nodeMap, true);
    if (src->hasOutput()) {
        Node *srcExit    = src->exitNode();
        Node *outputNode = srcExit->normInputs().front();
        Node *newOutput  = requireMappedNode(
            outputNode,
            nodeMap,
            "Output node not found in node map during graph cloning.");
        Node *newExitNode = ExitNode::create(*newGraph, newOutput->dataType(), newOutput->index());
        nodeMap[srcExit]  = newExitNode;
        overwriteFreshNodeAdjacencyPreservingOrder({srcExit}, nodeMap);
        for (Node *in : newExitNode->normInputs()) {
            if (std::ranges::find(in->normOutputs(), newExitNode) == in->normOutputs().end()) {
                NodeMutation::normOutputs(in).push_back(newExitNode);
            }
        }
        for (Node *in : newExitNode->withInputs()) {
            if (std::ranges::find(in->withOutputs(), newExitNode) == in->withOutputs().end()) {
                NodeMutation::withOutputs(in).push_back(newExitNode);
            }
        }
        for (Node *in : newExitNode->ctrlInputs()) {
            if (std::ranges::find(in->ctrlOutputs(), newExitNode) == in->ctrlOutputs().end()) {
                NodeMutation::ctrlOutputs(in).push_back(newExitNode);
            }
        }
        newGraph->exitNode_ = newExitNode;
        if (newGraph->builderState_) {
            newGraph->builderState_->exitNode = newExitNode;
        }
        if (auto *sourceContext =
                src->getExtra<camel::source::SourceContext, kSourceContextExtraIndex>()) {
            sourceContext->cloneGirNodeDebugBinding(srcExit, newExitNode);
        }
    } else {
        Node *nullOutput = DataNode::create(*newGraph, Data::null());
        GraphBuilder(newGraph).setOutput(nullOutput);
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
        newGraph->hasOutput(),
        std::format(
            "cloneGraph produced graph '{}' without output (source='{}', sourceHasOutput={}).",
            newGraph->name_,
            src->name(),
            src->hasOutput() ? "true" : "false"));
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
        if (targetGraph.hasOutput()) {
            declared.insert(targetGraph.exitNode());
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
        if (targetGraph.hasOutput()) {
            scanInputs(targetGraph.exitNode());
        }
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
            nrefNode->setDataType(input->dataType());
            Node::link(LinkType::Norm, input, nrefNode);
            gateByInput.emplace(input, nrefNode);
            parameterGateTargets.push_back(nrefNode);
            gatedInput = nrefNode;
        } else {
            gatedInput = gateIt->second;
        }
        nodeMap[port] = gatedInput;
    }

    if (!targetGraph.hasOutput()) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("GIR").warn(
                "Cannot inline FUNC node {} in graph {}: target graph '{}' has no output.",
                node->toString(),
                graph_->name_,
                targetGraph.name()));
        return InlineResult{};
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
    Node *targetOutput = targetGraph.exitNode()->normInputs().front();
    result.valueExit =
        requireMappedNode(targetOutput, nodeMap, "Target output node not found during inlining.");
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
        // 当子图仅由 DATA/PORT 直达 EXIT 时，反向推导可能得不到可执行入口根。
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

    Node *completionCtrl      = result.valueExit;
    const auto exitCtrlInputs = targetGraph.exitNode()->ctrlInputs();
    ASSERT(
        exitCtrlInputs.size() <= 1,
        std::format(
            "Target graph '{}' has {} EXIT ctrl inputs during inlining (expected <= 1).",
            targetGraph.name(),
            exitCtrlInputs.size()));
    if (!exitCtrlInputs.empty()) {
        completionCtrl = requireMappedNode(
            exitCtrlInputs.front(),
            nodeMap,
            "Mapped EXIT ctrl completion node not found during inlining.");
    }
    ASSERT(completionCtrl != nullptr, "Inlined callable has no control completion anchor.");

    if (completionCtrl != result.valueExit) {
        auto *gatedValue = GateNode::create(*graph_);
        gatedValue->setDataType(result.valueExit->dataType());
        Node::link(LinkType::Ctrl, completionCtrl, gatedValue);
        Node::link(LinkType::Norm, result.valueExit, gatedValue);
        result.valueExit = gatedValue;
    }

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

    if (graph.hasOutput()) {
        Node *exitNode     = graph.exitNode();
        Node *exitInput    = exitNode->normInputs().front();
        auto it            = indexMap.find(exitInput);
        data_idx_t exitIdx = (it != indexMap.end()) ? it->second : exitInput->index();
        assignIndex(exitNode, exitIdx);
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
    graph.frameSize_                 = 0;
    graph.staticArea_                = nullptr;
}

void GraphBuilder::validateGraph(const Graph &graph) {
    // 完整性硬约束：进入 seal 的图必须先有 output（EXIT 节点）。
    ASSERT(graph.hasOutput(), std::format("Graph '{}' has no output (EXIT node).", graph.name()));
    ASSERT(graph.exitNode() != nullptr, std::format("Graph '{}' has no exit node.", graph.name()));
    ASSERT(
        graph.exitNode()->normInputs().size() == 1,
        std::format("Graph '{}' exit node must have exactly one input.", graph.name()));

    for (Node *node : graph.normPorts()) {
        validateNodeAdjacency(node);
    }
    for (Node *node : graph.withPorts()) {
        validateNodeAdjacency(node);
    }
    for (Node *node : graph.closure()) {
        validateNodeAdjacency(node);
    }
    for (Node *node : graph.nodes()) {
        validateNodeAdjacency(node);
        validateFuncGraphReference(graph, node);
        if (node->type() == NodeType::BRCH) {
            auto *br = tt::as_ptr<BrchNode>(node);
            if (br->hasMatchedJoin() && br->armCount() != br->matchedJoin()->armCount()) {
                ASSERT(
                    false,
                    std::format(
                        "BRCH/JOIN arm count mismatch in graph '{}': brch={}, brArms={}, join={}, "
                        "joinArms={}.",
                        graph.name(),
                        br->toString(),
                        br->armCount(),
                        br->matchedJoin()->toString(),
                        br->matchedJoin()->armCount()));
            }
        }
        if (node->type() == NodeType::JOIN) {
            ASSERT(
                !tt::as_ptr<JoinNode>(node)->hasMatchedBranch() ||
                    tt::as_ptr<JoinNode>(node)->armCount() ==
                        tt::as_ptr<JoinNode>(node)->matchedBranch()->armCount(),
                std::format("JOIN/BRCH arm count mismatch in graph '{}'.", graph.name()));
        }
        if (node->type() == NodeType::GATE) {
            ASSERT(
                !node->normInputs().empty(),
                std::format(
                    "GATE node '{}' in graph '{}' must have at least one Norm input.",
                    node->toString(),
                    graph.name()));
            ASSERT(
                !node->ctrlInputs().empty(),
                std::format(
                    "GATE node '{}' in graph '{}' must have at least one Ctrl input.",
                    node->toString(),
                    graph.name()));
        }
    }
    validateNodeAdjacency(graph.exitNode());
    validateSingleControlCompletionLeaf(graph);

    for (const auto &[name, subGraphs] : graph.subGraphs()) {
        for (const auto &subGraph : subGraphs) {
            (void)subGraph;
            ASSERT(subGraph != nullptr, "Graph subgraph registry contains null graph.");
            ASSERT(
                subGraph->name() == name,
                std::format(
                    "Subgraph registry key '{}' mismatches graph '{}'.",
                    name,
                    subGraph->name()));
        }
    }
}

void GraphBuilder::validateGraphRecursively(const graph_ptr_t &graph) {
    if (!graph) {
        return;
    }
    std::unordered_set<const Graph *> visited;
    std::function<void(const graph_ptr_t &)> dfs = [&](const graph_ptr_t &curr) {
        if (!curr || !visited.insert(curr.get()).second) {
            return;
        }
        validateGraph(*curr);
        for (const auto &[_, subGraphs] : curr->subGraphs()) {
            for (const auto &subGraph : subGraphs) {
                dfs(subGraph);
            }
        }
        for (const auto &dep : curr->dependencies()) {
            dfs(dep);
        }
    };
    dfs(graph);
}

void GraphBuilder::rearrange() const {
    GetDefaultLogger().in("GIR").debug("Rearranging graph {}.", graph_->name_);
    auto layout = computeLayout(*graph_);
    applyLayout(*graph_, layout);
}

} // namespace camel::compile::gir
