
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
 * Supported by: National Key Research and Development Program of China
 */


// Generated from OpenCML.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"
#include "OpenCMLParser.h"



/**
 * This class defines an abstract visitor for a parse tree
 * produced by OpenCMLParser.
 */
class  OpenCMLVisitor : public antlr4::tree::AbstractParseTreeVisitor {
public:

  /**
   * Visit parse trees produced by OpenCMLParser.
   */
    virtual std::any visitProgram(OpenCMLParser::ProgramContext *context) = 0;

    virtual std::any visitDecl(OpenCMLParser::DeclContext *context) = 0;

    virtual std::any visitStmt(OpenCMLParser::StmtContext *context) = 0;

    virtual std::any visitStmtList(OpenCMLParser::StmtListContext *context) = 0;

    virtual std::any visitModuleDecl(OpenCMLParser::ModuleDeclContext *context) = 0;

    virtual std::any visitImportDecl(OpenCMLParser::ImportDeclContext *context) = 0;

    virtual std::any visitExportDecl(OpenCMLParser::ExportDeclContext *context) = 0;

    virtual std::any visitStmtBlock(OpenCMLParser::StmtBlockContext *context) = 0;

    virtual std::any visitBlockExpr(OpenCMLParser::BlockExprContext *context) = 0;

    virtual std::any visitBlockStmt(OpenCMLParser::BlockStmtContext *context) = 0;

    virtual std::any visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) = 0;

    virtual std::any visitFuncDecl(OpenCMLParser::FuncDeclContext *context) = 0;

    virtual std::any visitParentIdents(OpenCMLParser::ParentIdentsContext *context) = 0;

    virtual std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) = 0;

    virtual std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) = 0;

    virtual std::any visitCarrier(OpenCMLParser::CarrierContext *context) = 0;

    virtual std::any visitLetDecl(OpenCMLParser::LetDeclContext *context) = 0;

    virtual std::any visitUseDecl(OpenCMLParser::UseDeclContext *context) = 0;

    virtual std::any visitRetStmt(OpenCMLParser::RetStmtContext *context) = 0;

    virtual std::any visitTypeDecl(OpenCMLParser::TypeDeclContext *context) = 0;

    virtual std::any visitEnumDecl(OpenCMLParser::EnumDeclContext *context) = 0;

    virtual std::any visitExprStmt(OpenCMLParser::ExprStmtContext *context) = 0;

    virtual std::any visitAnnotation(OpenCMLParser::AnnotationContext *context) = 0;

    virtual std::any visitAnnotations(OpenCMLParser::AnnotationsContext *context) = 0;

    virtual std::any visitModifiers(OpenCMLParser::ModifiersContext *context) = 0;

    virtual std::any visitIndexValue(OpenCMLParser::IndexValueContext *context) = 0;

    virtual std::any visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) = 0;

    virtual std::any visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) = 0;

    virtual std::any visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) = 0;

    virtual std::any visitIdentList(OpenCMLParser::IdentListContext *context) = 0;

    virtual std::any visitValueList(OpenCMLParser::ValueListContext *context) = 0;

    virtual std::any visitIndexValues(OpenCMLParser::IndexValuesContext *context) = 0;

    virtual std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context) = 0;

    virtual std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context) = 0;

    virtual std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context) = 0;

    virtual std::any visitMemberAccess(OpenCMLParser::MemberAccessContext *context) = 0;

    virtual std::any visitParentParams(OpenCMLParser::ParentParamsContext *context) = 0;

    virtual std::any visitParentArgues(OpenCMLParser::ParentArguesContext *context) = 0;

    virtual std::any visitAngledParams(OpenCMLParser::AngledParamsContext *context) = 0;

    virtual std::any visitAngledValues(OpenCMLParser::AngledValuesContext *context) = 0;

    virtual std::any visitDataExpr(OpenCMLParser::DataExprContext *context) = 0;

    virtual std::any visitPattern(OpenCMLParser::PatternContext *context) = 0;

    virtual std::any visitMatchCase(OpenCMLParser::MatchCaseContext *context) = 0;

    virtual std::any visitCatchClause(OpenCMLParser::CatchClauseContext *context) = 0;

    virtual std::any visitStructExpr(OpenCMLParser::StructExprContext *context) = 0;

    virtual std::any visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) = 0;

    virtual std::any visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) = 0;

    virtual std::any visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) = 0;

    virtual std::any visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) = 0;

    virtual std::any visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) = 0;

    virtual std::any visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) = 0;

    virtual std::any visitNullableExpr(OpenCMLParser::NullableExprContext *context) = 0;

    virtual std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) = 0;

    virtual std::any visitLinkExpr(OpenCMLParser::LinkExprContext *context) = 0;

    virtual std::any visitBindExpr(OpenCMLParser::BindExprContext *context) = 0;

    virtual std::any visitWithExpr(OpenCMLParser::WithExprContext *context) = 0;

    virtual std::any visitAnnoExpr(OpenCMLParser::AnnoExprContext *context) = 0;

    virtual std::any visitDictExpr(OpenCMLParser::DictExprContext *context) = 0;

    virtual std::any visitListExpr(OpenCMLParser::ListExprContext *context) = 0;

    virtual std::any visitPrimaryData(OpenCMLParser::PrimaryDataContext *context) = 0;

    virtual std::any visitLiteral(OpenCMLParser::LiteralContext *context) = 0;

    virtual std::any visitTypeExpr(OpenCMLParser::TypeExprContext *context) = 0;

    virtual std::any visitUnionType(OpenCMLParser::UnionTypeContext *context) = 0;

    virtual std::any visitUnionUnit(OpenCMLParser::UnionUnitContext *context) = 0;

    virtual std::any visitListType(OpenCMLParser::ListTypeContext *context) = 0;

    virtual std::any visitTypeOrData(OpenCMLParser::TypeOrDataContext *context) = 0;

    virtual std::any visitArgsType(OpenCMLParser::ArgsTypeContext *context) = 0;

    virtual std::any visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) = 0;

    virtual std::any visitDictExprType(OpenCMLParser::DictExprTypeContext *context) = 0;

    virtual std::any visitDictType(OpenCMLParser::DictTypeContext *context) = 0;

    virtual std::any visitTupleType(OpenCMLParser::TupleTypeContext *context) = 0;

    virtual std::any visitLambdaType(OpenCMLParser::LambdaTypeContext *context) = 0;

    virtual std::any visitIdentDef(OpenCMLParser::IdentDefContext *context) = 0;

    virtual std::any visitIdentRef(OpenCMLParser::IdentRefContext *context) = 0;


};

