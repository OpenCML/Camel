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

class CSTDumpVisitor : public OpenCMLVisitor {
    int depth = 0;
    std::vector<bool> visible;

    std::any dumpCST(antlr4::tree::ParseTree *context, std::string nodeName) {
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

        auto getHead = [=](bool last) -> std::string {
            int i = 0;
            std::string ret = "";
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

        std::cout << getHead(isLast);
        std::cout << nodeName;
        if (children.size() == 0) {
            std::string raw = context->getText();
            std::string text = std::regex_replace(raw, std::regex(R"(\n)"), "\\n");
            std::cout << " " << text;
        }
        std::cout << std::endl;

        if (depth > 0)
            for (int i = depth; i < visible.size(); i++)
                visible[i] = true;

        depth++;

        std::any result = defaultResult();
        size_t n = children.size();
        for (size_t i = 0; i < n; i++) {
            if (children[i]->getTreeType() == antlr4::tree::ParseTreeType::RULE) {
                std::any childResult = context->children[i]->accept(this);
                result = aggregateResult(std::move(result), std::move(childResult));
            } else {
                dumpCST(context->children[i], "");
            }
        }

        depth--;

        return result;
    };

    std::any visitProgram(OpenCMLParser::ProgramContext *context) { return dumpCST(context, "Program"); };

    std::any visitStmtList(OpenCMLParser::StmtListContext *context) { return dumpCST(context, "StmtList"); };

    std::any visitStmt(OpenCMLParser::StmtContext *context) { return dumpCST(context, "Stmt"); };

    std::any visitLetStmt(OpenCMLParser::LetStmtContext *context) { return dumpCST(context, "LetStmt"); };

    std::any visitUseStmt(OpenCMLParser::UseStmtContext *context) { return dumpCST(context, "UseStmt"); };

    std::any visitTypeStmt(OpenCMLParser::TypeStmtContext *context) { return dumpCST(context, "TypeStmt"); };

    std::any visitExprStmt(OpenCMLParser::ExprStmtContext *context) { return dumpCST(context, "ExprStmt"); };

    std::any visitAssignStmt(OpenCMLParser::AssignStmtContext *context) { return dumpCST(context, "AssignStmt"); };

    std::any visitWithDef(OpenCMLParser::WithDefContext *context) { return dumpCST(context, "WithDef"); };

    std::any visitFuncDef(OpenCMLParser::FuncDefContext *context) { return dumpCST(context, "FuncDef"); };

    std::any visitRetStmt(OpenCMLParser::RetStmtContext *context) { return dumpCST(context, "RetStmt"); };

    std::any visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) { return dumpCST(context, "LambdaExpr"); };

    std::any visitCarrier(OpenCMLParser::CarrierContext *context) { return dumpCST(context, "Carrier"); };

    std::any visitAnnotation(OpenCMLParser::AnnotationContext *context) { return dumpCST(context, "Annotation"); };

    std::any visitAnnotations(OpenCMLParser::AnnotationsContext *context) { return dumpCST(context, "Annotations"); };

    std::any visitModifiers(OpenCMLParser::ModifiersContext *context) { return dumpCST(context, "Modifiers"); };

    std::any visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) { return dumpCST(context, "KeyTypePair"); };

    std::any visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) { return dumpCST(context, "KeyValuePair"); };

    std::any visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) { return dumpCST(context, "KeyParamPair"); };

    std::any visitTypeList(OpenCMLParser::TypeListContext *context) { return dumpCST(context, "TypeList"); };

    std::any visitIdentList(OpenCMLParser::IdentListContext *context) { return dumpCST(context, "IdentList"); };

    std::any visitValueList(OpenCMLParser::ValueListContext *context) { return dumpCST(context, "ValueList"); };

    std::any visitPairedTypes(OpenCMLParser::PairedTypesContext *context) { return dumpCST(context, "PairedTypes"); };

    std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context) { return dumpCST(context, "PairedValues"); };

    std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context) { return dumpCST(context, "PairedParams"); };

    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context) { return dumpCST(context, "ArgumentList"); };

    std::any visitBracedPairedValues(OpenCMLParser::BracedPairedValuesContext *context) {
        return dumpCST(context, "BracedPairedValues");
    };

    std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) { return dumpCST(context, "BracedIdents"); };

    std::any visitBracedStmts(OpenCMLParser::BracedStmtsContext *context) { return dumpCST(context, "BracedStmts"); };

    std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) { return dumpCST(context, "BracketIdents"); };

    std::any visitBracketValues(OpenCMLParser::BracketValuesContext *context) { return dumpCST(context, "BracketValues"); };

    std::any visitParentParams(OpenCMLParser::ParentParamsContext *context) { return dumpCST(context, "ParentParams"); };

    std::any visitParentValues(OpenCMLParser::ParentValuesContext *context) { return dumpCST(context, "ParentValues"); };

    std::any visitAngledParams(OpenCMLParser::AngledParamsContext *context) { return dumpCST(context, "AngledParams"); };

    std::any visitAngledValues(OpenCMLParser::AngledValuesContext *context) { return dumpCST(context, "AngledValues"); };

    std::any visitPrimEntity(OpenCMLParser::PrimEntityContext *context) { return dumpCST(context, "PrimEntity"); };

    std::any visitMemberAccess(OpenCMLParser::MemberAccessContext *context) { return dumpCST(context, "MemberAccess"); };

    std::any visitEntityChain(OpenCMLParser::EntityChainContext *context) { return dumpCST(context, "EntityChain"); };

    std::any visitEntityLink(OpenCMLParser::EntityLinkContext *context) { return dumpCST(context, "EntityLink"); };

    std::any visitEntityUnit(OpenCMLParser::EntityUnitContext *context) { return dumpCST(context, "EntityUnit"); };

    std::any visitEntityWith(OpenCMLParser::EntityWithContext *context) { return dumpCST(context, "EntityWith"); };

    std::any visitEntityExpr(OpenCMLParser::EntityExprContext *context) { return dumpCST(context, "EntityExpr"); };

    std::any visitRelaExpr(OpenCMLParser::RelaExprContext *context) { return dumpCST(context, "RelaExpr"); };

    std::any visitAddExpr(OpenCMLParser::AddExprContext *context) { return dumpCST(context, "AddExpr"); };

    std::any visitMultiExpr(OpenCMLParser::MultiExprContext *context) { return dumpCST(context, "MultiExpr"); };

    std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) { return dumpCST(context, "UnaryExpr"); };

    std::any visitPrimExpr(OpenCMLParser::PrimExprContext *context) { return dumpCST(context, "PrimExpr"); };

    std::any visitLiteral(OpenCMLParser::LiteralContext *context) { return dumpCST(context, "Literal"); };

    std::any visitTypeExpr(OpenCMLParser::TypeExprContext *context) { return dumpCST(context, "TypeExpr"); };

    std::any visitUnaryType(OpenCMLParser::UnaryTypeContext *context) { return dumpCST(context, "UnaryType"); };

    std::any visitAtomType(OpenCMLParser::AtomTypeContext *context) { return dumpCST(context, "AtomType"); };

    std::any visitLambdaType(OpenCMLParser::LambdaTypeContext *context) { return dumpCST(context, "LambdaType"); };

    std::any visitPrimType(OpenCMLParser::PrimTypeContext *context) { return dumpCST(context, "PrimType"); };

    std::any visitStructType(OpenCMLParser::StructTypeContext *context) { return dumpCST(context, "StructType"); };

    std::any visitSpecialType(OpenCMLParser::SpecialTypeContext *context) { return dumpCST(context, "SpecialType"); };

    std::any visitIdentRef(OpenCMLParser::IdentRefContext *context) { return dumpCST(context, "IdentRef"); };
};