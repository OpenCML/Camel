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
 * Created: Mar. 26, 2024
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <iostream>
#include <regex>
#include <string>

#include "antlr/OpenCMLVisitor.h"
#include "antlr4-runtime/antlr4-runtime.h"
#include "common/scope.h"
#include "error/diagnostics/diagnostics.h"
#include "parse/ast/ast.h"

namespace AbstractSyntaxTree {

class Builder : public OpenCMLVisitor {
  public:
    Builder() {};
    virtual ~Builder() = default;

    node_ptr_t build(antlr4::tree::ParseTree *tree, diagnostics_ptr_t diags) {
        diags_ = diags;
        root_ = nullptr;
        visit(tree);
        return root_;
    }

  private:
    node_ptr_t root_;

    diagnostics_ptr_t diags_;

    std::shared_ptr<ModuleLoad> module_ = std::make_shared<ModuleLoad>();
    std::vector<std::shared_ptr<ImportLoad>> imports_;
    std::shared_ptr<ExportLoad> export_ = std::make_shared<ExportLoad>();

    // Auto-generated visitor methods

    std::any visitProgram(OpenCMLParser::ProgramContext *context);

    std::any visitDecl(OpenCMLParser::DeclContext *context);

    std::any visitStmt(OpenCMLParser::StmtContext *context);

    std::any visitStmtList(OpenCMLParser::StmtListContext *context);

    std::any visitModuleName(OpenCMLParser::ModuleNameContext *context);

    std::any visitModuleDecl(OpenCMLParser::ModuleDeclContext *context);

    std::any visitImportDecl(OpenCMLParser::ImportDeclContext *context);

    std::any visitExportDecl(OpenCMLParser::ExportDeclContext *context);

    std::any visitBlockStmt(OpenCMLParser::BlockStmtContext *context);

    std::any visitStmtBlock(OpenCMLParser::StmtBlockContext *context);

    std::any visitBlockExpr(OpenCMLParser::BlockExprContext *context);

    std::any visitFuncData(OpenCMLParser::FuncDataContext *context);

    std::any visitFuncDecl(OpenCMLParser::FuncDeclContext *context);

    std::any visitParentIdents(OpenCMLParser::ParentIdentsContext *context);

    std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *context);

    std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *context);

    std::any visitCarrier(OpenCMLParser::CarrierContext *context);

    std::any visitDataDecl(OpenCMLParser::DataDeclContext *context);

    std::any visitTypeDecl(OpenCMLParser::TypeDeclContext *context);

    std::any visitUseDecl(OpenCMLParser::UseDeclContext *context);

    std::any visitRetStmt(OpenCMLParser::RetStmtContext *context);

    std::any visitImplMark(OpenCMLParser::ImplMarkContext *context);

    std::any visitModifiers(OpenCMLParser::ModifiersContext *context);

    std::any visitIndexValue(OpenCMLParser::IndexValueContext *context);

    std::any visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context);

    std::any visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context);

    std::any visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context);

    std::any visitDataList(OpenCMLParser::DataListContext *context);

    std::any visitIdentList(OpenCMLParser::IdentListContext *context);

    std::any visitIndexValues(OpenCMLParser::IndexValuesContext *context);

    std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context);

    std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context);

    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context);

    std::any visitIndices(OpenCMLParser::IndicesContext *context);

    std::any visitParentParams(OpenCMLParser::ParentParamsContext *context);

    std::any visitParentArgues(OpenCMLParser::ParentArguesContext *context);

    std::any visitAngledParams(OpenCMLParser::AngledParamsContext *context);

    std::any visitAngledValues(OpenCMLParser::AngledValuesContext *context);

    std::any visitPattern(OpenCMLParser::PatternContext *context);

    std::any visitMatchCase(OpenCMLParser::MatchCaseContext *context);

    std::any visitCatchClause(OpenCMLParser::CatchClauseContext *context);

    std::any visitCtrlExpr(OpenCMLParser::CtrlExprContext *context);

    std::any visitDataExpr(OpenCMLParser::DataExprContext *context);

    std::any visitWaitExpr(OpenCMLParser::WaitExprContext *context);

    std::any visitAssignExpr(OpenCMLParser::AssignExprContext *context);

    std::any visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context);

    std::any visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context);

    std::any visitEqualityExpr(OpenCMLParser::EqualityExprContext *context);

    std::any visitRelationalExpr(OpenCMLParser::RelationalExprContext *context);

    std::any visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context);

    std::any visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context);

    std::any visitNullableExpr(OpenCMLParser::NullableExprContext *context);

    std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context);

    std::any visitLinkExpr(OpenCMLParser::LinkExprContext *context);

    std::any visitCompExpr(OpenCMLParser::CompExprContext *context);

    std::any visitAnnoExpr(OpenCMLParser::AnnoExprContext *context);

    std::any visitWithExpr(OpenCMLParser::WithExprContext *context);

    std::any visitAccessExpr(OpenCMLParser::AccessExprContext *context);

    std::any visitStructData(OpenCMLParser::StructDataContext *context);

    std::any visitArrayData(OpenCMLParser::ArrayDataContext *context);

    std::any visitTupleData(OpenCMLParser::TupleDataContext *context);

    std::any visitPrimaryData(OpenCMLParser::PrimaryDataContext *context);

    std::any visitLiteral(OpenCMLParser::LiteralContext *context);

    std::any visitTypeExpr(OpenCMLParser::TypeExprContext *context);

    std::any visitUnionType(OpenCMLParser::UnionTypeContext *context);

    std::any visitInterType(OpenCMLParser::InterTypeContext *context);

    std::any visitDiffType(OpenCMLParser::DiffTypeContext *context);

    std::any visitKeyUnionDiffType(OpenCMLParser::KeyUnionDiffTypeContext *context);

    std::any visitKeyInterType(OpenCMLParser::KeyInterTypeContext *context);

    std::any visitTypeUnit(OpenCMLParser::TypeUnitContext *context);

    std::any visitArrayType(OpenCMLParser::ArrayTypeContext *context);

    std::any visitTypeOrData(OpenCMLParser::TypeOrDataContext *context);

    std::any visitSpecType(OpenCMLParser::SpecTypeContext *context);

    std::any visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context);

    std::any visitStructType(OpenCMLParser::StructTypeContext *context);

    std::any visitTypeList(OpenCMLParser::TypeListContext *context);

    std::any visitTupleType(OpenCMLParser::TupleTypeContext *context);

    std::any visitFuncType(OpenCMLParser::FuncTypeContext *context);

    std::any visitIdentDef(OpenCMLParser::IdentDefContext *context);

    std::any visitIdentRef(OpenCMLParser::IdentRefContext *context);

    // End of auto-generated visitor methods
};
} // namespace AbstractSyntaxTree
