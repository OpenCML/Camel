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
 * Updated: Mar. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <unordered_map>

#include "arena.h"
#include "camel/core/data.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/debug.h"
#include "camel/utils/exstore.h"
#include "camel/utils/log.h"
#include "camel/utils/type.h"
#include "types.h"

namespace camel::core::context {
struct FrameMeta;
}

namespace camel::source {
class SourceContext;
}

namespace camel::compile::gir {

class Builder;
class GraphBuilder;
class GraphRewriteSession;

using Type         = camel::core::type::Type;
using FunctionType = camel::core::type::FunctionType;
using TupleType    = camel::core::type::TupleType;
using data_vec_t   = camel::core::data::data_vec_t;
using data_ptr_t   = camel::core::data::data_ptr_t;
using func_ptr_t   = camel::core::data::func_ptr_t;

// =============================================================================
// Graph：GIR 函数/子图的最终只读产物。
//
// Graph 一旦由 GraphBuilder::finalize() 导出后即视为不可变。
// 所有结构编辑（增删节点、改边、重排布局）都在 GraphDraft / GraphBuilder 的
// 工作态上完成，最终通过 finalize 一次性写入新图。
//
// extras_ 提供 O(1) 图级缓存槽位（FrameMeta、JIT entry、topo cache 等），
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
    bool looped() const { return looped_; }
    bool empty() const { return nodes_.empty(); }
    graph_ptr_t outer() const;
    size_t inDegree() const { return dependencies_.size(); }
    size_t outDegree() const { return dependents_.size(); }

    std::string toString() const;

    /// finalized() 表示此图已经历 finalize 导出，拥有完整的 slot 编号、layout 与 FrameMeta。
    /// 此标志为单向标记：一旦设为 true 则永不回退。要修改图必须克隆后在 draft 上操作。
    bool finalized() const { return finalized_; }

    FunctionType *funcType() const { return signature_.funcType; }
    graph_arena_ptr_t arena() const { return arena_; }
    camel::core::context::FrameMeta *frameMeta() const;
    const TupleType *staticDataType() const { return signature_.staticDataType; }
    const TupleType *runtimeDataType() const { return signature_.runtimeDataType; }
    const TupleType *closureType() const { return signature_.closureType; }

    const data_vec_t &staticDataArr() const { return staticDataArr_; }
    data_ptr_t materializeStaticData(data_idx_t index) const;
    data_ptr_t getStaticData(data_idx_t index) const;
    size_t staticDataSize() const { return staticDataArr_.size(); }
    size_t runtimeDataSize() const { return signature_.runtimeDataSize; }

    std::optional<std::unordered_set<graph_ptr_t>>
    getSubGraphsByName(const std::string &name) const;
    const std::unordered_map<std::string, std::unordered_set<graph_ptr_t>> &subGraphs() const {
        return subGraphs_;
    }

    const std::unordered_set<graph_wptr_t, WeakPtrHash, WeakPtrEqual> &dependents() const {
        return dependents_;
    }
    const std::unordered_set<graph_ptr_t> &dependencies() const { return dependencies_; }
    bool parameterized() const { return parameterized_; }

    Node *exitNode() const;
    Node *outputNode() const;
    bool hasOutput() const { return exitNode_ != nullptr; }

    const node_vec_t &nodes() const { return nodes_; }
    node_vec_t ports() const {
        node_vec_t ports;
        ports.reserve(normPorts_.size() + withPorts_.size());
        ports.insert(ports.end(), normPorts_.begin(), normPorts_.end());
        ports.insert(ports.end(), withPorts_.begin(), withPorts_.end());
        return ports;
    }
    bool hasPorts() const { return !normPorts_.empty() || !withPorts_.empty(); }
    bool hasClosure() const { return !closure_.empty(); }
    const node_vec_t &normPorts() const { return normPorts_; }
    const node_vec_t &withPorts() const { return withPorts_; }
    const node_vec_t &closure() const { return closure_; }
    size_t argsCount() const { return normPorts_.size() + withPorts_.size() + closure_.size(); }

    /// 通用 extra 缓存槽位。index 由各后端/工具自行约定，Graph 不感知具体用途。
    template <typename T, std::size_t Index> T *getExtra() const { return extras_.get<T, Index>(); }
    template <typename T, std::size_t Index> void setExtra(T *ptr) const {
        extras_.set<T, Index>(ptr);
    }

  private:
    friend class Builder;
    friend class GraphBuilder;
    friend class GraphRewriteSession;

    // FrameMeta 的 extra index 由 Graph 内部约定，对外不暴露。
    // 各 VM/工具的 extra index 在各自的头文件中定义。
    static constexpr std::size_t kFrameMetaExtraIndex_ = 0;

    void setFrameMeta(camel::core::context::FrameMeta *meta) const {
        setExtra<camel::core::context::FrameMeta, kFrameMetaExtraIndex_>(meta);
    }

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
    data_vec_t staticDataArr_ = {nullptr};

    // --- 节点所有权 ---
    // 当前 Node 由 ownedNodes_ (unique_ptr) 管理，Graph 析构时自动释放。
    // 后续可考虑让 finalized 图的 Node 进入 arena 以提升地址局部性；
    // draft 阶段可保留 unique_ptr 的灵活管理方式。
    std::vector<node_uptr_t> ownedNodes_;
    node_vec_t normPorts_, withPorts_, closure_;
    node_vec_t nodes_;
    Node *exitNode_ = nullptr;

    /// 单向终结标记。finalize() 设为 true 后永不回退。
    /// 要修改已终结的图，必须克隆出 draft 副本再操作。
    bool finalized_ = false;

    bool looped_        = false;
    bool parameterized_ = false;

    mutable ExtraStorage<4> extras_;
};

} // namespace camel::compile::gir
