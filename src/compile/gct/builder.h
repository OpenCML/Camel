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
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <set>
#include <unordered_set>
#include <iostream>
#include <regex>
#include <string>

#include "camel/common/scope.h"
#include "camel/compile/gct.h"
#include "camel/core/context/context.h"
#include "camel/core/error/abort.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/module/module.h"
#include "camel/parse/ast.h"

namespace camel::compile::gct {

using FunctionType = camel::core::type::FunctionType;
using void_ptr_t   = void *;

class Builder {
  public:
    Builder(
        const camel::core::context::context_ptr_t &context,
        const camel::core::module::module_ptr_t &module)
        : context_(context), module_(module) {
        typeScope_ = std::make_shared<Scope<Reference, Type *>>();
    };
    virtual ~Builder() = default;

    node_ptr_t build(AST::node_ptr_t node, camel::core::error::diagnostics_ptr_t diags) {
        idIndex_ = 0;
        diags_   = diags;
        initInnerTypes();
        try {
            root_ = visitModule(node);
        } catch (camel::core::error::Diagnostic &d) {
            diags_->add(std::move(d));
            root_ = nullptr;
        }
        return root_;
    }

  private:
    node_ptr_t root_;
    size_t idIndex_ = 0;
    scope_ptr_t<Reference, Type *> typeScope_;
    // Type names whose declaration (or whose import) failed: a use of one is a consequence of
    // that error and is not reported as an unresolved type of its own. When an import of a
    // whole module failed, which names it would have brought is unknown, and no unresolved
    // type is reported at all.
    std::unordered_set<std::string> poisonedTypes_;
    bool wholeImportFailed_ = false;
    std::unordered_map<void *, FunctionType *> funcDecls_;

    camel::core::context::context_ptr_t context_;
    camel::core::module::module_ptr_t module_;
    camel::core::error::diagnostics_ptr_t diags_;

    void initInnerTypes();

    std::pair<node_ptr_t, data_ptr_t> makeRefData(const node_ptr_t &expr);
    std::pair<data_ptr_t, bool> extractData(const node_ptr_t &node, node_ptr_t &execNode);
    std::pair<data_ptr_t, bool>
    extractData(const node_ptr_t &node, node_ptr_t &execNode, bool &dangling);

    // Type variables and type functions (design principles 2.9; camel/core/type/typevar.h).
    // `typeas T` binds a variable only among the parameters of a function declaration: the
    // builder notes that the function type it is about to visit is such a signature
    // (declSignature_, set from funcDeclPending_), allows binding while it visits the
    // parameters (typeasAllowed_), and records what was bound (declaredTypeVars_) so the
    // declaration can be checked. A type function may only appear as the base of
    // `Name<...>` (typeFunctionBase_).
    bool funcDeclPending_  = false;
    bool declSignature_    = false;
    bool typeasAllowed_    = false;
    bool inDeclReturnType_ = false;
    bool typeFunctionBase_ = false;
    std::vector<std::string> declaredTypeVars_;

    // Where the value of what is being lowered goes, as far as the warning
    // ExitInUnusedBranch needs it. A `return` / `throw` in a branch arm ends the arm and
    // gives the value of the `if` / `match` (an arm is a graph of its own); when nothing
    // takes that value the statement has no effect at all, and the program most likely
    // meant to leave the function.
    struct ValueUse {
        // The `if` / `match` about to be lowered that is a statement whose value nothing
        // takes (not the last statement of its block, or the last one of a block whose own
        // value nothing takes).
        const AST::Node *unusedCtrl = nullptr;
        // The expression of the expression statement about to be lowered (an `if` without
        // `else` is allowed there only).
        const AST::Node *stmtExpr = nullptr;
        // Set for the block about to be lowered: nothing takes its value.
        bool blockUnused = false;
        // The unused `if` / `match` whose arm is being lowered: exits written here are its.
        AST::node_ptr_t unusedArmOf = nullptr;
    };
    ValueUse valueUse_;
    // Restores valueUse_ when a construct with bodies of its own is left, also by an error.
    struct ValueUseScope {
        Builder &self;
        ValueUse saved;
        explicit ValueUseScope(Builder &b) : self(b), saved(b.valueUse_) {}
        ~ValueUseScope() { self.valueUse_ = saved; }
    };
    std::set<std::pair<size_t, size_t>> reportedExits_;
    // The next block lowered is an arm of `ctrl`, whose value is unused or not.
    void enterArmOf(const AST::node_ptr_t &ctrl, bool valueUnused);
    void reportExitInUnusedArm(const AST::node_ptr_t &exit, bool isThrow);

    void pushScope() { typeScope_ = std::make_shared<Scope<Reference, Type *>>(typeScope_); }
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
    node_ptr_t visitUsingNamespaceDecl(const AST::node_ptr_t &ast);
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
    node_ptr_t visitArrayData(const AST::node_ptr_t &ast);
    node_ptr_t visitStructData(const AST::node_ptr_t &ast);
    node_ptr_t visitTupleData(const AST::node_ptr_t &ast);
    node_ptr_t visitFuncData(const AST::node_ptr_t &ast);
    node_ptr_t visitRefData(const AST::node_ptr_t &ast);

    // ast/type.h
    Type *visitType(const AST::node_ptr_t &ast);
    Type *visitNullableType(const AST::node_ptr_t &ast);
    Type *visitTypeExpr(const AST::node_ptr_t &ast);
    Type *visitArrayType(const AST::node_ptr_t &ast);
    Type *visitStructType(const AST::node_ptr_t &ast);
    Type *visitTupleType(const AST::node_ptr_t &ast);
    Type *visitFuncType(const AST::node_ptr_t &ast);
    Type *visitUnitType(const AST::node_ptr_t &ast);
    Type *visitInferType(const AST::node_ptr_t &ast);
    Type *visitDataType(const AST::node_ptr_t &ast);
    Type *visitRefType(const AST::node_ptr_t &ast);
    Type *visitSpecType(const AST::node_ptr_t &ast);
    /// `[...] as Map<K, V>`: an error when two constant keys of the literal are equal.
    void rejectDuplicateLiteralKeys(
        const data_ptr_t &data, Type *targetType, const AST::node_ptr_t &ast);
};

} // namespace camel::compile::gct
