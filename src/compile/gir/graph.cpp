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
 * Updated: Apr. 01, 2026
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
#include <unordered_set>

using namespace camel::core::error;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

namespace {

using namespace camel::core::rtdata;

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

// 不能放在匿名命名空间内：Graph::makeStableId 定义在块外，需在本翻译单元内可见。
static std::string makeGraphStableId(const std::string &name) {
    // DebugMap / debugger / runtime diagnostics all key graph origins by stableId.
    // So this id must remain invariant across rearrange(), bytecode lowering, and
    // other passes that may change graph contents or function type details.
    static std::atomic<uint64_t> nextId = 1;
    const uint64_t id                   = nextId++;
    return std::format("graph:{}#{}", name.empty() ? "<anonymous>" : name, id);
}

// GraphBuilder 的实现已拆至 graph_builder.cpp（含 seal/clone/inline 与 staging 同步）。

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
// 节点「调试实体 id」两阶段
//
// 1) 构造时（draft）：只有占位串 draft:{序号}，供 Node::debugEntityId() 在任意时刻可查询；
//    与 SourceContext 的草稿绑定（bindGirNodeDraftDebug）按 Node* 关联，不依赖此字符串。
// 2) finalize/rearrange 之后：布局与 dataIndex 已确定，再计算内容寻址指纹并写入
//    nodeStableIds_（形如 gnode:{032x}），同时 sealPromoteGirNodeDebug 把 DebugMap 等
//    键从草稿迁到该实体 id。
// -----------------------------------------------------------------------------

void Graph::installProvisionalNodeStableId(Graph &graph, const Node *node) {
    // 草稿期仅需要“图内可区分”的临时实体 id；最终会在 seal 阶段统一提升为 gnode: 指纹。
    const uint64_t seq = graph.provisionalDebugIdSeed_++;
    graph.nodeStableIds_[node] =
        std::format("draft:{}:{}", graph.stableId_, static_cast<unsigned long long>(seq));
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

Node *Graph::exitNode() const {
    Node *exit = activeState() ? activeState()->exitNode : exitNode_;
    ASSERT(exit != nullptr, std::format("Graph {} has no output node.", name_));
    return exit;
}
Node *Graph::outputNode() const { return exitNode(); }

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

} // namespace camel::compile::gir
