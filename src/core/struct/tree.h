/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Mar. 26, 2024
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

template <typename data_t> class AbstractTreeNode;

template <typename data_t> using tree_node_t = AbstractTreeNode<data_t>;

template <typename data_t> using tree_node_ptr_t = std::shared_ptr<AbstractTreeNode<data_t>>;

template <typename data_t> using tree_children_t = std::vector<tree_node_ptr_t<data_t>>;

template <typename data_t> class AbstractTreeNode : public tree_children_t<data_t> {
  public:
    tree_node_t<data_t> *parent;
    data_t data;

    AbstractTreeNode(data_t data) : parent(nullptr), data(data) {}

    static tree_node_ptr_t<data_t> createNode(data_t data) { return std::make_shared<tree_node_t<data_t>>(data); }

    size_t find(data_t data) const {
        auto it = find_if(tree_children_t<data_t>::begin(), tree_children_t<data_t>::end(),
                          [=](tree_node_ptr_t<data_t> node) { return node->data == data; });
        return it == tree_children_t<data_t>::end() ? -1 : it - tree_children_t<data_t>::begin();
    }

    template <typename func_t> size_t find(func_t cmp) const {
        auto it = find_if(tree_children_t<data_t>::begin(), tree_children_t<data_t>::end(), cmp);
        return it == tree_children_t<data_t>::end() ? -1 : it - tree_children_t<data_t>::begin();
    }

    void reverseChildren() { reverse(tree_children_t<data_t>::begin(), tree_children_t<data_t>::end()); }

    tree_node_t<data_t> &operator[](size_t index) const {
        const auto &child = this->at(index);
        return static_cast<tree_node_t<data_t> &>(*child);
    }

    tree_node_ptr_t<data_t> get_child_ptr(size_t index) const {
        tree_node_ptr_t<data_t> child = this->at(index);
        return child;
    }

    tree_node_t<data_t> &operator<<(const tree_node_ptr_t<data_t> node) {
        node->parent = this;
        this->push_back(node);
        return static_cast<tree_node_t<data_t> &>(*this);
    }

    size_t size() const { return tree_children_t<data_t>::size(); }

    virtual std::string toString() const { return ""; }

    template <typename func_t> void foreach (func_t f) const {
        auto nodeF = [=](tree_children_t<data_t>::const_reference ref) { f(*ref); };
        for_each(tree_children_t<data_t>::begin(), tree_children_t<data_t>::end(), nodeF);
    }

    template <typename func_t> void traverse(func_t f) const {
        f(*this);
        foreach ([=](tree_node_t<data_t> &ref) { ref.traverse(f); })
            ;
    }

    template <typename func_t> void traverse(func_t f, int &level, int &index) {
        tree_node_t<data_t> &self = *this;
        f(self);
        level++;
        int tmpIdx = index++;
        index = 0;
        foreach ([&](tree_node_t<data_t> &ref) {
            ref.traverse(f, level, index);
            index++;
        })
            ;
        level--;
        index = tmpIdx;
    }

    template <typename func_t> void postorder(func_t f) const {
        foreach ([=](tree_node_t<data_t> &ref) { ref.postorder(f); })
            ;
        f(*this);
    }

    template <typename func_t> void postorder(func_t f, int &level, int &index) {
        tree_node_t<data_t> &self = *this;
        level++;
        int tmpIdx = index++;
        index = 0;
        foreach ([&](tree_node_t<data_t> &ref) {
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
        int depth = 0;
        int index = 0;
        traverse(
            [&](tree_node_t<data_t> &node) {
                bool isLast = false;
                if (visible.size() <= depth)
                    visible.push_back(true);
                if (depth > 0) {
                    if (node.parent == nullptr) {
                        warn << "DumpTree: Node <" << node.toString() << "> has no parent!" << std::endl;
                    } else if (index == node.parent->size() - 1) {
                        isLast = true;
                        visible[depth - 1] = false;
                    }
                }
                auto getHead = [&]() -> std::string {
                    int i = 0;
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
                    for (int i = depth; i < visible.size(); i++)
                        visible[i] = true;
            },
            depth, index);
        return;
    }

    void print() { this->dumpTree(std::cout); }
};