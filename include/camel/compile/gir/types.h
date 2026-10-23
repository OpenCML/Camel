// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
#include "camel/runtime/node_ref.h"

namespace camel::compile::gir {

// =============================================================================
// Enum and string-conversion declarations
// =============================================================================

enum class LinkType {
    Norm,
    With,
    Ctrl,
    /// Orders and nothing else (docs/design-principles.cn.md 3.1): the two ends run in this
    /// order when both of them run, and neither end runs because of it.
    Order,
};

std::string to_string(LinkType type);

// =============================================================================
// Forward declarations and type aliases
// =============================================================================

class DraftGraphBuilder;
using draft_node_ref_t = camel::runtime::gc_node_ref_t;

using graph_ptr_t     = std::shared_ptr<DraftGraphBuilder>;
using graph_wptr_t    = std::weak_ptr<DraftGraphBuilder>;
using graph_vec_t     = std::vector<graph_ptr_t>;
using graph_vec_ptr_t = std::shared_ptr<graph_vec_t>;
using node_handle_t   = camel::runtime::DraftNodeRef;
using node_lst_t      = std::list<node_handle_t>;
using node_vec_t      = std::vector<node_handle_t>;
using node_span_t     = std::span<const node_handle_t>;
using node_set_t      = std::unordered_set<node_handle_t>;

using data_idx_t = int32_t;

/// Debug/content-addressed fingerprint for a layout-stabilized node (128-bit), materialized as the
/// `gnode:{word0}{word1}` entity ID.
struct NodeDebugFingerprint {
    uint64_t word0 = 0;
    uint64_t word1 = 0;

    std::string toEntityId() const;
    auto operator<=>(const NodeDebugFingerprint &) const = default;
};

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
