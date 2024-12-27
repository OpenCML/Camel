
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
#include "OpenCMLVisitor.h"


/**
 * This class provides an empty implementation of OpenCMLVisitor, which can be
 * extended to create a visitor which only needs to handle a subset of the available methods.
 */
class  OpenCMLBaseVisitor : public OpenCMLVisitor {
public:

  virtual std::any visitProgram(OpenCMLParser::ProgramContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDecl(OpenCMLParser::DeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStmt(OpenCMLParser::StmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStmtList(OpenCMLParser::StmtListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitModuleDecl(OpenCMLParser::ModuleDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitImportDecl(OpenCMLParser::ImportDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExportDecl(OpenCMLParser::ExportDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStmtBlock(OpenCMLParser::StmtBlockContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLambdaExpr(OpenCMLParser::LambdaExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFuncDecl(OpenCMLParser::FuncDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitParentIdents(OpenCMLParser::ParentIdentsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCarrier(OpenCMLParser::CarrierContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLetStmt(OpenCMLParser::LetStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUseStmt(OpenCMLParser::UseStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRetStmt(OpenCMLParser::RetStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypeStmt(OpenCMLParser::TypeStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEnumStmt(OpenCMLParser::EnumStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExprStmt(OpenCMLParser::ExprStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAnnotation(OpenCMLParser::AnnotationContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAnnotations(OpenCMLParser::AnnotationsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitModifiers(OpenCMLParser::ModifiersContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIndexValue(OpenCMLParser::IndexValueContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitKeyTypePair(OpenCMLParser::KeyTypePairContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitKeyValuePair(OpenCMLParser::KeyValuePairContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitKeyParamPair(OpenCMLParser::KeyParamPairContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIdentList(OpenCMLParser::IdentListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitValueList(OpenCMLParser::ValueListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIndexValues(OpenCMLParser::IndexValuesContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPairedValues(OpenCMLParser::PairedValuesContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPairedParams(OpenCMLParser::PairedParamsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitArgumentList(OpenCMLParser::ArgumentListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMemberAccess(OpenCMLParser::MemberAccessContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitParentParams(OpenCMLParser::ParentParamsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitParentArgues(OpenCMLParser::ParentArguesContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAngledParams(OpenCMLParser::AngledParamsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAngledValues(OpenCMLParser::AngledValuesContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBlockExpr(OpenCMLParser::BlockExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDataExpr(OpenCMLParser::DataExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPattern(OpenCMLParser::PatternContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMatchCase(OpenCMLParser::MatchCaseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStructExpr(OpenCMLParser::StructExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEqualityExpr(OpenCMLParser::EqualityExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRelationalExpr(OpenCMLParser::RelationalExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNullableExpr(OpenCMLParser::NullableExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLinkExpr(OpenCMLParser::LinkExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBindExpr(OpenCMLParser::BindExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWithExpr(OpenCMLParser::WithExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAnnoExpr(OpenCMLParser::AnnoExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDictExpr(OpenCMLParser::DictExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitListExpr(OpenCMLParser::ListExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimaryData(OpenCMLParser::PrimaryDataContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLiteral(OpenCMLParser::LiteralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypeExpr(OpenCMLParser::TypeExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUnionType(OpenCMLParser::UnionTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUnionUnit(OpenCMLParser::UnionUnitContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitListType(OpenCMLParser::ListTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitArgsType(OpenCMLParser::ArgsTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimaryType(OpenCMLParser::PrimaryTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDictExprType(OpenCMLParser::DictExprTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDictType(OpenCMLParser::DictTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTupleType(OpenCMLParser::TupleTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLambdaType(OpenCMLParser::LambdaTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIdentDef(OpenCMLParser::IdentDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIdentRef(OpenCMLParser::IdentRefContext *ctx) override {
    return visitChildren(ctx);
  }


};

