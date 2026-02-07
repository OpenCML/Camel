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
 * Updated: Feb. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

/** GIR 枚举与基础类型：NodeType/LinkType、类型别名、弱指针哈希。不依赖 Graph/Node 完整定义。 */

#include <list>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace GraphIR {

// =============================================================================
// 枚举与字符串转换声明
// =============================================================================

enum class NodeType {
    DATA,
    PORT,
    CAST,
    COPY,
    FILL,
    ACCS,
    BRCH,
    JOIN,
    CALL,
    BIND,
    FUNC,
    OPER,
    EXIT,
    SYNC,
    NREF,
    DREF,
};

enum class LinkType {
    Norm,
    With,
    Ctrl,
};

std::string to_string(NodeType type);
std::string to_string(LinkType type);

// =============================================================================
// 前向声明与类型别名
// =============================================================================

class Graph;
class Node;

using graph_ptr_t     = std::shared_ptr<Graph>;
using graph_wptr_t    = std::weak_ptr<Graph>;
using graph_vec_t     = std::vector<graph_ptr_t>;
using graph_vec_ptr_t = std::shared_ptr<graph_vec_t>;
using node_ptr_t      = std::shared_ptr<Node>;
using node_wptr_t     = std::weak_ptr<Node>;
using node_lst_t      = std::list<node_ptr_t>;
using node_vec_t      = std::vector<node_ptr_t>;
using node_set_t      = std::unordered_set<node_ptr_t>;

using data_idx_t = int16_t;
using arr_size_t = uint16_t;

struct WeakPtrHash {
    template <typename T> std::size_t operator()(const std::weak_ptr<T> &wp) const {
        if (auto sp = wp.lock()) {
            return std::hash<T *>()(sp.get());
        }
        return 0;
    }
};

struct WeakPtrEqual {
    template <typename T>
    bool operator()(const std::weak_ptr<T> &lhs, const std::weak_ptr<T> &rhs) const {
        return !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
    }
};

} // namespace GraphIR
