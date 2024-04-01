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
        auto children = context->children;

        if (visible.size() <= depth)
            visible.push_back(true);

        if (depth > 0) {
            auto siblings = context->parent->children;
            if (siblings[siblings.size() - 1] == context) {
                isLast = true;
                visible[depth - 1] = false;
            }
        }

        auto getHead = [=](bool last) -> string {
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
                if (last)
                    ret += "\\-";
                else
                    ret += "|-";
            }
            return ret;
        };

        cout << getHead(isLast);
        cout << nodeName;
        if (children.size() == 0) {
            string raw = context->getText();
            string text = std::regex_replace(raw, std::regex(R"(\n)"), "\\n");
            cout << " " << text;
        }
        cout << endl;

        if (depth > 0)
            for (int i = depth; i < visible.size(); i++)
                visible[i] = true;

        depth++;

        std::any result = defaultResult();
        size_t n = children.size();
        for (size_t i = 0; i < n; i++) {
            if (children[i]->getTreeType() ==
                antlr4::tree::ParseTreeType::RULE) {
                std::any childResult = context->children[i]->accept(this);
                result =
                    aggregateResult(std::move(result), std::move(childResult));
            } else {
                dumpCST(context->children[i], "");
            }
        }

        depth--;

        return result;
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

    any visitExprStmt(OpenCMLParser::ExprStmtContext *context) {
        return dumpCST(context, "ExprStmt");
    };

    any visitAssignStmt(OpenCMLParser::AssignStmtContext *context) {
        return dumpCST(context, "AssignStmt");
    };

    any visitWithDef(OpenCMLParser::WithDefContext *context) {
        return dumpCST(context, "WithDef");
    };

    any visitFuncDef(OpenCMLParser::FuncDefContext *context) {
        return dumpCST(context, "FuncDef");
    };

    any visitRetStmt(OpenCMLParser::RetStmtContext *context) {
        return dumpCST(context, "RetStmt");
    };

    any visitLambda(OpenCMLParser::LambdaContext *context) {
        return dumpCST(context, "Lambda");
    };

    any visitCarrier(OpenCMLParser::CarrierContext *context) {
        return dumpCST(context, "Carrier");
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

    any visitArgument(OpenCMLParser::ArgumentContext *context) {
        return dumpCST(context, "Argument");
    };

    any visitArgsList(OpenCMLParser::ArgsListContext *context) {
        return dumpCST(context, "ArgsList");
    };

    any visitExprList(OpenCMLParser::ExprListContext *context) {
        return dumpCST(context, "ExprList");
    };

    any visitKeyValExpr(OpenCMLParser::KeyValExprContext *context) {
        return dumpCST(context, "KeyValExpr");
    };

    any visitKeyValList(OpenCMLParser::KeyValListContext *context) {
        return dumpCST(context, "KeyValList");
    };

    any visitDictPack(OpenCMLParser::DictPackContext *context) {
        return dumpCST(context, "DictPack");
    };

    any visitListPack(OpenCMLParser::ListPackContext *context) {
        return dumpCST(context, "ListPack");
    };

    any visitWithDecl(OpenCMLParser::WithDeclContext *context) {
        return dumpCST(context, "WithDecl");
    };

    any visitWithPack(OpenCMLParser::WithPackContext *context) {
        return dumpCST(context, "WithPack");
    };

    any visitArgsDecl(OpenCMLParser::ArgsDeclContext *context) {
        return dumpCST(context, "ArgsDecl");
    };

    any visitArgsPack(OpenCMLParser::ArgsPackContext *context) {
        return dumpCST(context, "ArgsPack");
    };

    any visitStmtPack(OpenCMLParser::StmtPackContext *context) {
        return dumpCST(context, "StmtPack");
    };

    any visitPrimEntity(OpenCMLParser::PrimEntityContext *context) {
        return dumpCST(context, "PrimEntity");
    };

    any visitMemberAccess(OpenCMLParser::MemberAccessContext *context) {
        return dumpCST(context, "MemberAccess");
    };

    any visitEntity(OpenCMLParser::EntityContext *context) {
        return dumpCST(context, "Entity");
    };

    any visitEntityLink(OpenCMLParser::EntityLinkContext *context) {
        return dumpCST(context, "EntityLink");
    };

    any visitEntityChain(OpenCMLParser::EntityChainContext *context) {
        return dumpCST(context, "EntityChain");
    };

    any visitEntityUnpack(OpenCMLParser::EntityUnpackContext *context) {
        return dumpCST(context, "EntityUnpack");
    };

    any visitEntityExpr(OpenCMLParser::EntityExprContext *context) {
        return dumpCST(context, "EntityExpr");
    };

    any visitExpr(OpenCMLParser::ExprContext *context) {
        return dumpCST(context, "Expr");
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