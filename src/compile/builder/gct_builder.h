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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Jul. 09, 2025
 * Updated: Jul. 09, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <iostream>
#include <regex>
#include <string>

#include "common/scope.h"
#include "compile/gct.h"
#include "core/context/context.h"
#include "core/module/module.h"
#include "error/abort.h"
#include "error/diagnostics/diagnostics.h"
#include "parse/ast/ast.h"

namespace GraphConstructTree {
using void_ptr_t = void *;

class Builder {
  public:
    Builder(const context_ptr_t &context, const module_ptr_t &module)
        : context_(context), module_(module) {
        typeScope_ = std::make_shared<Scope<Reference, type_ptr_t>>();
    };
    virtual ~Builder() = default;

    node_ptr_t build(AST::node_ptr_t node, diagnostics_ptr_t diags) {
        idIndex_ = 0;
        diags_ = diags;
        initInnerTypes();
        root_ = visitModule(node);
        return root_;
    }

  private:
    node_ptr_t root_;
    size_t idIndex_ = 0;
    scope_ptr_t<Reference, type_ptr_t> typeScope_;
    std::unordered_map<void *, func_type_ptr_t> funcDecls_;

    context_ptr_t context_;
    module_ptr_t module_;
    diagnostics_ptr_t diags_;

    void initInnerTypes();

    std::pair<node_ptr_t, data_ptr_t> makeRefData(const node_ptr_t &expr);
    std::pair<data_ptr_t, bool> extractData(const node_ptr_t &node, node_ptr_t &execNode);
    std::pair<data_ptr_t, bool>
    extractData(const node_ptr_t &node, node_ptr_t &execNode, bool &dangling);

    void pushScope() { typeScope_ = std::make_shared<Scope<Reference, type_ptr_t>>(typeScope_); }
    void popScope() { typeScope_ = typeScope_->outer(); } // TODO: Shall we free the scope?

    // ast/base.h
    node_ptr_t visitModule(const AST::node_ptr_t &ast);
    void_ptr_t visitImport(const AST::node_ptr_t &ast);
    node_ptr_t visitExport(const AST::node_ptr_t &ast);

    // ast/stmt.h
    node_ptr_t visitStmt(const AST::node_ptr_t &ast);
    node_ptr_t visitDataDecl(const AST::node_ptr_t &ast);
    node_ptr_t visitFuncDecl(const AST::node_ptr_t &ast);
    node_ptr_t visitTypeDecl(const AST::node_ptr_t &ast);
    node_ptr_t visitNameDecl(const AST::node_ptr_t &ast);
    node_ptr_t visitExprStmt(const AST::node_ptr_t &ast);
    node_ptr_t visitExitStmt(const AST::node_ptr_t &ast);
    node_ptr_t visitStmtBlock(const AST::node_ptr_t &ast);

    // ast/data.h
    node_ptr_t visitData(const AST::node_ptr_t &ast);
    node_ptr_t visitUnaryExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitBinaryExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitReservedExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitIfExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitMatchExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitTryExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitLiteral(const AST::node_ptr_t &ast);
    node_ptr_t visitListData(const AST::node_ptr_t &ast);
    node_ptr_t visitDictData(const AST::node_ptr_t &ast);
    node_ptr_t visitTupleData(const AST::node_ptr_t &ast);
    node_ptr_t visitFuncData(const AST::node_ptr_t &ast);
    node_ptr_t visitRefData(const AST::node_ptr_t &ast);

    // ast/type.h
    type_ptr_t visitType(const AST::node_ptr_t &ast);
    type_ptr_t visitNullableType(const AST::node_ptr_t &ast);
    type_ptr_t visitTypeExpr(const AST::node_ptr_t &ast);
    type_ptr_t visitListType(const AST::node_ptr_t &ast);
    type_ptr_t visitDictType(const AST::node_ptr_t &ast);
    type_ptr_t visitTupleType(const AST::node_ptr_t &ast);
    type_ptr_t visitFuncType(const AST::node_ptr_t &ast);
    type_ptr_t visitUnitType(const AST::node_ptr_t &ast);
    type_ptr_t visitInferType(const AST::node_ptr_t &ast);
    type_ptr_t visitDataType(const AST::node_ptr_t &ast);
    type_ptr_t visitRefType(const AST::node_ptr_t &ast);
};

} // namespace GraphConstructTree
