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
 * Created: Mar. 17, 2024
 * Updated: Sep. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <iostream>
#include <regex>
#include <string>

#include "antlr/OpenCMLVisitor.h"
#include "antlr4-runtime/antlr4-runtime.h"

class CSTDumpVisitor : public OpenCMLVisitor {
    size_t depth = 0;
    std::ostream &os = std::cout;
    std::vector<bool> visible;

  public:
    CSTDumpVisitor(std::ostream &os) : os(os) {}

    std::any dumpNode(antlr4::tree::ParseTree *context, std::string nodeName);

    // Auto-generated visitor methods

    std::any visitProgram(OpenCMLParser::ProgramContext *context) {
        return dumpNode(context, "Program");
    };

    std::any visitDecl(OpenCMLParser::DeclContext *context) { return dumpNode(context, "Decl"); };

    std::any visitStmt(OpenCMLParser::StmtContext *context) { return dumpNode(context, "Stmt"); };

    std::any visitStmtList(OpenCMLParser::StmtListContext *context) {
        return dumpNode(context, "StmtList");
    };

    std::any visitModuleName(OpenCMLParser::ModuleNameContext *context) {
        return dumpNode(context, "ModuleName");
    };

    std::any visitModuleDecl(OpenCMLParser::ModuleDeclContext *context) {
        return dumpNode(context, "ModuleDecl");
    };

    std::any visitImportDecl(OpenCMLParser::ImportDeclContext *context) {
        return dumpNode(context, "ImportDecl");
    };

    std::any visitExportDecl(OpenCMLParser::ExportDeclContext *context) {
        return dumpNode(context, "ExportDecl");
    };

    std::any visitBlockStmt(OpenCMLParser::BlockStmtContext *context) {
        return dumpNode(context, "BlockStmt");
    };

    std::any visitStmtBlock(OpenCMLParser::StmtBlockContext *context) {
        return dumpNode(context, "StmtBlock");
    };

    std::any visitBlockExpr(OpenCMLParser::BlockExprContext *context) {
        return dumpNode(context, "BlockExpr");
    };

    std::any visitFuncData(OpenCMLParser::FuncDataContext *context) {
        return dumpNode(context, "FuncData");
    };

    std::any visitFuncDecl(OpenCMLParser::FuncDeclContext *context) {
        return dumpNode(context, "FuncDecl");
    };

    std::any visitParentIdents(OpenCMLParser::ParentIdentsContext *context) {
        return dumpNode(context, "ParentIdents");
    };

    std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) {
        return dumpNode(context, "BracedIdents");
    };

    std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) {
        return dumpNode(context, "BracketIdents");
    };

    std::any visitCarrier(OpenCMLParser::CarrierContext *context) {
        return dumpNode(context, "Carrier");
    };

    std::any visitDataDecl(OpenCMLParser::DataDeclContext *context) {
        return dumpNode(context, "DataDecl");
    };

    std::any visitTypeDecl(OpenCMLParser::TypeDeclContext *context) {
        return dumpNode(context, "TypeDecl");
    };

    std::any visitUseDecl(OpenCMLParser::UseDeclContext *context) {
        return dumpNode(context, "UseDecl");
    };

    std::any visitRetStmt(OpenCMLParser::RetStmtContext *context) {
        return dumpNode(context, "RetStmt");
    };

    std::any visitImplMark(OpenCMLParser::ImplMarkContext *context) {
        return dumpNode(context, "ImplMark");
    };

    std::any visitModifiers(OpenCMLParser::ModifiersContext *context) {
        return dumpNode(context, "Modifiers");
    };

    std::any visitIndexValue(OpenCMLParser::IndexValueContext *context) {
        return dumpNode(context, "IndexValue");
    };

    std::any visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) {
        return dumpNode(context, "KeyTypePair");
    };

    std::any visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) {
        return dumpNode(context, "KeyValuePair");
    };

    std::any visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) {
        return dumpNode(context, "KeyParamPair");
    };

    std::any visitDataList(OpenCMLParser::DataListContext *context) {
        return dumpNode(context, "DataList");
    };

    std::any visitIdentList(OpenCMLParser::IdentListContext *context) {
        return dumpNode(context, "IdentList");
    };

    std::any visitIndexValues(OpenCMLParser::IndexValuesContext *context) {
        return dumpNode(context, "IndexValues");
    };

    std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context) {
        return dumpNode(context, "PairedValues");
    };

    std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context) {
        return dumpNode(context, "PairedParams");
    };

    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context) {
        return dumpNode(context, "ArgumentList");
    };

    std::any visitIndices(OpenCMLParser::IndicesContext *context) {
        return dumpNode(context, "Indices");
    };

    std::any visitParentParams(OpenCMLParser::ParentParamsContext *context) {
        return dumpNode(context, "ParentParams");
    };

    std::any visitParentArgues(OpenCMLParser::ParentArguesContext *context) {
        return dumpNode(context, "ParentArgues");
    };

    std::any visitAngledParams(OpenCMLParser::AngledParamsContext *context) {
        return dumpNode(context, "AngledParams");
    };

    std::any visitAngledValues(OpenCMLParser::AngledValuesContext *context) {
        return dumpNode(context, "AngledValues");
    };

    std::any visitPattern(OpenCMLParser::PatternContext *context) {
        return dumpNode(context, "Pattern");
    };

    std::any visitMatchCase(OpenCMLParser::MatchCaseContext *context) {
        return dumpNode(context, "MatchCase");
    };

    std::any visitCatchClause(OpenCMLParser::CatchClauseContext *context) {
        return dumpNode(context, "CatchClause");
    };

    std::any visitCtrlExpr(OpenCMLParser::CtrlExprContext *context) {
        return dumpNode(context, "CtrlExpr");
    };

    std::any visitDataExpr(OpenCMLParser::DataExprContext *context) {
        return dumpNode(context, "DataExpr");
    };

    std::any visitWaitExpr(OpenCMLParser::WaitExprContext *context) {
        return dumpNode(context, "WaitExpr");
    };

    std::any visitAssignExpr(OpenCMLParser::AssignExprContext *context) {
        return dumpNode(context, "AssignExpr");
    };

    std::any visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) {
        return dumpNode(context, "LogicalOrExpr");
    };

    std::any visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) {
        return dumpNode(context, "LogicalAndExpr");
    };

    std::any visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) {
        return dumpNode(context, "EqualityExpr");
    };

    std::any visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) {
        return dumpNode(context, "RelationalExpr");
    };

    std::any visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) {
        return dumpNode(context, "AdditiveExpr");
    };

    std::any visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) {
        return dumpNode(context, "MultiplicativeExpr");
    };

    std::any visitNullableExpr(OpenCMLParser::NullableExprContext *context) {
        return dumpNode(context, "NullableExpr");
    };

    std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) {
        return dumpNode(context, "UnaryExpr");
    };

    std::any visitLinkExpr(OpenCMLParser::LinkExprContext *context) {
        return dumpNode(context, "LinkExpr");
    };

    std::any visitCompExpr(OpenCMLParser::CompExprContext *context) {
        return dumpNode(context, "CompExpr");
    };

    std::any visitAnnoExpr(OpenCMLParser::AnnoExprContext *context) {
        return dumpNode(context, "AnnoExpr");
    };

    std::any visitWithExpr(OpenCMLParser::WithExprContext *context) {
        return dumpNode(context, "WithExpr");
    };

    std::any visitAccessExpr(OpenCMLParser::AccessExprContext *context) {
        return dumpNode(context, "AccessExpr");
    };

    std::any visitDictData(OpenCMLParser::DictDataContext *context) {
        return dumpNode(context, "DictData");
    };

    std::any visitListData(OpenCMLParser::ListDataContext *context) {
        return dumpNode(context, "ListData");
    };

    std::any visitTupleData(OpenCMLParser::TupleDataContext *context) {
        return dumpNode(context, "TupleData");
    };

    std::any visitPrimaryData(OpenCMLParser::PrimaryDataContext *context) {
        return dumpNode(context, "PrimaryData");
    };

    std::any visitLiteral(OpenCMLParser::LiteralContext *context) {
        return dumpNode(context, "Literal");
    };

    std::any visitTypeExpr(OpenCMLParser::TypeExprContext *context) {
        return dumpNode(context, "TypeExpr");
    };

    std::any visitUnionType(OpenCMLParser::UnionTypeContext *context) {
        return dumpNode(context, "UnionType");
    };

    std::any visitInterType(OpenCMLParser::InterTypeContext *context) {
        return dumpNode(context, "InterType");
    };

    std::any visitDiffType(OpenCMLParser::DiffTypeContext *context) {
        return dumpNode(context, "DiffType");
    };

    std::any visitKeyUnionDiffType(OpenCMLParser::KeyUnionDiffTypeContext *context) {
        return dumpNode(context, "KeyUnionDiffType");
    };

    std::any visitKeyInterType(OpenCMLParser::KeyInterTypeContext *context) {
        return dumpNode(context, "KeyInterType");
    };

    std::any visitTypeUnit(OpenCMLParser::TypeUnitContext *context) {
        return dumpNode(context, "TypeUnit");
    };

    std::any visitListType(OpenCMLParser::ListTypeContext *context) {
        return dumpNode(context, "ListType");
    };

    std::any visitTypeOrData(OpenCMLParser::TypeOrDataContext *context) {
        return dumpNode(context, "TypeOrData");
    };

    std::any visitSpecType(OpenCMLParser::SpecTypeContext *context) {
        return dumpNode(context, "SpecType");
    };

    std::any visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) {
        return dumpNode(context, "PrimaryType");
    };

    std::any visitDictType(OpenCMLParser::DictTypeContext *context) {
        return dumpNode(context, "DictType");
    };

    std::any visitTypeList(OpenCMLParser::TypeListContext *context) {
        return dumpNode(context, "TypeList");
    };

    std::any visitTupleType(OpenCMLParser::TupleTypeContext *context) {
        return dumpNode(context, "TupleType");
    };

    std::any visitFuncType(OpenCMLParser::FuncTypeContext *context) {
        return dumpNode(context, "FuncType");
    };

    std::any visitIdentDef(OpenCMLParser::IdentDefContext *context) {
        return dumpNode(context, "IdentDef");
    };

    std::any visitIdentRef(OpenCMLParser::IdentRefContext *context) {
        return dumpNode(context, "IdentRef");
    };

    // End of auto-generated visitor methods
};
