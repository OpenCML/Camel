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
// Graph：函数/子图，包含节点集合、端口、静态/运行时数据段
// =============================================================================

class Graph : public std::enable_shared_from_this<Graph> {
  public:
    static constexpr std::size_t kFrameMetaExtraIndex = 0;

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

    FunctionType *funcType() const { return signature_.funcType; }
    graph_arena_ptr_t arena() const { return arena_; }
    camel::core::context::FrameMeta *frameMeta() const;
    void setFrameMeta(camel::core::context::FrameMeta *meta) const {
        setExtra<camel::core::context::FrameMeta, kFrameMetaExtraIndex>(meta);
    }
    const TupleType *staticDataType() const { return signature_.staticDataType; }
    const TupleType *runtimeDataType() const { return signature_.runtimeDataType; }
    const TupleType *closureType() const { return signature_.closureType; }
    bool frozen() const { return frozen_; }

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

    template <typename T, std::size_t Index> T *getExtra() const { return extras_.get<T, Index>(); }
    template <typename T, std::size_t Index> void setExtra(T *ptr) const {
        extras_.set<T, Index>(ptr);
    }

  private:
    friend class Builder;
    friend class GraphBuilder;
    friend class GraphRewriteSession;

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

    std::vector<node_uptr_t> ownedNodes_;
    node_vec_t normPorts_, withPorts_, closure_;
    node_vec_t nodes_;
    Node *exitNode_ = nullptr;

    bool frozen_ = false;

    bool looped_        = false;
    bool parameterized_ = false;

    mutable ExtraStorage<4> extras_;
};

} // namespace camel::compile::gir
