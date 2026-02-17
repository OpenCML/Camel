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
 * Created: May. 05, 2024
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "common/tree.h"
#include "compile/gct/load.h"

namespace GraphConstructTree {

// =============================================================================
// Node：GCT 树节点（CRTP 继承 AbstractTreeNode）
// =============================================================================

class Node : public AbstractTreeNode<load_ptr_t, Node> {
  public:
    Node(load_ptr_t load) : AbstractTreeNode(load) {}
    virtual ~Node() = default;

    LoadType type() const { return load_->type(); }
    std::string toString() const { return load_->toString(); }

    template <typename T> node_ptr_t atAs(size_t index) const {
        ASSERT(index < children_.size(), "Index out of bounds");
        ASSERT(children_.at(index) != nullptr, "Child node is null");
        ASSERT(
            std::dynamic_pointer_cast<T>(children_.at(index)->load()),
            "Dynamic pointer cast failed");
        return children_.at(index);
    }

    template <typename LoadT> std::shared_ptr<LoadT> loadAs() {
        ASSERT(std::dynamic_pointer_cast<LoadT>(load_), "Load type cast failed");
        return std::dynamic_pointer_cast<LoadT>(load_);
    }
    template <typename LoadT> const std::shared_ptr<LoadT> loadAs() const {
        ASSERT(std::dynamic_pointer_cast<LoadT>(load_), "Load type does not match requested type");
        return std::dynamic_pointer_cast<LoadT>(load_);
    }
};

} // namespace GraphConstructTree
