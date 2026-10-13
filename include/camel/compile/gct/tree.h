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
 * Created: Oct. 13, 2026
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Tree: the graph construction tree of one module, and the only place its nodes are made.
 *
 * The GCT builder makes a Tree from the AST of a module; the module keeps it (the GIR
 * builder reads it, and generic functions are lowered again from it at every instantiation,
 * also from other modules). Nodes are made with the make* functions and read through the
 * accessors of Node; nothing outside this directory names the representation behind them.
 */

#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "camel/common/ref.h"
#include "camel/compile/gct/node.h"

namespace camel::compile::gct {

class Tree {
  public:
    Tree()                        = default;
    Tree(const Tree &)            = delete;
    Tree &operator=(const Tree &) = delete;

    /// The node of the module: an EXEC with its declarations and statements. Null until the
    /// builder has set it.
    const node_ptr_t &root() const { return root_; }
    void setRoot(const node_ptr_t &root) { root_ = root; }

    // One function per kind of node. A new node has no children, no origin and no parent.
    node_ptr_t makeData(const data_ptr_t &data);
    node_ptr_t makeType(Type *type, ImplMark impl);
    node_ptr_t makeDecl(const std::string &name, bool isFunc);
    node_ptr_t makeFunc(const std::string &name);
    node_ptr_t makeNRef(const std::string &name);
    node_ptr_t makeDRef(const std::string &name);
    node_ptr_t makeVari();
    node_ptr_t makeWait();
    node_ptr_t makeLink(size_t argCount);
    node_ptr_t makeWith(size_t argCount);
    node_ptr_t makeAccs(size_t index);
    node_ptr_t makeAccs(const std::string &key);
    node_ptr_t makeBrch(BranchKind kind = BranchKind::Cond);
    node_ptr_t makeCase(CaseType type, const std::string &binding = {});
    node_ptr_t makeCast(Type *targetType, bool annotation = false);
    node_ptr_t makeAnno();
    node_ptr_t makeExit(ExitType type = ExitType::Return);
    node_ptr_t makeExec(bool synced = false, bool operands = false);
    node_ptr_t makeExpt(const std::vector<Reference> &exports);

    /// A copy of `node` and of everything below it, with the origins of the original and
    /// without a parent: for a subtree that is needed in two places.
    node_ptr_t clone(const node_ptr_t &node);

    /// The tree as indented text, one node per line (`camel inspect --gct`).
    void print(std::ostream &os) const;

  private:
    node_ptr_t root_;
};

} // namespace camel::compile::gct
