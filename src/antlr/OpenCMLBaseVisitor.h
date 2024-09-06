
/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
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

  virtual std::any visitStmtList(OpenCMLParser::StmtListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStmt(OpenCMLParser::StmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLetStmt(OpenCMLParser::LetStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUseStmt(OpenCMLParser::UseStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypeStmt(OpenCMLParser::TypeStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExprStmt(OpenCMLParser::ExprStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAssignStmt(OpenCMLParser::AssignStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWithDef(OpenCMLParser::WithDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFuncDef(OpenCMLParser::FuncDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRetStmt(OpenCMLParser::RetStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLambdaExpr(OpenCMLParser::LambdaExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCarrier(OpenCMLParser::CarrierContext *ctx) override {
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

  virtual std::any visitKeyTypePair(OpenCMLParser::KeyTypePairContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitKeyValuePair(OpenCMLParser::KeyValuePairContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitKeyParamPair(OpenCMLParser::KeyParamPairContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypeList(OpenCMLParser::TypeListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIdentList(OpenCMLParser::IdentListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitValueList(OpenCMLParser::ValueListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPairedTypes(OpenCMLParser::PairedTypesContext *ctx) override {
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

  virtual std::any visitBracedPairedValues(OpenCMLParser::BracedPairedValuesContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBracedStmts(OpenCMLParser::BracedStmtsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBracketValues(OpenCMLParser::BracketValuesContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitParentParams(OpenCMLParser::ParentParamsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitParentValues(OpenCMLParser::ParentValuesContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAngledParams(OpenCMLParser::AngledParamsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAngledValues(OpenCMLParser::AngledValuesContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMemberAccess(OpenCMLParser::MemberAccessContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimEntity(OpenCMLParser::PrimEntityContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEntityChain(OpenCMLParser::EntityChainContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEntityLink(OpenCMLParser::EntityLinkContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEntityUnit(OpenCMLParser::EntityUnitContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEntityWith(OpenCMLParser::EntityWithContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEntityExpr(OpenCMLParser::EntityExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRelaExpr(OpenCMLParser::RelaExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAddExpr(OpenCMLParser::AddExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMultiExpr(OpenCMLParser::MultiExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimExpr(OpenCMLParser::PrimExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLiteral(OpenCMLParser::LiteralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypeExpr(OpenCMLParser::TypeExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUnaryType(OpenCMLParser::UnaryTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAtomType(OpenCMLParser::AtomTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLambdaType(OpenCMLParser::LambdaTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimType(OpenCMLParser::PrimTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStructType(OpenCMLParser::StructTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSpecialType(OpenCMLParser::SpecialTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIdentRef(OpenCMLParser::IdentRefContext *ctx) override {
    return visitChildren(ctx);
  }


};

