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
 * Created: May. 29, 2024
 * Updated: Oct. 22, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include <fstream>
#include <iostream>

#include "ast.h"
#include "common/ast.h"
#include "common/context.h"
#include "common/graph.h"

namespace GraphIR {

namespace ast = AbstractSyntaxTree;

using void_ptr_t = void *;

class Constructor {
  public:
    Constructor(context_ptr_t &context) : context_(context) {}

    graph_ptr_t construct(ast::node_ptr_t &ast) {
        visit(ast);
        return context_->currGraph();
    }

  private:
    context_ptr_t context_;

    std::any visit(const ast::node_ptr_t &ast);

    node_ptr_t visitDataNode(const ast::node_ptr_t &ast);
    node_ptr_t visitVariNode(const ast::node_ptr_t &ast);
    type_ptr_t visitTypeNode(const ast::node_ptr_t &ast);
    node_ptr_t visitDeclNode(const ast::node_ptr_t &ast);
    node_ptr_t visitFuncNode(const ast::node_ptr_t &ast);
    void_ptr_t visitNRefNode(const ast::node_ptr_t &ast);
    node_ptr_t visitDRefNode(const ast::node_ptr_t &ast);
    node_ptr_t visitWaitNode(const ast::node_ptr_t &ast);
    node_ptr_t visitAnnoNode(const ast::node_ptr_t &ast);
    node_ptr_t visitLinkNode(const ast::node_ptr_t &ast);
    node_ptr_t visitWithNode(const ast::node_ptr_t &ast);
    void_ptr_t visitRetnNode(const ast::node_ptr_t &ast);
    node_ptr_t visitExecNode(const ast::node_ptr_t &ast);
    void_ptr_t visitFromNode(const ast::node_ptr_t &ast);
};
} // namespace GraphIR
