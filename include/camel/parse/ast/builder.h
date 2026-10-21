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
 * Created: Oct. 12, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Builds the abstract syntax tree from the concrete syntax tree of the hand-written parser.
 *
 * One function per production, as the grammar names them; where the tree has no node for a
 * production (the levels that only pass on, see node_kinds.def) the function that would have
 * handled it is one of the four dispatchers: visitDecl, visitStmt, visitExpr, visitType.
 *
 * The tree built here is, node for node, the one the ANTLR-based builder produced: same loads,
 * same token ranges, same origins and semantic bundles. The later stages did not change.
 */

#pragma once

#include <string>
#include <vector>

#include "camel/core/error/diagnostics.h"
#include "camel/core/source/manager.h"
#include "camel/parse/ast.h"
#include "camel/parse/ast/value.h"
#include "camel/parse/syntax/cst.h"

namespace camel::parse::ast {

class Builder {
  public:
    Builder() = default;

    node_ptr_t build(const syntax::Tree &tree, camel::core::error::diagnostics_ptr_t diags) {
        diags_         = diags;
        sourceContext_ = diags ? diags->sourceContext() : nullptr;
        sourceFileId_  = camel::source::kInvalidSourceFileId;
        if (sourceContext_ && diags) {
            auto fileId = sourceContext_->findFileId(diags->modulePath());
            if (fileId.has_value()) {
                sourceFileId_ = *fileId;
            }
        }
        if (sourceContext_) {
            // One span and one origin per AST node, a few more for the anchors of names.
            sourceContext_->reserveOrigins(tree.nodeCount() + tree.nodeCount() / 2);
        }
        root_ = nullptr;
        namespaceStack_.clear();
        visitProgram(syntax::cst::Program(tree.root()));
        return root_;
    }

  private:
    node_ptr_t root_;
    camel::core::error::diagnostics_ptr_t diags_;
    camel::source::source_context_ptr_t sourceContext_;
    camel::source::source_file_id_t sourceFileId_ = camel::source::kInvalidSourceFileId;

    std::shared_ptr<ModuleLoad> module_ = std::make_shared<ModuleLoad>();
    std::vector<std::shared_ptr<ImportLoad>> imports_;
    std::shared_ptr<ExportLoad> export_ = std::make_shared<ExportLoad>();
    std::vector<std::string> namespaceStack_;

    Reference qualifyRef(const Reference &ref) const;
    void pushNamespace(const Reference &ref);
    void popNamespace(size_t previousSize);

    // Dispatch on the kind of a node.
    Value visitDecl(syntax::Node context);
    Value visitStmt(syntax::Node context);
    Value visitBlockExpr(syntax::Node context);
    Value visitExpr(syntax::Node context);
    Value visitType(syntax::Node context);

    Value visitProgram(syntax::cst::Program context);
    Value visitStmtList(syntax::cst::StmtList context);
    Value visitModuleDecl(syntax::cst::ModuleDecl context);
    Value visitImportDecl(syntax::cst::ImportDecl context);
    Value visitExportDecl(syntax::cst::ExportDecl context);
    Value visitUsingNamespaceDecl(syntax::cst::UsingNamespaceDecl context);
    Value visitRuleDecl(syntax::cst::RuleDecl context);
    /// The import of the module `rewrite` that rule declarations need was made.
    bool ruleImportAdded_ = false;
    Value visitBlockStmt(syntax::cst::BlockStmt context);
    Value visitStmtBlock(syntax::cst::StmtBlock context);
    Value visitFuncData(syntax::cst::FuncData context);
    Value visitFuncAnno(syntax::cst::FuncAnno context);
    /// The permissions of a `@numeric(...)` annotation; nullopt for a decorator (`@name`,
    /// `@name<args>`). Any other annotation with names in parentheses, an unknown permission
    /// and an empty list are reported (SemanticDiag::InvalidAnnotation) and grant nothing.
    std::optional<uint8_t> numericAnnotationOf(syntax::cst::FuncAnno context);
    Value visitFuncDecl(syntax::cst::FuncDecl context);
    Value visitNamespaceDecl(syntax::cst::NamespaceDecl context);
    /// `topLevel`: a declaration of the module or of a namespace block, whose names take the
    /// path of the enclosing namespaces (`namespace Config { let limit = 10 }` declares
    /// `Config::limit`, design principles 10.2); a local declaration does not.
    Value visitDataDecl(syntax::cst::DataDecl context, bool topLevel = false);
    Value visitTypeDecl(syntax::cst::TypeDecl context);
    Value visitRetStmt(syntax::cst::RetStmt context);
    Value visitSpreadValue(syntax::cst::SpreadValue context);
    Value visitKeyTypePair(syntax::cst::KeyTypePair context);
    Value visitKeyValuePair(syntax::cst::KeyValuePair context);
    Value visitKeyParamPair(syntax::cst::KeyParamPair context);
    Value visitDataList(syntax::cst::DataList context);
    Value visitIdentList(syntax::cst::IdentList context);
    Value visitIndexValues(syntax::cst::IndexValues context);
    Value visitPairedValues(syntax::cst::PairedValues context);
    Value visitPairedParams(syntax::cst::PairedParams context);
    Value visitArgumentList(syntax::cst::ArgumentList context);
    Value visitIndices(syntax::cst::Indices context);
    Value visitParentParams(syntax::cst::ParentParams context);
    Value visitParentArgues(syntax::cst::ParentArgues context);
    Value visitAngledParams(syntax::cst::AngledParams context);
    Value visitAngledValues(syntax::cst::AngledValues context);
    Value visitPattern(syntax::cst::Pattern context);
    Value visitMatchCase(syntax::cst::MatchCase context);
    /// IfExpr, MatchExpr, TryExpr.
    Value visitCtrlExpr(syntax::Node context);
    Value visitWaitExpr(syntax::cst::WaitExpr context);
    Value visitLoopExpr(syntax::cst::LoopExpr context);
    Value visitAssignExpr(syntax::cst::AssignExpr context);
    Value visitLogicalOrExpr(syntax::cst::LogicalOrExpr context);
    Value visitLogicalAndExpr(syntax::cst::LogicalAndExpr context);
    /// OrExpr (`a | b`) and AndExpr (`a & b`).
    Value visitEagerLogicExpr(syntax::Node context);
    Value visitEqualityExpr(syntax::cst::EqualityExpr context);
    Value visitRelationalExpr(syntax::cst::RelationalExpr context);
    Value visitAdditiveExpr(syntax::cst::AdditiveExpr context);
    Value visitMultiplicativeExpr(syntax::cst::MultiplicativeExpr context);
    Value visitNullableExpr(syntax::cst::NullableExpr context);
    /// CastExpr (`e as T`, `e is T`) and UnaryExpr (`!e`, `-e`, `~e`).
    Value visitUnaryExpr(syntax::Node context);
    Value visitLinkExpr(syntax::cst::LinkExpr context);
    Value visitCompExpr(syntax::cst::CompExpr context);
    Value visitAnnoExpr(syntax::cst::AnnoExpr context);
    Value visitAccessExpr(syntax::cst::AccessExpr context);
    /// `lhs.member` (a field or tuple element), located by `range`.
    node_ptr_t
    makeMemberAccess(node_ptr_t lhs, syntax::Child dot, syntax::Child member, syntax::Node range);
    Value visitStructData(syntax::cst::StructData context);
    Value visitArrayData(syntax::cst::ArrayData context);
    Value visitTupleData(syntax::cst::TupleData context);
    Value visitRefData(syntax::cst::RefData context);
    Value visitConstasData(syntax::cst::ConstasData context);
    Value visitLiteralData(syntax::cst::Literal context);
    /// A formatted string (`FString`): `'...'->format<...>`, built here as that same tree.
    Value visitFStringData(syntax::cst::Literal context);
    Value visitLiteral(syntax::cst::Literal context);
    Value visitTypeExpr(syntax::cst::TypeExpr context);
    Value visitInterType(syntax::cst::InterType context);
    Value visitDiffType(syntax::cst::DiffType context);
    Value visitKeyUnionDiffType(syntax::cst::KeyUnionDiffType context);
    Value visitKeyInterType(syntax::cst::KeyInterType context);
    Value visitTypeUnit(syntax::cst::TypeUnit context);
    Value visitArrayType(syntax::cst::ArrayType context);
    Value visitConstData(syntax::cst::ConstData context);
    Value visitSpecType(syntax::cst::SpecType context);
    Value visitRefType(syntax::cst::RefType context);
    Value visitTypeOfType(syntax::cst::TypeOfType context);
    Value visitTypeAsType(syntax::cst::TypeAsType context);
    Value visitStructType(syntax::cst::StructType context);
    Value visitEnumType(syntax::cst::EnumType context);
    Value visitEnumPattern(syntax::cst::Pattern context, syntax::cst::IdentRef ref);
    Value visitTypeList(syntax::cst::TypeList context);
    Value visitTupleType(syntax::cst::TupleType context);
    Value visitFuncType(syntax::cst::FuncType context);
    Value visitIdentDef(syntax::cst::IdentDef context);
    Value visitIdentRef(syntax::cst::IdentRef context);
};

} // namespace camel::parse::ast
