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

    template <typename T> bool judgeMultiLine(const std::vector<T *> &list) {
        // force inline:
        // 1. if the list is empty
        // 2. if the list has only one element
        // 3. if the list is written in one line
        // (the last token of an element is in the same line as the first token of the next element)
    }

    template <typename T>
    std::any visitList(const std::vector<T *> &list, antlr4::ParserRuleContext *context, bool trailingComma = false,
                       std::string comma = ",", bool forceInline = false, bool forceMultiLine = false,
                       bool padding = true) {
        std::string result;
        const auto &firstLine = context->getStart()->getLine();
        auto lastLine = context->getStop()->getLine();
        // auto enable multi-line if the code is too long or the first line is not the same as the last line
        const bool multiLine =
            ((firstLine < lastLine || context->getText().size() > this->threshold) && !forceInline) || forceMultiLine;
        if (multiLine) {
            pushIndent();
            result += lineEnd();
        } else if (padding) {
            result += " ";
        }
        std::string separator = comma + (multiLine ? lineEnd() : " ");
        lastLine = list[0]->getStop()->getLine();
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
            result.erase(
                std::find_if(result.rbegin(), result.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
                result.end());
            if (trailingComma) {
                result += comma;
            }
            result += lineEnd();
        } else {
            result += trailingComma ? comma : "";
            result += padding ? " " : "";
        }
        return result;
    }

    std::any visitStmtList(OpenCMLParser::StmtListContext *context, bool forceInline, bool forceMultiLine,
                           bool padding);
    std::any visitAnnotations(OpenCMLParser::AnnotationsContext *context, bool multiLine);
    std::any visitTypeList(OpenCMLParser::TypeListContext *context, bool trailingComma, bool forceInline,
                           bool forceMultiLine, bool padding);
    std::any visitIdentList(OpenCMLParser::IdentListContext *context, bool trailingComma, bool forceInline,
                            bool forceMultiLine, bool padding);
    std::any visitValueList(OpenCMLParser::ValueListContext *context, bool trailingComma, bool forceInline,
                            bool forceMultiLine, bool padding);
    std::any visitPairedTypes(OpenCMLParser::PairedTypesContext *context, bool trailingComma, bool forceInline,
                              bool forceMultiLine, bool padding);
    std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context, bool trailingComma, bool forceInline,
                               bool forceMultiLine, bool padding);
    std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context, bool trailingComma, bool forceInline,
                               bool forceMultiLine, bool padding);
    std::any visitIndexKVPairs(OpenCMLParser::IndexKVPairsContext *context, bool trailingComma, bool forceInline,
                               bool forceMultiLine, bool padding);
    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context, bool trailingComma, bool forceInline,
                               bool forceMultiLine, bool padding);

  public:
    Formatter(bool compact = false) : compact(compact) {}

    std::any visitProgram(OpenCMLParser::ProgramContext *context);
    std::any visitStmtList(OpenCMLParser::StmtListContext *context) {
        return visitStmtList(context, false, false, true);
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
        return visitTypeList(context, false, false, false, true);
    }
    std::any visitIdentList(OpenCMLParser::IdentListContext *context) {
        return visitIdentList(context, false, false, false, true);
    }
    std::any visitValueList(OpenCMLParser::ValueListContext *context) {
        return visitValueList(context, false, false, false, true);
    }
    std::any visitPairedTypes(OpenCMLParser::PairedTypesContext *context) {
        return visitPairedTypes(context, false, false, false, true);
    }
    std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context) {
        return visitPairedValues(context, false, false, false, true);
    }
    std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context) {
        return visitPairedParams(context, false, false, false, true);
    }
    std::any visitIndexKVPairs(OpenCMLParser::IndexKVPairsContext *context) {
        return visitIndexKVPairs(context, false, false, false, true);
    }
    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context) {
        return visitArgumentList(context, false, false, false, true);
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