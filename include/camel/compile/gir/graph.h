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
 * Created: Aug. 13, 2024
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <unordered_map>

#include "arena.h"
#include "camel/core/data.h"
#include "camel/core/slot.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/debug.h"
#include "camel/utils/exstore.h"
#include "camel/utils/type.h"
#include "types.h"

class OperatorIndex;
class Tuple;

namespace camel::core::context {}

namespace camel::source {
class SourceContext;
}

namespace camel::compile::gir {

class Builder;
class GraphBuilder;

using Type              = camel::core::type::Type;
using FunctionType      = camel::core::type::FunctionType;
using TupleType         = camel::core::type::TupleType;
using data_vec_t        = camel::core::data::data_vec_t;
using data_ptr_t        = camel::core::data::data_ptr_t;
using static_slot_vec_t = std::vector<slot_t>;

struct GraphBuilderState {
    std::unordered_map<std::string, std::unordered_set<graph_ptr_t>> subGraphs;
    std::unordered_set<graph_ptr_t> dependencies;
    node_vec_t normPorts, withPorts, closure;
    node_vec_t nodes;
    static_slot_vec_t staticDataArr = {NullSlot};
    FunctionType *funcType          = nullptr;
    TupleType *staticDataType       = nullptr;
    TupleType *runtimeDataType      = nullptr;
    TupleType *closureType          = nullptr;
    size_t runtimeDataSize          = 1;
    Node *exitNode                  = nullptr;
    bool looped                     = false;
    bool parameterized              = false;
};

enum class SealState {
    Draft,
    Sealing,
    Sealed,
};

// =============================================================================
// Graph：GIR 函数/子图的最终只读产物。
//
// Graph 一旦由 GraphBuilder::sealGraph() 封印后即视为不可变。
// 所有结构编辑（增删节点、改边、重排布局）都在 GraphDraft / GraphBuilder 的
// draft 工作态上完成，最终通过 sealGraph 一次性导出封印图。
//
// 封印后，所有节点的邻接 vectors 被搬迁到 arena 上的定长数组（frozen 模式），
// draft vectors 被释放，节点不再允许结构编辑。
//
// extras_ 提供 O(1) 图级缓存槽位（JIT entry、topo cache 等），
// 由各后端/工具自行约定 index 并维护失效协议，Graph 本身只提供通用
// getExtra / setExtra 泛型接口。
// =============================================================================

class Graph : public std::enable_shared_from_this<Graph> {
  public:
    static std::string makeStableId(const std::string &name);

    Graph(const Graph &other)            = delete;
    Graph &operator=(const Graph &other) = delete;
    Graph(Graph &&other)                 = delete;
    Graph &operator=(Graph &&other)      = delete;

    explicit Graph(
        FunctionType *funcType, const graph_ptr_t &graph = nullptr, const std::string &name = "");
    ~Graph();

    static graph_ptr_t null() { return nullptr; }

    bool operator==(const Graph &other) const { return this == &other; }
    bool operator!=(const Graph &other) const { return !(this == &other); }

    bool isRoot() const { return !outer_.lock(); }
    bool isMacro() const { return signature_.funcType && signature_.funcType->modifiers().macro(); }
    const std::string &name() const { return name_; }
    std::string mangledName() const { return name_ + std::format("<{}>", funcType()->mangle()); }
    const std::string &stableId() const { return stableId_; }
    std::string location() const;
    bool looped() const { return activeState() ? activeState()->looped : looped_; }
    bool empty() const { return (activeState() ? activeState()->nodes : nodes_).empty(); }
    graph_ptr_t outer() const;
    size_t inDegree() const {
        return (activeState() ? activeState()->dependencies : dependencies_).size();
    }
    size_t outDegree() const { return dependents_.size(); }

    std::string toString() const;

    /// Sealed 表示此图已经历 sealGraph 封印，拥有完整的 slot 编号、layout 与执行期静态区，
    /// 且所有节点邻接已冻结到 FrozenRegion。状态机是单向的：
    /// Draft -> Sealing -> Sealed。
    ///
    /// 约束：
    /// - sealed 图只读；
    /// - draft 可变视图仅存在于 builderState_（staging）；
    /// - Graph 的“_ 字段”在 draft 期是 staging 的镜像缓存，仅用于兼容只读访问。
    bool finalized() const { return sealState_ == SealState::Sealed; }
    bool hasDraftStaging() const { return builderState_ != nullptr; }
    bool hasMutableDraftView() const { return hasDraftStaging() && !finalized(); }

    FunctionType *funcType() const {
        return activeState() ? activeState()->funcType : signature_.funcType;
    }
    graph_arena_ptr_t arena() const { return arena_; }
    size_t frameSize() const { return frameSize_; }
    ::Tuple *staticArea() const { return staticArea_; }
    bool hasFrameLayout() const { return frameSize_ != 0 && staticArea_ != nullptr; }
    const TupleType *staticDataType() const {
        return activeState() ? activeState()->staticDataType : signature_.staticDataType;
    }
    const TupleType *runtimeDataType() const {
        return activeState() ? activeState()->runtimeDataType : signature_.runtimeDataType;
    }
    const TupleType *closureType() const {
        return activeState() ? activeState()->closureType : signature_.closureType;
    }

    const static_slot_vec_t &staticDataArr() const {
        return activeState() ? activeState()->staticDataArr : staticDataArr_;
    }
    slot_t getStaticDataSlot(data_idx_t index) const;
    size_t staticDataSize() const {
        if (hasPackedStaticData_) {
            return packedStaticDataSize_;
        }
        return staticDataArr().size();
    }
    size_t runtimeDataSize() const {
        return activeState() ? activeState()->runtimeDataSize : signature_.runtimeDataSize;
    }

    std::optional<std::unordered_set<graph_ptr_t>>
    getSubGraphsByName(const std::string &name) const;
    const std::unordered_map<std::string, std::unordered_set<graph_ptr_t>> &subGraphs() const {
        return activeState() ? activeState()->subGraphs : subGraphs_;
    }

    const std::unordered_set<graph_wptr_t, WeakPtrHash, WeakPtrEqual> &dependents() const {
        return dependents_;
    }
    const std::unordered_set<graph_ptr_t> &dependencies() const {
        return activeState() ? activeState()->dependencies : dependencies_;
    }
    bool parameterized() const {
        return activeState() ? activeState()->parameterized : parameterized_;
    }

    Node *exitNode() const;
    Node *outputNode() const;

    const node_vec_t &nodes() const { return activeState() ? activeState()->nodes : nodes_; }
    node_vec_t ports() const {
        const auto &norm = normPorts();
        const auto &with = withPorts();
        node_vec_t ports;
        ports.reserve(norm.size() + with.size());
        ports.insert(ports.end(), norm.begin(), norm.end());
        ports.insert(ports.end(), with.begin(), with.end());
        return ports;
    }
    bool hasPorts() const { return !normPorts().empty() || !withPorts().empty(); }
    bool hasClosure() const { return !closure().empty(); }
    const node_vec_t &normPorts() const {
        return activeState() ? activeState()->normPorts : normPorts_;
    }
    const node_vec_t &withPorts() const {
        return activeState() ? activeState()->withPorts : withPorts_;
    }
    const node_vec_t &closure() const { return activeState() ? activeState()->closure : closure_; }
    size_t argsCount() const { return normPorts().size() + withPorts().size() + closure().size(); }

    const std::string &nodeDebugEntityId(const Node *node) const;
    const std::string &nodePortName(const Node *node) const;
    const std::string &nodeAccsKey(const Node *node) const;

    /// 将 OperatorIndex shared_ptr 注册到本图，返回裸指针供 OperNode 持有。
    ::OperatorIndex *registerOperIndex(std::shared_ptr<::OperatorIndex> idx);
    /// 从注册表查找裸指针对应的 shared_ptr（用于 clone 时传递所有权到新图）。
    std::shared_ptr<::OperatorIndex> lookupOperIndex(const ::OperatorIndex *raw) const;

    /// 通用 extra 缓存槽位。index 由各后端/工具自行约定，Graph 不感知具体用途。
    template <typename T, std::size_t Index> T *getExtra() const { return extras_.get<T, Index>(); }
    template <typename T, std::size_t Index> void setExtra(T *ptr) const {
        extras_.set<T, Index>(ptr);
    }

  private:
    // draft 期读取统一经 staging；sealed 后 staging 被消费，读取落回只读固化字段。
    const GraphBuilderState *activeState() const { return builderState_.get(); }
    friend class Node;
    friend class PortNode;
    friend class FuncNode;
    friend class OperNode;
    friend class AccsNode;
    friend class Builder;
    friend class GraphBuilder;

    /// rearrange 之后、安装执行期布局之前：写入 gnode: 实体 id 并 promote SourceContext 调试映射。
    void promoteNodeDebugIds(camel::source::SourceContext *sourceContext);
    NodeDebugFingerprint computeNodeDebugFingerprintForNode(Node *node, uint64_t tieBreaker) const;
    /// 草稿节点占位 id（指针十六进制），seal 时会替换为内容寻址实体 id。
    static void installProvisionalNodeStableId(Graph &graph, const Node *node);
    void packStaticSlotsToFrozen();
    void installFinalFrameLayout();

    std::string name_;
    std::string stableId_;
    graph_wptr_t outer_;
    graph_arena_ptr_t arena_;

    std::unordered_map<std::string, std::unordered_set<graph_ptr_t>> subGraphs_;
    std::unordered_set<graph_ptr_t> dependencies_;
    std::unordered_set<graph_wptr_t, WeakPtrHash, WeakPtrEqual> dependents_;

    struct SignatureLayout {
        FunctionType *funcType     = nullptr;
        TupleType *staticDataType  = nullptr;
        TupleType *runtimeDataType = nullptr;
        TupleType *closureType     = nullptr;
        size_t runtimeDataSize     = 1;
    } signature_;
    static_slot_vec_t staticDataArr_ = {NullSlot};
    bool hasPackedStaticData_        = false;
    slot_t *packedStaticData_        = nullptr;
    size_t packedStaticDataSize_     = 0;
    size_t frameSize_                = 0;
    ::Tuple *staticArea_             = nullptr;

    // --- 节点所有权 ---
    // Node 对象在 GraphArena 中构造；arena 通过 tracked dtor 在 Graph 销毁时统一析构节点对象。
    // Graph 仅记录节点集合用于遍历、freeze 及调试映射维护。
    std::vector<Node *> ownedNodes_;

    // --- 节点属性集中存储 ---
    // stableId 和 portName 不存于 Node 自身，由 Graph 集中管理。
    // 使 Node 更接近平凡析构（无 std::string 成员），
    // 同时避免 frozen arena node 需要存储动态字符串。
    std::unordered_map<const Node *, std::string> nodeStableIds_;
    std::unordered_map<const Node *, std::string> nodePortNames_;
    std::unordered_map<const Node *, std::string> nodeAccsKeys_;

    // --- 非平凡数据集中持有 ---
    // OperNode 持有裸指针，实际 shared_ptr 所有权由 Graph 集中管理。
    // 这使 Node 本身更接近平凡析构，为 arena 化分配铺路。
    std::unordered_map<const ::OperatorIndex *, std::shared_ptr<::OperatorIndex>>
        operIndexRegistry_;
    node_vec_t normPorts_, withPorts_, closure_;
    node_vec_t nodes_;
    Node *exitNode_ = nullptr;

    /// 单向终结标记。sealGraph() 设为 true 后永不回退。
    /// 要修改已封印的图，必须克隆出 draft 副本再操作。
    SealState sealState_ = SealState::Draft;

    bool looped_                     = false;
    bool parameterized_              = false;
    uint64_t provisionalDebugIdSeed_ = 0;
    std::shared_ptr<GraphBuilderState> builderState_;

    mutable ExtraStorage<4> extras_;
};

} // namespace camel::compile::gir
