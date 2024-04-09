
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

    virtual std::any visitStmtList(OpenCMLParser::StmtListContext *context) = 0;

    virtual std::any visitStmt(OpenCMLParser::StmtContext *context) = 0;

    virtual std::any visitLetStmt(OpenCMLParser::LetStmtContext *context) = 0;

    virtual std::any visitUseStmt(OpenCMLParser::UseStmtContext *context) = 0;

    virtual std::any visitExprStmt(OpenCMLParser::ExprStmtContext *context) = 0;

    virtual std::any visitAssignStmt(OpenCMLParser::AssignStmtContext *context) = 0;

    virtual std::any visitWithDef(OpenCMLParser::WithDefContext *context) = 0;

    virtual std::any visitFuncDef(OpenCMLParser::FuncDefContext *context) = 0;

    virtual std::any visitRetStmt(OpenCMLParser::RetStmtContext *context) = 0;

    virtual std::any visitLambda(OpenCMLParser::LambdaContext *context) = 0;

    virtual std::any visitCarrier(OpenCMLParser::CarrierContext *context) = 0;

    virtual std::any visitAnnotation(OpenCMLParser::AnnotationContext *context) = 0;

    virtual std::any visitAnnotations(OpenCMLParser::AnnotationsContext *context) = 0;

    virtual std::any visitModifiers(OpenCMLParser::ModifiersContext *context) = 0;

    virtual std::any visitArgument(OpenCMLParser::ArgumentContext *context) = 0;

    virtual std::any visitArgsList(OpenCMLParser::ArgsListContext *context) = 0;

    virtual std::any visitExprList(OpenCMLParser::ExprListContext *context) = 0;

    virtual std::any visitKeyValExpr(OpenCMLParser::KeyValExprContext *context) = 0;

    virtual std::any visitKeyValList(OpenCMLParser::KeyValListContext *context) = 0;

    virtual std::any visitDictPack(OpenCMLParser::DictPackContext *context) = 0;

    virtual std::any visitListPack(OpenCMLParser::ListPackContext *context) = 0;

    virtual std::any visitWithDecl(OpenCMLParser::WithDeclContext *context) = 0;

    virtual std::any visitWithPack(OpenCMLParser::WithPackContext *context) = 0;

    virtual std::any visitArgsDecl(OpenCMLParser::ArgsDeclContext *context) = 0;

    virtual std::any visitArgsPack(OpenCMLParser::ArgsPackContext *context) = 0;

    virtual std::any visitStmtPack(OpenCMLParser::StmtPackContext *context) = 0;

    virtual std::any visitPrimEntity(OpenCMLParser::PrimEntityContext *context) = 0;

    virtual std::any visitMemberAccess(OpenCMLParser::MemberAccessContext *context) = 0;

    virtual std::any visitEntity(OpenCMLParser::EntityContext *context) = 0;

    virtual std::any visitEntityLink(OpenCMLParser::EntityLinkContext *context) = 0;

    virtual std::any visitEntityChain(OpenCMLParser::EntityChainContext *context) = 0;

    virtual std::any visitEntityUnpack(OpenCMLParser::EntityUnpackContext *context) = 0;

    virtual std::any visitEntityExpr(OpenCMLParser::EntityExprContext *context) = 0;

    virtual std::any visitExpr(OpenCMLParser::ExprContext *context) = 0;

    virtual std::any visitRelaExpr(OpenCMLParser::RelaExprContext *context) = 0;

    virtual std::any visitAddExpr(OpenCMLParser::AddExprContext *context) = 0;

    virtual std::any visitMultiExpr(OpenCMLParser::MultiExprContext *context) = 0;

    virtual std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) = 0;

    virtual std::any visitPrimExpr(OpenCMLParser::PrimExprContext *context) = 0;

    virtual std::any visitLiteral(OpenCMLParser::LiteralContext *context) = 0;

    virtual std::any visitValue(OpenCMLParser::ValueContext *context) = 0;

    virtual std::any visitType(OpenCMLParser::TypeContext *context) = 0;

    virtual std::any visitTypeUnit(OpenCMLParser::TypeUnitContext *context) = 0;

    virtual std::any visitInnerType(OpenCMLParser::InnerTypeContext *context) = 0;

    virtual std::any visitScalarType(OpenCMLParser::ScalarTypeContext *context) = 0;

    virtual std::any visitVectorType(OpenCMLParser::VectorTypeContext *context) = 0;

    virtual std::any visitStructType(OpenCMLParser::StructTypeContext *context) = 0;

    virtual std::any visitIdentRef(OpenCMLParser::IdentRefContext *context) = 0;


};

