
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

    virtual std::any visitStmtList(OpenCMLParser::StmtListContext *context) = 0;

    virtual std::any visitStmt(OpenCMLParser::StmtContext *context) = 0;

    virtual std::any visitLetStmt(OpenCMLParser::LetStmtContext *context) = 0;

    virtual std::any visitUseStmt(OpenCMLParser::UseStmtContext *context) = 0;

    virtual std::any visitTypeStmt(OpenCMLParser::TypeStmtContext *context) = 0;

    virtual std::any visitExprStmt(OpenCMLParser::ExprStmtContext *context) = 0;

    virtual std::any visitWaitStmt(OpenCMLParser::WaitStmtContext *context) = 0;

    virtual std::any visitWithDef(OpenCMLParser::WithDefContext *context) = 0;

    virtual std::any visitFuncDecl(OpenCMLParser::FuncDeclContext *context) = 0;

    virtual std::any visitFuncDef(OpenCMLParser::FuncDefContext *context) = 0;

    virtual std::any visitRetStmt(OpenCMLParser::RetStmtContext *context) = 0;

    virtual std::any visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) = 0;

    virtual std::any visitCarrier(OpenCMLParser::CarrierContext *context) = 0;

    virtual std::any visitAnnotation(OpenCMLParser::AnnotationContext *context) = 0;

    virtual std::any visitAnnotations(OpenCMLParser::AnnotationsContext *context) = 0;

    virtual std::any visitModifiers(OpenCMLParser::ModifiersContext *context) = 0;

    virtual std::any visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) = 0;

    virtual std::any visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) = 0;

    virtual std::any visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) = 0;

    virtual std::any visitIndexKTPair(OpenCMLParser::IndexKTPairContext *context) = 0;

    virtual std::any visitIndexKVPair(OpenCMLParser::IndexKVPairContext *context) = 0;

    virtual std::any visitTypeList(OpenCMLParser::TypeListContext *context) = 0;

    virtual std::any visitIdentList(OpenCMLParser::IdentListContext *context) = 0;

    virtual std::any visitValueList(OpenCMLParser::ValueListContext *context) = 0;

    virtual std::any visitPairedTypes(OpenCMLParser::PairedTypesContext *context) = 0;

    virtual std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context) = 0;

    virtual std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context) = 0;

    virtual std::any visitIndexKVPairs(OpenCMLParser::IndexKVPairsContext *context) = 0;

    virtual std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context) = 0;

    virtual std::any visitBracedPairedValues(OpenCMLParser::BracedPairedValuesContext *context) = 0;

    virtual std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) = 0;

    virtual std::any visitBracedStmts(OpenCMLParser::BracedStmtsContext *context) = 0;

    virtual std::any visitBracedValues(OpenCMLParser::BracedValuesContext *context) = 0;

    virtual std::any visitBracedIndexKVPairs(OpenCMLParser::BracedIndexKVPairsContext *context) = 0;

    virtual std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) = 0;

    virtual std::any visitBracketValues(OpenCMLParser::BracketValuesContext *context) = 0;

    virtual std::any visitMemberAccess(OpenCMLParser::MemberAccessContext *context) = 0;

    virtual std::any visitParentParams(OpenCMLParser::ParentParamsContext *context) = 0;

    virtual std::any visitParentArgues(OpenCMLParser::ParentArguesContext *context) = 0;

    virtual std::any visitParentValues(OpenCMLParser::ParentValuesContext *context) = 0;

    virtual std::any visitAngledParams(OpenCMLParser::AngledParamsContext *context) = 0;

    virtual std::any visitAngledValues(OpenCMLParser::AngledValuesContext *context) = 0;

    virtual std::any visitEntityExpr(OpenCMLParser::EntityExprContext *context) = 0;

    virtual std::any visitTernaryExpr(OpenCMLParser::TernaryExprContext *context) = 0;

    virtual std::any visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) = 0;

    virtual std::any visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) = 0;

    virtual std::any visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) = 0;

    virtual std::any visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) = 0;

    virtual std::any visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) = 0;

    virtual std::any visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) = 0;

    virtual std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) = 0;

    virtual std::any visitLinkExpr(OpenCMLParser::LinkExprContext *context) = 0;

    virtual std::any visitWithExpr(OpenCMLParser::WithExprContext *context) = 0;

    virtual std::any visitAnnotatedExpr(OpenCMLParser::AnnotatedExprContext *context) = 0;

    virtual std::any visitPrimaryExpr(OpenCMLParser::PrimaryExprContext *context) = 0;

    virtual std::any visitLiteral(OpenCMLParser::LiteralContext *context) = 0;

    virtual std::any visitTypeExpr(OpenCMLParser::TypeExprContext *context) = 0;

    virtual std::any visitArrayType(OpenCMLParser::ArrayTypeContext *context) = 0;

    virtual std::any visitAtomType(OpenCMLParser::AtomTypeContext *context) = 0;

    virtual std::any visitLambdaType(OpenCMLParser::LambdaTypeContext *context) = 0;

    virtual std::any visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) = 0;

    virtual std::any visitStructType(OpenCMLParser::StructTypeContext *context) = 0;

    virtual std::any visitSpecialType(OpenCMLParser::SpecialTypeContext *context) = 0;

    virtual std::any visitIdentRef(OpenCMLParser::IdentRefContext *context) = 0;


};

