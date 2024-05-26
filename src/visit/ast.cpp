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
 * Created: Mar. 26, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "ast.h"

std::any ASTConstructor::visitProgram(OpenCMLParser::ProgramContext *context) { return dumpCST(context, "Program"); };

std::any ASTConstructor::visitStmtList(OpenCMLParser::StmtListContext *context) {
    return dumpCST(context, "StmtList");
};

std::any ASTConstructor::visitStmt(OpenCMLParser::StmtContext *context) { return dumpCST(context, "Stmt"); };

std::any ASTConstructor::visitLetStmt(OpenCMLParser::LetStmtContext *context) { return dumpCST(context, "LetStmt"); };

std::any ASTConstructor::visitUseStmt(OpenCMLParser::UseStmtContext *context) { return dumpCST(context, "UseStmt"); };

std::any ASTConstructor::visitTypeStmt(OpenCMLParser::TypeStmtContext *context) {
    return dumpCST(context, "TypeStmt");
};

std::any ASTConstructor::visitExprStmt(OpenCMLParser::ExprStmtContext *context) {
    return dumpCST(context, "ExprStmt");
};

std::any ASTConstructor::visitAssignStmt(OpenCMLParser::AssignStmtContext *context) {
    return dumpCST(context, "AssignStmt");
};

std::any ASTConstructor::visitWithDef(OpenCMLParser::WithDefContext *context) { return dumpCST(context, "WithDef"); };

std::any ASTConstructor::visitFuncDef(OpenCMLParser::FuncDefContext *context) { return dumpCST(context, "FuncDef"); };

std::any ASTConstructor::visitRetStmt(OpenCMLParser::RetStmtContext *context) { return dumpCST(context, "RetStmt"); };

std::any ASTConstructor::visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) {
    return dumpCST(context, "LambdaExpr");
};

std::any ASTConstructor::visitCarrier(OpenCMLParser::CarrierContext *context) { return dumpCST(context, "Carrier"); };

std::any ASTConstructor::visitAnnotation(OpenCMLParser::AnnotationContext *context) {
    return dumpCST(context, "Annotation");
};

std::any ASTConstructor::visitAnnotations(OpenCMLParser::AnnotationsContext *context) {
    return dumpCST(context, "Annotations");
};

std::any ASTConstructor::visitModifiers(OpenCMLParser::ModifiersContext *context) {
    return dumpCST(context, "Modifiers");
};

std::any ASTConstructor::visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) {
    return dumpCST(context, "KeyTypePair");
};

std::any ASTConstructor::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) {
    return dumpCST(context, "KeyValuePair");
};

std::any ASTConstructor::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) {
    return dumpCST(context, "KeyParamPair");
};

std::any ASTConstructor::visitTypeList(OpenCMLParser::TypeListContext *context) {
    return dumpCST(context, "TypeList");
};

std::any ASTConstructor::visitIdentList(OpenCMLParser::IdentListContext *context) {
    return dumpCST(context, "IdentList");
};

std::any ASTConstructor::visitValueList(OpenCMLParser::ValueListContext *context) {
    return dumpCST(context, "ValueList");
};

std::any ASTConstructor::visitPairedTypes(OpenCMLParser::PairedTypesContext *context) {
    return dumpCST(context, "PairedTypes");
};

std::any ASTConstructor::visitPairedValues(OpenCMLParser::PairedValuesContext *context) {
    return dumpCST(context, "PairedValues");
};

std::any ASTConstructor::visitPairedParams(OpenCMLParser::PairedParamsContext *context) {
    return dumpCST(context, "PairedParams");
};

std::any ASTConstructor::visitArgumentList(OpenCMLParser::ArgumentListContext *context) {
    return dumpCST(context, "ArgumentList");
};

std::any ASTConstructor::visitBracedPairedValues(OpenCMLParser::BracedPairedValuesContext *context) {
    return dumpCST(context, "BracedPairedValues");
};

std::any ASTConstructor::visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) {
    return dumpCST(context, "BracedIdents");
};

std::any ASTConstructor::visitBracedStmts(OpenCMLParser::BracedStmtsContext *context) {
    return dumpCST(context, "BracedStmts");
};

std::any ASTConstructor::visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) {
    return dumpCST(context, "BracketIdents");
};

std::any ASTConstructor::visitBracketValues(OpenCMLParser::BracketValuesContext *context) {
    return dumpCST(context, "BracketValues");
};

std::any ASTConstructor::visitParentParams(OpenCMLParser::ParentParamsContext *context) {
    return dumpCST(context, "ParentParams");
};

std::any ASTConstructor::visitParentValues(OpenCMLParser::ParentValuesContext *context) {
    return dumpCST(context, "ParentValues");
};

std::any ASTConstructor::visitAngledParams(OpenCMLParser::AngledParamsContext *context) {
    return dumpCST(context, "AngledParams");
};

std::any ASTConstructor::visitAngledValues(OpenCMLParser::AngledValuesContext *context) {
    return dumpCST(context, "AngledValues");
};

std::any ASTConstructor::visitPrimEntity(OpenCMLParser::PrimEntityContext *context) {
    return dumpCST(context, "PrimEntity");
};

std::any ASTConstructor::visitMemberAccess(OpenCMLParser::MemberAccessContext *context) {
    return dumpCST(context, "MemberAccess");
};

std::any ASTConstructor::visitEntity(OpenCMLParser::EntityContext *context) { return dumpCST(context, "Entity"); };

std::any ASTConstructor::visitEntityChain(OpenCMLParser::EntityChainContext *context) {
    return dumpCST(context, "EntityChain");
};

std::any ASTConstructor::visitEntityLink(OpenCMLParser::EntityLinkContext *context) {
    return dumpCST(context, "EntityLink");
};

std::any ASTConstructor::visitEntityCall(OpenCMLParser::EntityCallContext *context) {
    return dumpCST(context, "EntityCall");
};

std::any ASTConstructor::visitEntitySpread(OpenCMLParser::EntitySpreadContext *context) {
    return dumpCST(context, "EntitySpread");
};

std::any ASTConstructor::visitEntityExpr(OpenCMLParser::EntityExprContext *context) {
    return dumpCST(context, "EntityExpr");
};

std::any ASTConstructor::visitRelaExpr(OpenCMLParser::RelaExprContext *context) {
    return dumpCST(context, "RelaExpr");
};

std::any ASTConstructor::visitAddExpr(OpenCMLParser::AddExprContext *context) { return dumpCST(context, "AddExpr"); };

std::any ASTConstructor::visitMultiExpr(OpenCMLParser::MultiExprContext *context) {
    return dumpCST(context, "MultiExpr");
};

std::any ASTConstructor::visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) {
    return dumpCST(context, "UnaryExpr");
};

std::any ASTConstructor::visitPrimExpr(OpenCMLParser::PrimExprContext *context) {
    return dumpCST(context, "PrimExpr");
};

std::any ASTConstructor::visitLiteral(OpenCMLParser::LiteralContext *context) { return dumpCST(context, "Literal"); };

std::any ASTConstructor::visitTypeExpr(OpenCMLParser::TypeExprContext *context) {
    return dumpCST(context, "TypeExpr");
};

std::any ASTConstructor::visitType(OpenCMLParser::TypeContext *context) { return dumpCST(context, "Type"); };

std::any ASTConstructor::visitLambdaType(OpenCMLParser::LambdaTypeContext *context) {
    return dumpCST(context, "LambdaType");
};

std::any ASTConstructor::visitPrimType(OpenCMLParser::PrimTypeContext *context) {
    return dumpCST(context, "PrimType");
};

std::any ASTConstructor::visitStructType(OpenCMLParser::StructTypeContext *context) {
    return dumpCST(context, "StructType");
};

std::any ASTConstructor::visitSpecialType(OpenCMLParser::SpecialTypeContext *context) {
    return dumpCST(context, "SpecialType");
};

std::any ASTConstructor::visitIdentRef(OpenCMLParser::IdentRefContext *context) {
    return dumpCST(context, "IdentRef");
};