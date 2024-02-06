
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

    virtual std::any visitWithDef(OpenCMLParser::WithDefContext *context) = 0;

    virtual std::any visitFuncDef(OpenCMLParser::FuncDefContext *context) = 0;

    virtual std::any visitTypeDef(OpenCMLParser::TypeDefContext *context) = 0;

    virtual std::any visitEnumDef(OpenCMLParser::EnumDefContext *context) = 0;

    virtual std::any visitRetStmt(OpenCMLParser::RetStmtContext *context) = 0;

    virtual std::any visitExprStmt(OpenCMLParser::ExprStmtContext *context) = 0;

    virtual std::any visitAssignStmt(OpenCMLParser::AssignStmtContext *context) = 0;

    virtual std::any visitCarrier(OpenCMLParser::CarrierContext *context) = 0;

    virtual std::any visitBiasAnno(OpenCMLParser::BiasAnnoContext *context) = 0;

    virtual std::any visitSizeAnno(OpenCMLParser::SizeAnnoContext *context) = 0;

    virtual std::any visitAnnotation(OpenCMLParser::AnnotationContext *context) = 0;

    virtual std::any visitAnnotations(OpenCMLParser::AnnotationsContext *context) = 0;

    virtual std::any visitModifiers(OpenCMLParser::ModifiersContext *context) = 0;

    virtual std::any visitWithList(OpenCMLParser::WithListContext *context) = 0;

    virtual std::any visitWithDecl(OpenCMLParser::WithDeclContext *context) = 0;

    virtual std::any visitParamDef(OpenCMLParser::ParamDefContext *context) = 0;

    virtual std::any visitArgsList(OpenCMLParser::ArgsListContext *context) = 0;

    virtual std::any visitArgument(OpenCMLParser::ArgumentContext *context) = 0;

    virtual std::any visitTypePack(OpenCMLParser::TypePackContext *context) = 0;

    virtual std::any visitKeyValDecl(OpenCMLParser::KeyValDeclContext *context) = 0;

    virtual std::any visitKeyValExpr(OpenCMLParser::KeyValExprContext *context) = 0;

    virtual std::any visitEntityRef(OpenCMLParser::EntityRefContext *context) = 0;

    virtual std::any visitFunctorRef(OpenCMLParser::FunctorRefContext *context) = 0;

    virtual std::any visitListUnpack(OpenCMLParser::ListUnpackContext *context) = 0;

    virtual std::any visitDictUnpack(OpenCMLParser::DictUnpackContext *context) = 0;

    virtual std::any visitDictPack(OpenCMLParser::DictPackContext *context) = 0;

    virtual std::any visitListPack(OpenCMLParser::ListPackContext *context) = 0;

    virtual std::any visitStmtPack(OpenCMLParser::StmtPackContext *context) = 0;

    virtual std::any visitLambda(OpenCMLParser::LambdaContext *context) = 0;

    virtual std::any visitEntityExpr(OpenCMLParser::EntityExprContext *context) = 0;

    virtual std::any visitEntityChain(OpenCMLParser::EntityChainContext *context) = 0;

    virtual std::any visitEntity(OpenCMLParser::EntityContext *context) = 0;

    virtual std::any visitNormCall(OpenCMLParser::NormCallContext *context) = 0;

    virtual std::any visitLinkCall(OpenCMLParser::LinkCallContext *context) = 0;

    virtual std::any visitCalcExpr(OpenCMLParser::CalcExprContext *context) = 0;

    virtual std::any visitRelaExpr(OpenCMLParser::RelaExprContext *context) = 0;

    virtual std::any visitAddExpr(OpenCMLParser::AddExprContext *context) = 0;

    virtual std::any visitMultiExpr(OpenCMLParser::MultiExprContext *context) = 0;

    virtual std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) = 0;

    virtual std::any visitPrimExpr(OpenCMLParser::PrimExprContext *context) = 0;

    virtual std::any visitExpr(OpenCMLParser::ExprContext *context) = 0;

    virtual std::any visitLiteral(OpenCMLParser::LiteralContext *context) = 0;

    virtual std::any visitValue(OpenCMLParser::ValueContext *context) = 0;

    virtual std::any visitType(OpenCMLParser::TypeContext *context) = 0;

    virtual std::any visitInnerType(OpenCMLParser::InnerTypeContext *context) = 0;

    virtual std::any visitNumberType(OpenCMLParser::NumberTypeContext *context) = 0;

    virtual std::any visitScalarType(OpenCMLParser::ScalarTypeContext *context) = 0;

    virtual std::any visitVectorType(OpenCMLParser::VectorTypeContext *context) = 0;

    virtual std::any visitStructType(OpenCMLParser::StructTypeContext *context) = 0;

    virtual std::any visitNullableType(OpenCMLParser::NullableTypeContext *context) = 0;

    virtual std::any visitIdentRef(OpenCMLParser::IdentRefContext *context) = 0;


};

