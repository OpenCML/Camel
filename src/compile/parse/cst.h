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
 * Updated: Mar. 25, 2025
 * Supported by: National Key Research and Development Program of China
 */

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

    std::any visitProgram(OpenCMLParser::ProgramContext *context) { return dumpNode(context, "Program"); };

    std::any visitDecl(OpenCMLParser::DeclContext *context) { return dumpNode(context, "Decl"); };

    std::any visitStmt(OpenCMLParser::StmtContext *context) { return dumpNode(context, "Stmt"); };

    std::any visitStmtList(OpenCMLParser::StmtListContext *context) { return dumpNode(context, "StmtList"); };

    std::any visitModuleDecl(OpenCMLParser::ModuleDeclContext *context) { return dumpNode(context, "ModuleDecl"); };

    std::any visitImportDecl(OpenCMLParser::ImportDeclContext *context) { return dumpNode(context, "ImportDecl"); };

    std::any visitExportDecl(OpenCMLParser::ExportDeclContext *context) { return dumpNode(context, "ExportDecl"); };

    std::any visitStmtBlock(OpenCMLParser::StmtBlockContext *context) { return dumpNode(context, "StmtBlock"); };

    std::any visitBlockExpr(OpenCMLParser::BlockExprContext *context) { return dumpNode(context, "BlockExpr"); };

    std::any visitWaitStmt(OpenCMLParser::WaitStmtContext *context) { return dumpNode(context, "WaitStmt"); };

    std::any visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) { return dumpNode(context, "LambdaExpr"); };

    std::any visitFuncDecl(OpenCMLParser::FuncDeclContext *context) { return dumpNode(context, "FuncDecl"); };

    std::any visitParentIdents(OpenCMLParser::ParentIdentsContext *context) { return dumpNode(context, "ParentIdents"); };

    std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) { return dumpNode(context, "BracedIdents"); };

    std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) { return dumpNode(context, "BracketIdents"); };

    std::any visitCarrier(OpenCMLParser::CarrierContext *context) { return dumpNode(context, "Carrier"); };

    std::any visitLetDecl(OpenCMLParser::LetDeclContext *context) { return dumpNode(context, "LetDecl"); };

    std::any visitUseDecl(OpenCMLParser::UseDeclContext *context) { return dumpNode(context, "UseDecl"); };

    std::any visitRetStmt(OpenCMLParser::RetStmtContext *context) { return dumpNode(context, "RetStmt"); };

    std::any visitTypeDecl(OpenCMLParser::TypeDeclContext *context) { return dumpNode(context, "TypeDecl"); };

    std::any visitEnumDecl(OpenCMLParser::EnumDeclContext *context) { return dumpNode(context, "EnumDecl"); };

    std::any visitAnnotation(OpenCMLParser::AnnotationContext *context) { return dumpNode(context, "Annotation"); };

    std::any visitAnnotations(OpenCMLParser::AnnotationsContext *context) { return dumpNode(context, "Annotations"); };

    std::any visitImplMark(OpenCMLParser::ImplMarkContext *context) { return dumpNode(context, "ImplMark"); };

    std::any visitModifiers(OpenCMLParser::ModifiersContext *context) { return dumpNode(context, "Modifiers"); };

    std::any visitIndexValue(OpenCMLParser::IndexValueContext *context) { return dumpNode(context, "IndexValue"); };

    std::any visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) { return dumpNode(context, "KeyTypePair"); };

    std::any visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) { return dumpNode(context, "KeyValuePair"); };

    std::any visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) { return dumpNode(context, "KeyParamPair"); };

    std::any visitDataList(OpenCMLParser::DataListContext *context) { return dumpNode(context, "DataList"); };

    std::any visitIdentList(OpenCMLParser::IdentListContext *context) { return dumpNode(context, "IdentList"); };

    std::any visitValueList(OpenCMLParser::ValueListContext *context) { return dumpNode(context, "ValueList"); };

    std::any visitIndexValues(OpenCMLParser::IndexValuesContext *context) { return dumpNode(context, "IndexValues"); };

    std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context) { return dumpNode(context, "PairedValues"); };

    std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context) { return dumpNode(context, "PairedParams"); };

    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context) { return dumpNode(context, "ArgumentList"); };

    std::any visitMemberAccess(OpenCMLParser::MemberAccessContext *context) { return dumpNode(context, "MemberAccess"); };

    std::any visitParentParams(OpenCMLParser::ParentParamsContext *context) { return dumpNode(context, "ParentParams"); };

    std::any visitParentArgues(OpenCMLParser::ParentArguesContext *context) { return dumpNode(context, "ParentArgues"); };

    std::any visitAngledParams(OpenCMLParser::AngledParamsContext *context) { return dumpNode(context, "AngledParams"); };

    std::any visitAngledValues(OpenCMLParser::AngledValuesContext *context) { return dumpNode(context, "AngledValues"); };

    std::any visitWaitExpr(OpenCMLParser::WaitExprContext *context) { return dumpNode(context, "WaitExpr"); };

    std::any visitPattern(OpenCMLParser::PatternContext *context) { return dumpNode(context, "Pattern"); };

    std::any visitMatchCase(OpenCMLParser::MatchCaseContext *context) { return dumpNode(context, "MatchCase"); };

    std::any visitCatchClause(OpenCMLParser::CatchClauseContext *context) { return dumpNode(context, "CatchClause"); };

    std::any visitCtrlExpr(OpenCMLParser::CtrlExprContext *context) { return dumpNode(context, "CtrlExpr"); };

    std::any visitDataExpr(OpenCMLParser::DataExprContext *context) { return dumpNode(context, "DataExpr"); };

    std::any visitAssignExpr(OpenCMLParser::AssignExprContext *context) { return dumpNode(context, "AssignExpr"); };

    std::any visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) { return dumpNode(context, "LogicalOrExpr"); };

    std::any visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) { return dumpNode(context, "LogicalAndExpr"); };

    std::any visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) { return dumpNode(context, "EqualityExpr"); };

    std::any visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) { return dumpNode(context, "RelationalExpr"); };

    std::any visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) { return dumpNode(context, "AdditiveExpr"); };

    std::any visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) { return dumpNode(context, "MultiplicativeExpr"); };

    std::any visitNullableExpr(OpenCMLParser::NullableExprContext *context) { return dumpNode(context, "NullableExpr"); };

    std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) { return dumpNode(context, "UnaryExpr"); };

    std::any visitLinkExpr(OpenCMLParser::LinkExprContext *context) { return dumpNode(context, "LinkExpr"); };

    std::any visitBindExpr(OpenCMLParser::BindExprContext *context) { return dumpNode(context, "BindExpr"); };

    std::any visitWithExpr(OpenCMLParser::WithExprContext *context) { return dumpNode(context, "WithExpr"); };

    std::any visitAnnoExpr(OpenCMLParser::AnnoExprContext *context) { return dumpNode(context, "AnnoExpr"); };

    std::any visitDictExpr(OpenCMLParser::DictExprContext *context) { return dumpNode(context, "DictExpr"); };

    std::any visitListExpr(OpenCMLParser::ListExprContext *context) { return dumpNode(context, "ListExpr"); };

    std::any visitPrimaryData(OpenCMLParser::PrimaryDataContext *context) { return dumpNode(context, "PrimaryData"); };

    std::any visitLiteral(OpenCMLParser::LiteralContext *context) { return dumpNode(context, "Literal"); };

    std::any visitTypeExpr(OpenCMLParser::TypeExprContext *context) { return dumpNode(context, "TypeExpr"); };

    std::any visitUnionType(OpenCMLParser::UnionTypeContext *context) { return dumpNode(context, "UnionType"); };

    std::any visitUnionUnit(OpenCMLParser::UnionUnitContext *context) { return dumpNode(context, "UnionUnit"); };

    std::any visitListType(OpenCMLParser::ListTypeContext *context) { return dumpNode(context, "ListType"); };

    std::any visitTypeOrData(OpenCMLParser::TypeOrDataContext *context) { return dumpNode(context, "TypeOrData"); };

    std::any visitArgsType(OpenCMLParser::ArgsTypeContext *context) { return dumpNode(context, "ArgsType"); };

    std::any visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) { return dumpNode(context, "PrimaryType"); };

    std::any visitDictExprType(OpenCMLParser::DictExprTypeContext *context) { return dumpNode(context, "DictExprType"); };

    std::any visitDictType(OpenCMLParser::DictTypeContext *context) { return dumpNode(context, "DictType"); };

    std::any visitTypeList(OpenCMLParser::TypeListContext *context) { return dumpNode(context, "TypeList"); };

    std::any visitTupleType(OpenCMLParser::TupleTypeContext *context) { return dumpNode(context, "TupleType"); };

    std::any visitLambdaType(OpenCMLParser::LambdaTypeContext *context) { return dumpNode(context, "LambdaType"); };

    std::any visitIdentDef(OpenCMLParser::IdentDefContext *context) { return dumpNode(context, "IdentDef"); };

    std::any visitIdentRef(OpenCMLParser::IdentRefContext *context) { return dumpNode(context, "IdentRef"); };
};
