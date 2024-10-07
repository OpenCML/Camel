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
 * Updated: Oct. 7, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include <fstream>
#include <iostream>

#include "ast.h"
#include "common/ast.h"
#include "common/context.h"
#include "common/entity.h"
#include "common/function.h"
#include "common/graph.h"
#include "common/scope.h"
#include "utils/log.h"

inline void _dumpGIR() {
    std::ifstream file("D:\\Projects\\Camel\\demo\\feat\\gir.txt");

    if (file) {
        std::string content((std::istreambuf_iterator<char>(file)), (std::istreambuf_iterator<char>()));
        std::cout << content << std::endl;
        file.close();
    } else {
        error << "Error dumping GIR" << std::endl;
    }
}

namespace GraphIR {

namespace ast = AbstractSyntaxTree;

using node_scope_t = Scope<std::string, node_ptr_t>;
using node_scope_ptr_t = scope_ptr_t<std::string, node_ptr_t>;
using func_scope_t = Scope<std::string, func_ptr_t>;
using func_scope_ptr_t = scope_ptr_t<std::string, func_ptr_t>;

using void_ptr_t = void *;

class Constructor {
  public:
    Constructor(context_ptr_t &context) : context_(context) {}

    graph_ptr_t construct(ast::node_ptr_t &ast) { return nullptr; }

  private:
    context_ptr_t context_;

    node_scope_ptr_t nodeScope_; // TODO: init required
    entity_scope_ptr_t entityScope_;
    func_scope_ptr_t funcScope_;

    void pushScope(const std::string &name) {
        nodeScope_ = node_scope_t::push(nodeScope_);
        entityScope_ = entity_scope_t::push(entityScope_);
        funcScope_ = func_scope_t::push(funcScope_);
    }

    void popScope() {
        nodeScope_ = node_scope_t::pop(nodeScope_);
        entityScope_ = entity_scope_t::pop(entityScope_);
        funcScope_ = func_scope_t::pop(funcScope_);
    }

    std::any visit(const ast::node_ptr_t &ast);

    node_ptr_t visitDataNode(const ast::node_ptr_t &ast);
    node_ptr_t visitVariNode(const ast::node_ptr_t &ast);
    type_ptr_t visitTypeNode(const ast::node_ptr_t &ast);
    node_ptr_t visitFuncNode(const ast::node_ptr_t &ast);
    void_ptr_t visitNRefNode(const ast::node_ptr_t &ast);
    node_ptr_t visitDRefNode(const ast::node_ptr_t &ast);
    node_ptr_t visitWaitNode(const ast::node_ptr_t &ast);
    node_ptr_t visitAnnoNode(const ast::node_ptr_t &ast);
    node_ptr_t visitLinkNode(const ast::node_ptr_t &ast);
    node_ptr_t visitWithNode(const ast::node_ptr_t &ast);
    node_ptr_t visitRetnNode(const ast::node_ptr_t &ast);
    node_ptr_t visitExecNode(const ast::node_ptr_t &ast);
    void_ptr_t visitFromNode(const ast::node_ptr_t &ast);
};
} // namespace GraphIR
