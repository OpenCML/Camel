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
 * Created: Mar. 17, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include <iostream>

#include "antlr/OpenCMLVisitor.h"

using namespace std;

class CSTDumpVisitor : public OpenCMLVisitor {
    std::any visitProgram(OpenCMLParser::ProgramContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitStmtList(OpenCMLParser::StmtListContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitStmt(OpenCMLParser::StmtContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitLetStmt(OpenCMLParser::LetStmtContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitUseStmt(OpenCMLParser::UseStmtContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitWithDef(OpenCMLParser::WithDefContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitFuncDef(OpenCMLParser::FuncDefContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitTypeDef(OpenCMLParser::TypeDefContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitEnumDef(OpenCMLParser::EnumDefContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitRetStmt(OpenCMLParser::RetStmtContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitExprStmt(OpenCMLParser::ExprStmtContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitAssignStmt(OpenCMLParser::AssignStmtContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitCarrier(OpenCMLParser::CarrierContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitBiasAnno(OpenCMLParser::BiasAnnoContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitSizeAnno(OpenCMLParser::SizeAnnoContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitAnnotation(OpenCMLParser::AnnotationContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitAnnotations(OpenCMLParser::AnnotationsContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitModifiers(OpenCMLParser::ModifiersContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitWithList(OpenCMLParser::WithListContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitWithDecl(OpenCMLParser::WithDeclContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitParamDef(OpenCMLParser::ParamDefContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitArgsList(OpenCMLParser::ArgsListContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitArgument(OpenCMLParser::ArgumentContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitTypePack(OpenCMLParser::TypePackContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitKeyValDecl(OpenCMLParser::KeyValDeclContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitKeyValExpr(OpenCMLParser::KeyValExprContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitEntityRef(OpenCMLParser::EntityRefContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitFunctorRef(OpenCMLParser::FunctorRefContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitListUnpack(OpenCMLParser::ListUnpackContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitDictUnpack(OpenCMLParser::DictUnpackContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitDictPack(OpenCMLParser::DictPackContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitListPack(OpenCMLParser::ListPackContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitStmtPack(OpenCMLParser::StmtPackContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitLambda(OpenCMLParser::LambdaContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitEntityExpr(OpenCMLParser::EntityExprContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitEntityChain(OpenCMLParser::EntityChainContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitEntity(OpenCMLParser::EntityContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitNormCall(OpenCMLParser::NormCallContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitLinkCall(OpenCMLParser::LinkCallContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitCalcExpr(OpenCMLParser::CalcExprContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitRelaExpr(OpenCMLParser::RelaExprContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitAddExpr(OpenCMLParser::AddExprContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitMultiExpr(OpenCMLParser::MultiExprContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitPrimExpr(OpenCMLParser::PrimExprContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitExpr(OpenCMLParser::ExprContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitLiteral(OpenCMLParser::LiteralContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitValue(OpenCMLParser::ValueContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitType(OpenCMLParser::TypeContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitInnerType(OpenCMLParser::InnerTypeContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitNumberType(OpenCMLParser::NumberTypeContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitScalarType(OpenCMLParser::ScalarTypeContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitVectorType(OpenCMLParser::VectorTypeContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitStructType(OpenCMLParser::StructTypeContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };

    std::any visitIdentRef(OpenCMLParser::IdentRefContext *context) {
        cout << context->getText() << endl;
        return visitChildren(context);
    };
};