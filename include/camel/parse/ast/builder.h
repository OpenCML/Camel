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
 * Updated: Oct. 12, 2026
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

#include <any>
#include <string>
#include <vector>

#include "camel/core/error/diagnostics.h"
#include "camel/core/source/manager.h"
#include "camel/parse/ast.h"
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
    std::any visitDecl(syntax::Node context);
    std::any visitStmt(syntax::Node context);
    std::any visitBlockExpr(syntax::Node context);
    std::any visitExpr(syntax::Node context);
    std::any visitType(syntax::Node context);

    std::any visitProgram(syntax::cst::Program context);
    std::any visitStmtList(syntax::cst::StmtList context);
    std::any visitModuleDecl(syntax::cst::ModuleDecl context);
    std::any visitImportDecl(syntax::cst::ImportDecl context);
    std::any visitExportDecl(syntax::cst::ExportDecl context);
    std::any visitUsingNamespaceDecl(syntax::cst::UsingNamespaceDecl context);
    std::any visitBlockStmt(syntax::cst::BlockStmt context);
    std::any visitStmtBlock(syntax::cst::StmtBlock context);
    std::any visitFuncData(syntax::cst::FuncData context);
    std::any visitFuncAnno(syntax::cst::FuncAnno context);
    std::any visitFuncDecl(syntax::cst::FuncDecl context);
    std::any visitNamespaceDecl(syntax::cst::NamespaceDecl context);
    std::any visitDataDecl(syntax::cst::DataDecl context);
    std::any visitTypeDecl(syntax::cst::TypeDecl context);
    std::any visitUseDecl(syntax::cst::UseDecl context);
    std::any visitRetStmt(syntax::cst::RetStmt context);
    std::any visitSpreadValue(syntax::cst::SpreadValue context);
    std::any visitKeyTypePair(syntax::cst::KeyTypePair context);
    std::any visitKeyValuePair(syntax::cst::KeyValuePair context);
    std::any visitKeyParamPair(syntax::cst::KeyParamPair context);
    std::any visitDataList(syntax::cst::DataList context);
    std::any visitIdentList(syntax::cst::IdentList context);
    std::any visitIndexValues(syntax::cst::IndexValues context);
    std::any visitPairedValues(syntax::cst::PairedValues context);
    std::any visitPairedParams(syntax::cst::PairedParams context);
    std::any visitArgumentList(syntax::cst::ArgumentList context);
    std::any visitIndices(syntax::cst::Indices context);
    std::any visitParentParams(syntax::cst::ParentParams context);
    std::any visitParentArgues(syntax::cst::ParentArgues context);
    std::any visitAngledParams(syntax::cst::AngledParams context);
    std::any visitAngledValues(syntax::cst::AngledValues context);
    std::any visitPattern(syntax::cst::Pattern context);
    std::any visitMatchCase(syntax::cst::MatchCase context);
    /// IfExpr, MatchExpr, TryExpr.
    std::any visitCtrlExpr(syntax::Node context);
    std::any visitWaitExpr(syntax::cst::WaitExpr context);
    std::any visitLoopExpr(syntax::cst::LoopExpr context);
    std::any visitAssignExpr(syntax::cst::AssignExpr context);
    std::any visitLogicalOrExpr(syntax::cst::LogicalOrExpr context);
    std::any visitLogicalAndExpr(syntax::cst::LogicalAndExpr context);
    std::any visitEqualityExpr(syntax::cst::EqualityExpr context);
    std::any visitRelationalExpr(syntax::cst::RelationalExpr context);
    std::any visitAdditiveExpr(syntax::cst::AdditiveExpr context);
    std::any visitMultiplicativeExpr(syntax::cst::MultiplicativeExpr context);
    std::any visitNullableExpr(syntax::cst::NullableExpr context);
    /// CastExpr (`e as T`, `e is T`) and UnaryExpr (`!e`, `-e`, `~e`).
    std::any visitUnaryExpr(syntax::Node context);
    std::any visitLinkExpr(syntax::cst::LinkExpr context);
    std::any visitCompExpr(syntax::cst::CompExpr context);
    std::any visitAnnoExpr(syntax::cst::AnnoExpr context);
    std::any visitAccessExpr(syntax::cst::AccessExpr context);
    /// `lhs.member` (a field or tuple element), located by `range`.
    node_ptr_t makeMemberAccess(
        node_ptr_t lhs, syntax::Child dot, syntax::Child member, syntax::Node range);
    std::any visitStructData(syntax::cst::StructData context);
    std::any visitArrayData(syntax::cst::ArrayData context);
    std::any visitTupleData(syntax::cst::TupleData context);
    std::any visitRefData(syntax::cst::RefData context);
    std::any visitLiteralData(syntax::cst::Literal context);
    std::any visitLiteral(syntax::cst::Literal context);
    std::any visitTypeExpr(syntax::cst::TypeExpr context);
    std::any visitUnionType(syntax::cst::UnionType context);
    std::any visitInterType(syntax::cst::InterType context);
    std::any visitDiffType(syntax::cst::DiffType context);
    std::any visitKeyUnionDiffType(syntax::cst::KeyUnionDiffType context);
    std::any visitKeyInterType(syntax::cst::KeyInterType context);
    std::any visitTypeUnit(syntax::cst::TypeUnit context);
    std::any visitArrayType(syntax::cst::ArrayType context);
    std::any visitConstData(syntax::cst::ConstData context);
    std::any visitSpecType(syntax::cst::SpecType context);
    std::any visitRefType(syntax::cst::RefType context);
    std::any visitTypeOfType(syntax::cst::TypeOfType context);
    std::any visitTypeAsType(syntax::cst::TypeAsType context);
    std::any visitStructType(syntax::cst::StructType context);
    std::any visitTypeList(syntax::cst::TypeList context);
    std::any visitTupleType(syntax::cst::TupleType context);
    std::any visitFuncType(syntax::cst::FuncType context);
    std::any visitIdentDef(syntax::cst::IdentDef context);
    std::any visitIdentRef(syntax::cst::IdentRef context);
};

} // namespace camel::parse::ast
