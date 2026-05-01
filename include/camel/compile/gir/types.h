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
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <compare>
#include <cstdint>
#include <list>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "camel/runtime/graph.h"

namespace camel::runtime {
struct DraftNode;
}

namespace camel::compile::gir {

// =============================================================================
// Enum and string-conversion declarations
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
    SYNC,
    GATE,
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
// Forward declarations and type aliases
// =============================================================================

class DraftGraphBuilder;
using draft_node_ref_t = camel::runtime::gc_node_ref_t;
using draft_node_t     = camel::runtime::DraftNode;

using graph_ptr_t     = std::shared_ptr<DraftGraphBuilder>;
using graph_wptr_t    = std::weak_ptr<DraftGraphBuilder>;
using graph_vec_t     = std::vector<graph_ptr_t>;
using graph_vec_ptr_t = std::shared_ptr<graph_vec_t>;
using node_handle_t   = draft_node_t *;
using node_lst_t      = std::list<node_handle_t>;
using node_vec_t      = std::vector<node_handle_t>;
using node_span_t     = std::span<const node_handle_t>;
using node_set_t      = std::unordered_set<node_handle_t>;

using data_idx_t = int16_t;

/// Debug/content-addressed fingerprint for a layout-stabilized node (128-bit), materialized as the
/// `gnode:{word0}{word1}` entity ID.
struct NodeDebugFingerprint {
    uint64_t word0 = 0;
    uint64_t word1 = 0;

    std::string toEntityId() const;
    auto operator<=>(const NodeDebugFingerprint &) const = default;
};
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

} // namespace camel::compile::gir
