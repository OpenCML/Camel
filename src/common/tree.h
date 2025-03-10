/**
 * Copyright (c) 2024 Beijing Jiaotong University
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

template <typename load_t> class AbstractTreeNode;

template <typename load_t> using tree_node_t = AbstractTreeNode<load_t>;

template <typename load_t> using tree_node_ptr_t = std::shared_ptr<tree_node_t<load_t>>;

template <typename load_t> using tree_children_t = std::vector<tree_node_ptr_t<load_t>>;

template <typename load_t>
class AbstractTreeNode : public tree_children_t<load_t>, std::enable_shared_from_this<AbstractTreeNode<load_t>> {
  protected:
    tree_node_t<load_t> *parent_;
    load_t load_;

  public:
    AbstractTreeNode(load_t load) : parent_(nullptr), load_(load) {}

    tree_node_t<load_t> *parent() const { return parent_; }
    load_t &load() { return load_; }

    void setParent(tree_node_t<load_t> *parent) { parent_ = parent; }

    static tree_node_ptr_t<load_t> createNode(load_t load) { return std::make_shared<tree_node_t<load_t>>(load); }

    size_t find(load_t load) const {
        auto it = find_if(tree_children_t<load_t>::begin(), tree_children_t<load_t>::end(),
                          [=](tree_node_ptr_t<load_t> node) { return node->load == load; });
        return it == tree_children_t<load_t>::end() ? -1 : it - tree_children_t<load_t>::begin();
    }

    template <typename func_t> size_t find(func_t cmp) const {
        auto it = find_if(tree_children_t<load_t>::begin(), tree_children_t<load_t>::end(), cmp);
        return it == tree_children_t<load_t>::end() ? -1 : it - tree_children_t<load_t>::begin();
    }

    void reverseChildren() { reverse(tree_children_t<load_t>::begin(), tree_children_t<load_t>::end()); }

    tree_node_ptr_t<load_t> operator[](size_t index) const {
        tree_node_ptr_t<load_t> child = this->at(index);
        return child;
    }

    tree_node_ptr_t<load_t> childAt(size_t index) const {
        tree_node_ptr_t<load_t> child = this->at(index);
        return child;
    }

    tree_node_t<load_t> &operator<<(const tree_node_ptr_t<load_t> node) {
        node->parent = this;
        this->push_back(node);
        return static_cast<tree_node_t<load_t> &>(*this);
    }

    size_t size() const { return tree_children_t<load_t>::size(); }

    virtual std::string toString() const { return ""; }

    template <typename func_t> void foreach (func_t f) const {
        auto nodeF = [=](typename tree_children_t<load_t>::const_reference ref) { f(*ref); };
        for_each(tree_children_t<load_t>::begin(), tree_children_t<load_t>::end(), nodeF);
    }

    template <typename func_t> void traverse(func_t f) const {
        f(*this);
        foreach ([=](tree_node_t<load_t> &ref) { ref.traverse(f); })
            ;
    }

    template <typename func_t> void traverse(func_t f, size_t &level, size_t &index) {
        tree_node_t<load_t> &self = *this;
        f(self);
        level++;
        size_t tmpIdx = index++;
        index = 0;
        foreach ([&](tree_node_t<load_t> &ref) {
            ref.traverse(f, level, index);
            index++;
        })
            ;
        level--;
        index = tmpIdx;
    }

    template <typename func_t> void postorder(func_t f) const {
        foreach ([=](tree_node_t<load_t> &ref) { ref.postorder(f); })
            ;
        f(*this);
    }

    template <typename func_t> void postorder(func_t f, size_t &level, size_t &index) {
        tree_node_t<load_t> &self = *this;
        level++;
        size_t tmpIdx = index++;
        index = 0;
        foreach ([&](tree_node_t<load_t> &ref) {
            ref.postorder(f, level, index);
            index++;
        })
            ;
        level--;
        index = tmpIdx;
        f(self);
    }

    void dumpTree(std::ostream &os) {
        std::vector<bool> visible;
        size_t depth = 0;
        size_t index = 0;
        traverse(
            [&](tree_node_t<load_t> &node) {
                bool isLast = false;
                if (visible.size() <= depth)
                    visible.push_back(true);
                if (depth > 0) {
                    if (node.parent_ == nullptr) {
                        warn << "DumpTree: Node <" << node.toString() << "> has no parent!" << std::endl;
                    } else if (index == node.parent_->size() - 1) {
                        isLast = true;
                        visible[depth - 1] = false;
                    }
                }
                auto getHead = [&]() -> std::string {
                    size_t i = 0;
                    std::string ret;
                    while (i < depth - 1) {
                        if (visible[i])
                            ret += "|  ";
                        else
                            ret += "   ";
                        i++;
                    }
                    if (depth > 0) {
                        if (isLast)
                            ret += "\\-";
                        else
                            ret += "|-";
                    }
                    return ret;
                };
                os << getHead();
                os << node.toString();
                os << std::endl;
                if (depth > 0)
                    for (size_t i = depth; i < visible.size(); i++)
                        visible[i] = true;
            },
            depth, index);
        return;
    }

    void print(std::ostream &os = std::cout) { this->dumpTree(os); }
};
