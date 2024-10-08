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
 * Created: Mar. 17, 2024
 * Updated: Oct. 07, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include <iostream>
#include <regex>
#include <string>

#include "antlr/OpenCMLVisitor.h"
#include "antlr4-runtime.h"

class CSTDumpVisitor : public OpenCMLVisitor {
    int depth = 0;
    std::vector<bool> visible;

    std::any dumpCST(antlr4::tree::ParseTree *context, std::string nodeName);

    std::any visitProgram(OpenCMLParser::ProgramContext *context) { return dumpCST(context, "Program"); };

    std::any visitStmtList(OpenCMLParser::StmtListContext *context) { return dumpCST(context, "StmtList"); };

    std::any visitStmt(OpenCMLParser::StmtContext *context) { return dumpCST(context, "Stmt"); };

    std::any visitLetStmt(OpenCMLParser::LetStmtContext *context) { return dumpCST(context, "LetStmt"); };

    std::any visitUseStmt(OpenCMLParser::UseStmtContext *context) { return dumpCST(context, "UseStmt"); };

    std::any visitTypeStmt(OpenCMLParser::TypeStmtContext *context) { return dumpCST(context, "TypeStmt"); };

    std::any visitExprStmt(OpenCMLParser::ExprStmtContext *context) { return dumpCST(context, "ExprStmt"); };

    std::any visitWaitStmt(OpenCMLParser::WaitStmtContext *context) { return dumpCST(context, "WaitStmt"); };

    std::any visitWithDef(OpenCMLParser::WithDefContext *context) { return dumpCST(context, "WithDef"); };

    std::any visitFuncDef(OpenCMLParser::FuncDefContext *context) { return dumpCST(context, "FuncDef"); };

    std::any visitRetStmt(OpenCMLParser::RetStmtContext *context) { return dumpCST(context, "RetStmt"); };

    std::any visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) { return dumpCST(context, "LambdaExpr"); };

    std::any visitCarrier(OpenCMLParser::CarrierContext *context) { return dumpCST(context, "Carrier"); };

    std::any visitAnnotation(OpenCMLParser::AnnotationContext *context) { return dumpCST(context, "Annotation"); };

    std::any visitAnnotations(OpenCMLParser::AnnotationsContext *context) { return dumpCST(context, "Annotations"); };

    std::any visitModifiers(OpenCMLParser::ModifiersContext *context) { return dumpCST(context, "Modifiers"); };

    std::any visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) { return dumpCST(context, "KeyTypePair"); };

    std::any visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) {
        return dumpCST(context, "KeyValuePair");
    };

    std::any visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) {
        return dumpCST(context, "KeyParamPair");
    };

    std::any visitIndexKTPair(OpenCMLParser::IndexKTPairContext *context) { return dumpCST(context, "IndexKTPair"); };

    std::any visitIndexKVPair(OpenCMLParser::IndexKVPairContext *context) { return dumpCST(context, "IndexKVPair"); };

    std::any visitTypeList(OpenCMLParser::TypeListContext *context) { return dumpCST(context, "TypeList"); };

    std::any visitIdentList(OpenCMLParser::IdentListContext *context) { return dumpCST(context, "IdentList"); };

    std::any visitValueList(OpenCMLParser::ValueListContext *context) { return dumpCST(context, "ValueList"); };

    std::any visitPairedTypes(OpenCMLParser::PairedTypesContext *context) { return dumpCST(context, "PairedTypes"); };

    std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context) {
        return dumpCST(context, "PairedValues");
    };

    std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context) {
        return dumpCST(context, "PairedParams");
    };

    std::any visitIndexKVPairs(OpenCMLParser::IndexKVPairsContext *context) {
        return dumpCST(context, "IndexKVPairs");
    };

    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context) {
        return dumpCST(context, "ArgumentList");
    };

    std::any visitBracedPairedValues(OpenCMLParser::BracedPairedValuesContext *context) {
        return dumpCST(context, "BracedPairedValues");
    };

    std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) {
        return dumpCST(context, "BracedIdents");
    };

    std::any visitBracedStmts(OpenCMLParser::BracedStmtsContext *context) { return dumpCST(context, "BracedStmts"); };

    std::any visitBracedValues(OpenCMLParser::BracedValuesContext *context) {
        return dumpCST(context, "BracedValues");
    };

    std::any visitBracedIndexKVPairs(OpenCMLParser::BracedIndexKVPairsContext *context) {
        return dumpCST(context, "BracedIndexKVPairs");
    };

    std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) {
        return dumpCST(context, "BracketIdents");
    };

    std::any visitBracketValues(OpenCMLParser::BracketValuesContext *context) {
        return dumpCST(context, "BracketValues");
    };

    std::any visitMemberAccess(OpenCMLParser::MemberAccessContext *context) {
        return dumpCST(context, "MemberAccess");
    };

    std::any visitParentParams(OpenCMLParser::ParentParamsContext *context) {
        return dumpCST(context, "ParentParams");
    };

    std::any visitParentArgues(OpenCMLParser::ParentArguesContext *context) {
        return dumpCST(context, "ParentArgues");
    };

    std::any visitParentValues(OpenCMLParser::ParentValuesContext *context) {
        return dumpCST(context, "ParentValues");
    };

    std::any visitAngledParams(OpenCMLParser::AngledParamsContext *context) {
        return dumpCST(context, "AngledParams");
    };

    std::any visitAngledValues(OpenCMLParser::AngledValuesContext *context) {
        return dumpCST(context, "AngledValues");
    };

    std::any visitEntityExpr(OpenCMLParser::EntityExprContext *context) { return dumpCST(context, "EntityExpr"); };

    std::any visitTernaryExpr(OpenCMLParser::TernaryExprContext *context) { return dumpCST(context, "TernaryExpr"); };

    std::any visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) {
        return dumpCST(context, "LogicalOrExpr");
    };

    std::any visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) {
        return dumpCST(context, "LogicalAndExpr");
    };

    std::any visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) {
        return dumpCST(context, "EqualityExpr");
    };

    std::any visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) {
        return dumpCST(context, "RelationalExpr");
    };

    std::any visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) {
        return dumpCST(context, "AdditiveExpr");
    };

    std::any visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) {
        return dumpCST(context, "MultiplicativeExpr");
    };

    std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) { return dumpCST(context, "UnaryExpr"); };

    std::any visitLinkExpr(OpenCMLParser::LinkExprContext *context) { return dumpCST(context, "LinkExpr"); };

    std::any visitWithExpr(OpenCMLParser::WithExprContext *context) { return dumpCST(context, "WithExpr"); };

    std::any visitAnnotatedExpr(OpenCMLParser::AnnotatedExprContext *context) {
        return dumpCST(context, "AnnotatedExpr");
    };

    std::any visitPrimaryExpr(OpenCMLParser::PrimaryExprContext *context) { return dumpCST(context, "PrimaryExpr"); };

    std::any visitLiteral(OpenCMLParser::LiteralContext *context) { return dumpCST(context, "Literal"); };

    std::any visitTypeExpr(OpenCMLParser::TypeExprContext *context) { return dumpCST(context, "TypeExpr"); };

    std::any visitArrayType(OpenCMLParser::ArrayTypeContext *context) { return dumpCST(context, "ArrayType"); };

    std::any visitAtomType(OpenCMLParser::AtomTypeContext *context) { return dumpCST(context, "AtomType"); };

    std::any visitLambdaType(OpenCMLParser::LambdaTypeContext *context) { return dumpCST(context, "LambdaType"); };

    std::any visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) { return dumpCST(context, "PrimaryType"); };

    std::any visitStructType(OpenCMLParser::StructTypeContext *context) { return dumpCST(context, "StructType"); };

    std::any visitSpecialType(OpenCMLParser::SpecialTypeContext *context) { return dumpCST(context, "SpecialType"); };

    std::any visitIdentRef(OpenCMLParser::IdentRefContext *context) { return dumpCST(context, "IdentRef"); };
};
