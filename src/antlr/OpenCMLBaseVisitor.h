
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

  virtual std::any visitLambda(OpenCMLParser::LambdaContext *ctx) override {
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

  virtual std::any visitArgument(OpenCMLParser::ArgumentContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitArgsList(OpenCMLParser::ArgsListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExprList(OpenCMLParser::ExprListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitKeyValExpr(OpenCMLParser::KeyValExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitKeyValList(OpenCMLParser::KeyValListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDictPack(OpenCMLParser::DictPackContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitListPack(OpenCMLParser::ListPackContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWithDecl(OpenCMLParser::WithDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWithPack(OpenCMLParser::WithPackContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitArgsDecl(OpenCMLParser::ArgsDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitArgsPack(OpenCMLParser::ArgsPackContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStmtPack(OpenCMLParser::StmtPackContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimEntity(OpenCMLParser::PrimEntityContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMemberAccess(OpenCMLParser::MemberAccessContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEntity(OpenCMLParser::EntityContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEntityLink(OpenCMLParser::EntityLinkContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEntityChain(OpenCMLParser::EntityChainContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEntityUnpack(OpenCMLParser::EntityUnpackContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEntityExpr(OpenCMLParser::EntityExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExpr(OpenCMLParser::ExprContext *ctx) override {
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

  virtual std::any visitValue(OpenCMLParser::ValueContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitType(OpenCMLParser::TypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitInnerType(OpenCMLParser::InnerTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNumberType(OpenCMLParser::NumberTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitScalarType(OpenCMLParser::ScalarTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitVectorType(OpenCMLParser::VectorTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStructType(OpenCMLParser::StructTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIdentRef(OpenCMLParser::IdentRefContext *ctx) override {
    return visitChildren(ctx);
  }


};

