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
 * Created: Mar. 26, 2024
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "utils/log.h"

template <typename load_t, typename node_t> // node_t is a derived class of AbstractTreeNode (CRTP)
class AbstractTreeNode : public std::enable_shared_from_this<AbstractTreeNode<load_t, node_t>> {
  protected:
    using node_ptr_t = std::shared_ptr<node_t>;
    using children_container_t = std::vector<node_ptr_t>;

    node_t *parent_;
    load_t load_;
    children_container_t children_;

  public:
    using iterator = typename children_container_t::iterator;
    using const_iterator = typename children_container_t::const_iterator;

    AbstractTreeNode(load_t load) : parent_(nullptr), load_(load) {}
    virtual ~AbstractTreeNode() = default;

    // ------------------ Iterator Support ------------------
    iterator begin() { return children_.begin(); }
    iterator end() { return children_.end(); }
    const_iterator begin() const { return children_.begin(); }
    const_iterator end() const { return children_.end(); }
    const_iterator cbegin() const { return children_.cbegin(); }
    const_iterator cend() const { return children_.cend(); }

    // ------------------ Accessors ------------------
    node_t *parent() const { return parent_; }

    load_t &load() { return load_; }
    const load_t &load() const { return load_; }

    void setParent(node_t *parent) { parent_ = parent; }

    node_ptr_t clone() { // no parent in the cloned node
        node_ptr_t newNode = std::make_shared<node_t>(load_);
        for (const auto &child : children_) {
            *newNode << child->clone();
        }
        return newNode;
    }

    // ------------------ Add Child ------------------
    node_t &operator<<(const node_ptr_t &node) {
        if (node != nullptr) {
            ASSERT(node->parent_ == nullptr, "Node already has a parent");
            node->parent_ = static_cast<node_t *>(this);
            children_.push_back(node);
        }
        return *static_cast<node_t *>(this);
    }

    // ------------------ Index & Size ------------------
    node_ptr_t at(size_t index) const { return children_.at(index); }
    node_ptr_t operator[](size_t index) const { return children_.at(index); }

    node_ptr_t front() const {
        if (children_.empty())
            throw std::out_of_range("No children in the node");
        return children_.front();
    }
    node_ptr_t back() const {
        if (children_.empty())
            throw std::out_of_range("No children in the node");
        return children_.back();
    }

    size_t size() const { return children_.size(); }
    bool empty() const { return children_.empty(); }

    // ------------------ Search ------------------
    std::optional<size_t> find(const load_t &load) const {
        auto it = std::find_if(children_.begin(), children_.end(),
                               [&](const node_ptr_t &node) { return node->load_ == load; });
        if (it == children_.end())
            return std::nullopt;
        return std::distance(children_.begin(), it);
    }

    template <typename func_t> std::optional<size_t> find(func_t cmp) const {
        auto it = std::find_if(children_.begin(), children_.end(), cmp);
        if (it == children_.end())
            return std::nullopt;
        return std::distance(children_.begin(), it);
    }

    void reverseChildren() { std::reverse(children_.begin(), children_.end()); }

    // ------------------ Tree Traversal ------------------
    template <typename func_t> void foreach (func_t f) const {
        for (const auto &child : children_) {
            f(*child);
        }
    }

    template <typename func_t> void traverse(func_t f) const {
        f(*static_cast<const node_t *>(this));
        foreach ([&](const node_t &child) { child.traverse(f); })
            ;
    }

    template <typename func_t> void traverse(func_t f, size_t &level, size_t &index) {
        f(*static_cast<node_t *>(this));
        level++;
        size_t tmpIdx = index;
        size_t childIndex = 0;
        for (auto &child : children_) {
            index = childIndex++;
            child->traverse(f, level, index);
        }
        level--;
        index = tmpIdx;
    }

    template <typename func_t> void postorder(func_t f) const {
        foreach ([&](const node_t &child) { child.postorder(f); })
            ;
        f(*static_cast<const node_t *>(this));
    }

    template <typename func_t> void postorder(func_t f, size_t &level, size_t &index) {
        level++;
        size_t tmpIdx = index++;
        index = 0;
        for (size_t i = 0; i < children_.size(); ++i) {
            children_[i]->postorder(f, level, index);
            index++;
        }
        level--;
        index = tmpIdx;
        f(*static_cast<node_t *>(this));
    }

    // ------------------ Print ------------------
    void dumpTree(std::ostream &os) {
        std::vector<bool> visible;
        size_t depth = 0;
        size_t index = 0;

        traverse(
            [&](node_t &node) {
                try {
                    bool isLast = false;
                    if (visible.size() <= depth)
                        visible.push_back(true);
                    if (depth > 0) {
                        if (node.parent_ == nullptr) {
                            log_warn << "DumpTree: Node <" << node.toString() << "> has no parent!" << std::endl;
                        } else if (index == node.parent_->size() - 1) {
                            isLast = true;
                            visible.at(depth - 1) = false;
                        }
                    }
                    auto getHead = [&]() -> std::string {
                        size_t i = 0;
                        std::string ret;
                        while (depth > 0 && i < depth - 1) {
                            ret += visible[i] ? "|  " : "   ";
                            ++i;
                        }
                        if (depth > 0)
                            ret += isLast ? "\\-" : "|-";
                        return ret;
                    };
                    os << getHead() << node.toString() << std::endl;

                    // Reset visibility
                    if (depth > 0)
                        for (size_t i = depth; i < visible.size(); ++i)
                            visible[i] = true;

                } catch (const std::exception &e) {
                    os << "ERROR: " << e.what() << std::endl;
                }
            },
            depth, index);
    }

    void print(std::ostream &os = std::cout) { dumpTree(os); }

    // ------------------ Virtual ------------------
    virtual std::string toString() const = 0;
};
