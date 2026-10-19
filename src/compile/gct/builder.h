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
#include <memory>
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

    /// The tree of the module whose AST is `node`. Null when the module could not be lowered
    /// at all; errors that leave a tree behind are in `diags`.
    std::unique_ptr<Tree> build(AST::node_ptr_t node, camel::core::error::diagnostics_ptr_t diags) {
        idIndex_ = 0;
        diags_   = diags;
        tree_    = std::make_unique<Tree>();
        initInnerTypes();
        try {
            tree_->setRoot(visitModule(node));
        } catch (camel::core::error::Diagnostic &d) {
            diags_->add(std::move(d));
            tree_.reset();
        }
        return std::move(tree_);
    }

  private:
    // The tree being built: every node of the module is made by it.
    std::unique_ptr<Tree> tree_;
    size_t idIndex_ = 0;
    scope_ptr_t<Reference, Type *> typeScope_;
    // Type names whose declaration (or whose import) failed: a use of one is a consequence of
    // that error and is not reported as an unresolved type of its own. When an import of a
    // whole module failed, which names it would have brought is unknown, and no unresolved
    // type is reported at all.
    std::unordered_set<std::string> poisonedTypes_;
    bool wholeImportFailed_ = false;
    // Namespaces (design principles 2.12). The namespace path of the top-level declaration
    // being lowered (`A::B` for `A::B::f`): a type name written in it is looked up there, then
    // in the enclosing namespaces, then at the top level. The `using namespace` in force, one
    // frame per block (the module's, a namespace block's, a block of a function body). The
    // namespaces the module declares (prefixes of its qualified names).
    std::vector<std::string> namespacePath_;
    std::vector<std::vector<Reference>> usingFrames_;
    std::unordered_set<std::string> ownNamespaces_;
    // The type a name written in a type position stands for, by the one rule of precedence:
    // the module's types (from the innermost namespace out; the module's own name may qualify
    // them), then imported by name, then through a `using namespace`, then built in. Reports
    // and throws when there is none, or when two `using` give different types (AmbiguousName).
    Type *lookupType(const Reference &ref, const AST::node_ptr_t &ast);
    /// `ref`, written at `ast`, names the type `remote` of the module `source`, which is
    /// not declared yet. When that declaration is in progress further up, the types form a
    /// cycle across modules: reported with the cycle (ModuleTypeCycle; design principles 2.13).
    /// When it is on a cycle reported already, the failure follows from that report and is not
    /// reported again. Either way BuildAbortException; otherwise nothing happens.
    void rejectTypeCycle(
        const std::shared_ptr<camel::core::module::Module> &source, const Reference &remote,
        const Reference &ref, const AST::node_ptr_t &ast);
    // Whether `ref` names a namespace a `using namespace` can open: one of the module (also
    // relative to the namespace being lowered) or one bound by an import.
    bool usableNamespace(const Reference &ref) const;
    // The names the module exports (visitModule), and the modules its imports bound, with the
    // import that bound each (compileImportedModules).
    std::unordered_set<std::string> exportedRefs_;
    std::vector<std::pair<AST::node_ptr_t, camel::core::module::module_ptr_t>> importedModules_;
    // Compiles the imported modules written in Camel as far as their declarations; one in
    // error is reported at its import (ImportedModuleHasErrors).
    void compileImportedModules();
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
    // The numeric permissions in force where the builder is (design principles 2.13): the
    // union of the `@numeric(...)` grants of the blocks and functions it is inside. Every EXEC
    // is made with them (makeExec).
    uint8_t numericPermissions_ = 0;
    node_ptr_t makeExec(bool synced = false, bool operands = false);

    bool funcDeclPending_  = false;
    bool declSignature_    = false;
    // The function type visited next is the signature of a function being defined (a
    // FuncData): it is built open (camel/core/type/composite/func.h), so that the graph
    // builder can set the result type the body decides when none is written. Every other
    // function type is interned.
    bool openSignature_    = false;
    bool typeasAllowed_    = false;
    bool inDeclReturnType_ = false;
    bool typeFunctionBase_ = false;
    std::vector<std::string> declaredTypeVars_;

    // `any` is not a type a program can write (design principles 2.11, 2.12): the name is
    // not in the type scope, and visitRefType reports it with the form recommended for the
    // position it was written in. The visitors of those positions set this around the
    // type they visit (TypePositionGuard in builder.cpp). A type nested in another one is
    // reported for the innermost of those positions: `any` in `g: (x: int) => any` is the
    // return type of a function type, in `xs: any[]` the type of a parameter.
    enum class TypePosition {
        Other,
        Parameter,
        WithParameter,
        ReturnType,
        CastTarget,
        Annotation,
        TypeDeclaration,
    };
    TypePosition typePosition_ = TypePosition::Other;
    /// Sets typePosition_ while the type of one position is visited.
    struct TypePositionGuard {
        TypePositionGuard(Builder &builder, TypePosition position);
        ~TypePositionGuard();
        Builder &builder;
        TypePosition saved;
    };
    /// Reports `any` written as a type (AnyNotAllowed) and aborts the declaration.
    [[noreturn]] void reportAnyType(const AST::node_ptr_t &ast);

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
    /// `func operator <symbol>(...)` (design principles 2.12): reports, and aborts, an operator
    /// that cannot be overloaded (OperatorNotOverloadable) and a declaration that does not fit
    /// its operator: the number of parameters, a with-parameter or a var parameter
    /// (OperatorOverloadInvalid). `funcData` is the declaration's FuncData, `decl` the whole.
    void checkOperatorDeclaration(
        const std::string &symbol, const AST::node_ptr_t &funcData, const AST::node_ptr_t &decl);
    node_ptr_t visitTypeDecl(const AST::node_ptr_t &ast);
    node_ptr_t visitUsingNamespaceDecl(const AST::node_ptr_t &ast);
    node_ptr_t visitExprStmt(const AST::node_ptr_t &ast);
    node_ptr_t visitExitStmt(const AST::node_ptr_t &ast);
    /// A block; one written with decorators is lowered by visitDecoratedBlock unless
    /// `decorators` is false (it lowers the block itself).
    node_ptr_t visitStmtBlock(const AST::node_ptr_t &ast, bool decorators = true);
    /// `@d { ... }` (language reference 7.6): LINK(0)[FUNC (decorated block)[TYPE, EXEC,
    /// ANNO...]], the block as a function of no parameters called where it is written. The
    /// EXEC is the block lowered as without decorators (its scope, its exits, its value).
    node_ptr_t visitDecoratedBlock(const AST::node_ptr_t &ast);

    // ast/data.h
    node_ptr_t visitData(const AST::node_ptr_t &ast);
    node_ptr_t visitUnaryExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitBinaryExpr(const AST::node_ptr_t &ast);
    /// `a && b` / `a || b`: the branch they stand for (design principles 2.11).
    node_ptr_t visitLogicalExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitReservedExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitIfExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitLoopExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitMatchExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitTryExpr(const AST::node_ptr_t &ast);
    node_ptr_t visitLiteral(const AST::node_ptr_t &ast);
    node_ptr_t visitArrayData(const AST::node_ptr_t &ast);
    /// `[e for x in xs if c]`: `xs->filter<(x) => c>->map<(x) => e>` (design principles
    /// 2.10), built as those two calls are. The type of `x` is the element type of `xs`,
    /// which only the GIR builder knows: each of the two functions is written with a type
    /// variable for it and is instantiated where `map` / `filter` receive it.
    node_ptr_t visitComprehension(const AST::node_ptr_t &ast);
    /// `ast` lowered where a value of type `type` is expected. An array literal whose
    /// elements are to be put into `T?` / `T!` is built with the conversions in place, and
    /// `entered` is set; anything else is visitData(ast).
    node_ptr_t visitDataInto(const AST::node_ptr_t &ast, Type *type, bool &entered);
    /// The initializer `value` of a declaration written with the type `type` (`let x: T = e`,
    /// lowered from `ast`). A literal is converted to the type here; any other expression is
    /// put under an annotation CAST, which the GIR builder resolves once the type of the value
    /// is known: it puts the value into a declared `T?` / `T!`, says which function a
    /// declared function type means, and otherwise checks that the value has the declared
    /// type (a value of that very type passes; nothing is converted).
    node_ptr_t declaredValue(const node_ptr_t &value, Type *type, const AST::node_ptr_t &ast);
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
