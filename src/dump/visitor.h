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
#include <regex>
#include <string>

#include "antlr/OpenCMLVisitor.h"
#include "antlr4-runtime.h"

using namespace std;

class CSTDumpVisitor : public OpenCMLVisitor {
    int depth = 0;
    vector<bool> visible;

    any dumpCST(antlr4::tree::ParseTree *context, string nodeName) {
        bool isLast = false;
        if (visible.size() <= depth)
            visible.push_back(true);
        if (depth > 0) {
            auto siblings = context->parent->children;
            if (siblings[siblings.size() - 1] == context) {
                isLast = true;
                visible[depth - 1] = false;
            }
        }
        auto getHead = [=]() -> string {
            int i = 0;
            string ret = "";
            while (i < depth - 1) {
                if (visible[i])
                    ret += "|  ";
                else
                    ret += "   ";
                i++;
            }
            if (depth > 0) {
                if (isLast)
                    ret += "\\-";
                else
                    ret += "|-";
            }
            return ret;
        };
        cout << getHead();
        cout << nodeName;
        auto children = context->children;
        if (children.size() > 0) {
            if (children[0]->children.size() == 0) {
                string raw = children[0]->getText();
                string text =
                    std::regex_replace(raw, std::regex(R"(\n)"), "\\n");
                cout << " : " << text;
            }
        }
        cout << endl;
        if (depth > 0)
            for (int i = depth; i < visible.size(); i++)
                visible[i] = true;
        depth++;
        any parentResult = OpenCMLVisitor::visitChildren(context);
        depth--;
        return parentResult;
    };

    any visitProgram(OpenCMLParser::ProgramContext *context) {
        return dumpCST(context, "Program");
    };

    any visitStmtList(OpenCMLParser::StmtListContext *context) {
        return dumpCST(context, "StmtList");
    };

    any visitStmt(OpenCMLParser::StmtContext *context) {
        return dumpCST(context, "Stmt");
    };

    any visitLetStmt(OpenCMLParser::LetStmtContext *context) {
        return dumpCST(context, "LetStmt");
    };

    any visitUseStmt(OpenCMLParser::UseStmtContext *context) {
        return dumpCST(context, "UseStmt");
    };

    any visitWithDef(OpenCMLParser::WithDefContext *context) {
        return dumpCST(context, "WithDef");
    };

    any visitFuncDef(OpenCMLParser::FuncDefContext *context) {
        return dumpCST(context, "FuncDef");
    };

    any visitTypeDef(OpenCMLParser::TypeDefContext *context) {
        return dumpCST(context, "TypeDef");
    };

    any visitEnumDef(OpenCMLParser::EnumDefContext *context) {
        return dumpCST(context, "EnumDef");
    };

    any visitRetStmt(OpenCMLParser::RetStmtContext *context) {
        return dumpCST(context, "RetStmt");
    };

    any visitExprStmt(OpenCMLParser::ExprStmtContext *context) {
        return dumpCST(context, "ExprStmt");
    };

    any visitAssignStmt(OpenCMLParser::AssignStmtContext *context) {
        return dumpCST(context, "AssignStmt");
    };

    any visitCarrier(OpenCMLParser::CarrierContext *context) {
        return dumpCST(context, "Carrier");
    };

    any visitBiasAnno(OpenCMLParser::BiasAnnoContext *context) {
        return dumpCST(context, "BiasAnno");
    };

    any visitSizeAnno(OpenCMLParser::SizeAnnoContext *context) {
        return dumpCST(context, "SizeAnno");
    };

    any visitAnnotation(OpenCMLParser::AnnotationContext *context) {
        return dumpCST(context, "Annotation");
    };

    any visitAnnotations(OpenCMLParser::AnnotationsContext *context) {
        return dumpCST(context, "Annotations");
    };

    any visitModifiers(OpenCMLParser::ModifiersContext *context) {
        return dumpCST(context, "Modifiers");
    };

    any visitWithList(OpenCMLParser::WithListContext *context) {
        return dumpCST(context, "WithList");
    };

    any visitWithDecl(OpenCMLParser::WithDeclContext *context) {
        return dumpCST(context, "WithDecl");
    };

    any visitParamDef(OpenCMLParser::ParamDefContext *context) {
        return dumpCST(context, "ParamDef");
    };

    any visitArgsList(OpenCMLParser::ArgsListContext *context) {
        return dumpCST(context, "ArgsList");
    };

    any visitArgument(OpenCMLParser::ArgumentContext *context) {
        return dumpCST(context, "Argument");
    };

    any visitTypePack(OpenCMLParser::TypePackContext *context) {
        return dumpCST(context, "TypePack");
    };

    any visitKeyValDecl(OpenCMLParser::KeyValDeclContext *context) {
        return dumpCST(context, "KeyValDecl");
    };

    any visitKeyValExpr(OpenCMLParser::KeyValExprContext *context) {
        return dumpCST(context, "KeyValExpr");
    };

    any visitEntityRef(OpenCMLParser::EntityRefContext *context) {
        return dumpCST(context, "EntityRef");
    };

    any visitFunctorRef(OpenCMLParser::FunctorRefContext *context) {
        return dumpCST(context, "FunctorRef");
    };

    any visitListUnpack(OpenCMLParser::ListUnpackContext *context) {
        return dumpCST(context, "ListUnpack");
    };

    any visitDictUnpack(OpenCMLParser::DictUnpackContext *context) {
        return dumpCST(context, "DictUnpack");
    };

    any visitDictPack(OpenCMLParser::DictPackContext *context) {
        return dumpCST(context, "DictPack");
    };

    any visitListPack(OpenCMLParser::ListPackContext *context) {
        return dumpCST(context, "ListPack");
    };

    any visitStmtPack(OpenCMLParser::StmtPackContext *context) {
        return dumpCST(context, "StmtPack");
    };

    any visitLambda(OpenCMLParser::LambdaContext *context) {
        return dumpCST(context, "Lambda");
    };

    any visitEntityExpr(OpenCMLParser::EntityExprContext *context) {
        return dumpCST(context, "EntityExpr");
    };

    any visitEntityChain(OpenCMLParser::EntityChainContext *context) {
        return dumpCST(context, "EntityChain");
    };

    any visitEntity(OpenCMLParser::EntityContext *context) {
        return dumpCST(context, "Entity");
    };

    any visitNormCall(OpenCMLParser::NormCallContext *context) {
        return dumpCST(context, "NormCall");
    };

    any visitLinkCall(OpenCMLParser::LinkCallContext *context) {
        return dumpCST(context, "LinkCall");
    };

    any visitCalcExpr(OpenCMLParser::CalcExprContext *context) {
        return dumpCST(context, "CalcExpr");
    };

    any visitRelaExpr(OpenCMLParser::RelaExprContext *context) {
        return dumpCST(context, "RelaExpr");
    };

    any visitAddExpr(OpenCMLParser::AddExprContext *context) {
        return dumpCST(context, "AddExpr");
    };

    any visitMultiExpr(OpenCMLParser::MultiExprContext *context) {
        return dumpCST(context, "MultiExpr");
    };

    any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) {
        return dumpCST(context, "UnaryExpr");
    };

    any visitPrimExpr(OpenCMLParser::PrimExprContext *context) {
        return dumpCST(context, "PrimExpr");
    };

    any visitExpr(OpenCMLParser::ExprContext *context) {
        return dumpCST(context, "Expr");
    };

    any visitLiteral(OpenCMLParser::LiteralContext *context) {
        return dumpCST(context, "Literal");
    };

    any visitValue(OpenCMLParser::ValueContext *context) {
        return dumpCST(context, "Value");
    };

    any visitType(OpenCMLParser::TypeContext *context) {
        return dumpCST(context, "Type");
    };

    any visitInnerType(OpenCMLParser::InnerTypeContext *context) {
        return dumpCST(context, "InnerType");
    };

    any visitNumberType(OpenCMLParser::NumberTypeContext *context) {
        return dumpCST(context, "NumberType");
    };

    any visitScalarType(OpenCMLParser::ScalarTypeContext *context) {
        return dumpCST(context, "ScalarType");
    };

    any visitVectorType(OpenCMLParser::VectorTypeContext *context) {
        return dumpCST(context, "VectorType");
    };

    any visitStructType(OpenCMLParser::StructTypeContext *context) {
        return dumpCST(context, "StructType");
    };

    any visitIdentRef(OpenCMLParser::IdentRefContext *context) {
        return dumpCST(context, "IdentRef");
    };
};