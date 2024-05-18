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
 * Created: May. 17, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include <algorithm>
#include <iostream>
#include <regex>
#include <string>

#include "antlr/OpenCMLVisitor.h"
#include "antlr4-runtime.h"

class Formatter : public OpenCMLVisitor {
  private:
    enum class QuotePreference { Single, Double };

    bool compact = false;
    std::string indent = "    ";
    std::string currentIndent = "";
    std::string newline = "\r\n";
    unsigned int threshold = 80;
    QuotePreference quote = QuotePreference::Single;

    int indentLevel = -1;

    void popIndent() {
        indentLevel--;
        currentIndent = currentIndent.substr(0, std::max(0, indentLevel) * indent.size());
    }
    void pushIndent() {
        indentLevel++;
        if (currentIndent.size() < indentLevel * indent.size()) {
            currentIndent += indent;
        }
    }

    std::string processStringLiteral(const std::string &input);

    inline std::string lineEnd() { return newline + currentIndent; }

    template <typename T>
    std::any visitList(const std::vector<T *> &list, antlr4::ParserRuleContext *context, bool trailingComma = false,
                       std::string comma = ",", bool padding = true, bool forceMultiLine = false) {
        std::string result;

        bool multiLine = false || forceMultiLine;
        bool wrappedWithBraces = false;

        if (!forceMultiLine) {
            // the elements are on one line if and only if
            // all elements' last token is on the same line
            // with the first token of the next element
            auto lastEleLine = list[0]->getStop()->getLine();
            for (int i = 1; i < list.size(); i++) {
                const auto &currEleLine = list[i]->getStart()->getLine();
                if (currEleLine > lastEleLine) {
                    multiLine = true;
                    break;
                }
                lastEleLine = list[i]->getStop()->getLine();
            }
        }

        const std::string snippet = context->getText();
        const std::string firstChar = snippet.substr(0, 1);
        const std::string lastChar = snippet.substr(snippet.size() - 1, 1);
        if (firstChar == lastChar) {
            if (firstChar == "{" || firstChar == "[" || firstChar == "<" || firstChar == "(") {
                wrappedWithBraces = true;
            }
        }

        if (multiLine) {
            pushIndent();
            if (!wrappedWithBraces) {
                result += lineEnd();
            }
        } else if (padding) {
            result += " ";
        }

        std::string separator = comma + (multiLine ? lineEnd() : " ");

        auto lastLine = list[0]->getStop()->getLine();
        for (size_t i = 0; i < list.size(); i++) {
            const auto &currLine = list[i]->getStart()->getLine();
            if (i == 0) {
                result += std::any_cast<std::string>(visit(list[i]));
            } else {
                result += separator;
                if (currLine > lastLine + 1)
                    result += lineEnd();
                result += std::any_cast<std::string>(visit(list[i]));
                lastLine = list[i]->getStop()->getLine();
            }
        }

        if (multiLine) {
            popIndent();
            if (trailingComma) {
                result += comma;
            }
            if (!wrappedWithBraces) {
                result += lineEnd();
            }
        } else {
            result += trailingComma ? comma : "";
            result += padding ? " " : "";
        }

        return result;
    }

    std::any visitStmtList(OpenCMLParser::StmtListContext *context, bool padding, bool forceMultiLine,
                           bool trailingComma);
    std::any visitAnnotations(OpenCMLParser::AnnotationsContext *context, bool multiLine);
    std::any visitTypeList(OpenCMLParser::TypeListContext *context, bool trailingComma, bool padding,
                           bool forceMultiLine);
    std::any visitIdentList(OpenCMLParser::IdentListContext *context, bool trailingComma, bool padding,
                            bool forceMultiLine);
    std::any visitValueList(OpenCMLParser::ValueListContext *context, bool trailingComma, bool padding,
                            bool forceMultiLine);
    std::any visitPairedTypes(OpenCMLParser::PairedTypesContext *context, bool trailingComma, bool padding,
                              bool forceMultiLine);
    std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context, bool trailingComma, bool padding,
                               bool forceMultiLine);
    std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context, bool trailingComma, bool padding,
                               bool forceMultiLine);
    std::any visitIndexKVPairs(OpenCMLParser::IndexKVPairsContext *context, bool trailingComma, bool padding,
                               bool forceMultiLine);
    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context, bool trailingComma, bool padding,
                               bool forceMultiLine);

  public:
    Formatter(bool compact = false) : compact(compact) {}

    std::any visitProgram(OpenCMLParser::ProgramContext *context);
    std::any visitStmtList(OpenCMLParser::StmtListContext *context) {
        return visitStmtList(context, true, false, true);
    }
    std::any visitStmt(OpenCMLParser::StmtContext *context);
    std::any visitLetStmt(OpenCMLParser::LetStmtContext *context);
    std::any visitUseStmt(OpenCMLParser::UseStmtContext *context);
    std::any visitTypeStmt(OpenCMLParser::TypeStmtContext *context);
    std::any visitExprStmt(OpenCMLParser::ExprStmtContext *context);
    std::any visitAssignStmt(OpenCMLParser::AssignStmtContext *context);
    std::any visitWithDef(OpenCMLParser::WithDefContext *context);
    std::any visitFuncDef(OpenCMLParser::FuncDefContext *context);
    std::any visitRetStmt(OpenCMLParser::RetStmtContext *context);
    std::any visitLambdaExpr(OpenCMLParser::LambdaExprContext *context);
    std::any visitCarrier(OpenCMLParser::CarrierContext *context);
    std::any visitAnnotation(OpenCMLParser::AnnotationContext *context);
    std::any visitAnnotations(OpenCMLParser::AnnotationsContext *context) { return visitAnnotations(context, false); }
    std::any visitModifiers(OpenCMLParser::ModifiersContext *context);
    std::any visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context);
    std::any visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context);
    std::any visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context);
    std::any visitIndexKTPair(OpenCMLParser::IndexKTPairContext *context);
    std::any visitIndexKVPair(OpenCMLParser::IndexKVPairContext *context);
    std::any visitTypeList(OpenCMLParser::TypeListContext *context) {
        return visitTypeList(context, false, true, false);
    }
    std::any visitIdentList(OpenCMLParser::IdentListContext *context) {
        return visitIdentList(context, false, true, false);
    }
    std::any visitValueList(OpenCMLParser::ValueListContext *context) {
        return visitValueList(context, false, true, false);
    }
    std::any visitPairedTypes(OpenCMLParser::PairedTypesContext *context) {
        return visitPairedTypes(context, false, true, false);
    }
    std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context) {
        return visitPairedValues(context, false, true, false);
    }
    std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context) {
        return visitPairedParams(context, false, true, false);
    }
    std::any visitIndexKVPairs(OpenCMLParser::IndexKVPairsContext *context) {
        return visitIndexKVPairs(context, false, true, false);
    }
    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context) {
        return visitArgumentList(context, false, true, false);
    }
    std::any visitBracedValues(OpenCMLParser::BracedValuesContext *context);
    std::any visitBracedIndexKVPairs(OpenCMLParser::BracedIndexKVPairsContext *context);
    std::any visitBracedPairedValues(OpenCMLParser::BracedPairedValuesContext *context);
    std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *context);
    std::any visitBracedStmts(OpenCMLParser::BracedStmtsContext *context);
    std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *context);
    std::any visitBracketValues(OpenCMLParser::BracketValuesContext *context);
    std::any visitParentParams(OpenCMLParser::ParentParamsContext *context);
    std::any visitParentValues(OpenCMLParser::ParentValuesContext *context);
    std::any visitAngledParams(OpenCMLParser::AngledParamsContext *context);
    std::any visitAngledValues(OpenCMLParser::AngledValuesContext *context);
    std::any visitPrimEntity(OpenCMLParser::PrimEntityContext *context);
    std::any visitMemberAccess(OpenCMLParser::MemberAccessContext *context);
    std::any visitEntity(OpenCMLParser::EntityContext *context);
    std::any visitEntityChain(OpenCMLParser::EntityChainContext *context);
    std::any visitEntityLink(OpenCMLParser::EntityLinkContext *context);
    std::any visitEntityCall(OpenCMLParser::EntityCallContext *context);
    std::any visitEntitySpread(OpenCMLParser::EntitySpreadContext *context);
    std::any visitEntityExpr(OpenCMLParser::EntityExprContext *context);
    std::any visitRelaExpr(OpenCMLParser::RelaExprContext *context);
    std::any visitAddExpr(OpenCMLParser::AddExprContext *context);
    std::any visitMultiExpr(OpenCMLParser::MultiExprContext *context);
    std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context);
    std::any visitPrimExpr(OpenCMLParser::PrimExprContext *context);
    std::any visitLiteral(OpenCMLParser::LiteralContext *context);
    std::any visitTypeExpr(OpenCMLParser::TypeExprContext *context);
    std::any visitType(OpenCMLParser::TypeContext *context);
    std::any visitPrimType(OpenCMLParser::PrimTypeContext *context);
    std::any visitStructType(OpenCMLParser::StructTypeContext *context);
    std::any visitSpecialType(OpenCMLParser::SpecialTypeContext *context);
    std::any visitIdentRef(OpenCMLParser::IdentRefContext *context);
};