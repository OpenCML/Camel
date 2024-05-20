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
    enum class CommentPreference { Hash, Slash };

    bool compact = false;
    std::string indent = "    ";
    std::string currentIndent = "";
    std::string newline = "\r\n";
    unsigned int threshold = 80;
    QuotePreference quotePrefer = QuotePreference::Single;
    CommentPreference cmtPrefer = CommentPreference::Hash;

    const std::vector<antlr4::Token *> tokens;

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

    inline size_t countLines(const std::string &input) { return std::count(input.begin(), input.end(), '\n'); }

    inline std::string slash2hash(const std::string &input) { return std::regex_replace(input, std::regex("//"), "#"); }

    inline std::string hash2slash(const std::string &input) { return std::regex_replace(input, std::regex("#"), "//"); }

    template <typename T>
    std::any formatList(const std::vector<T *> &list, antlr4::ParserRuleContext *context, bool trailingComma = false,
                        std::string comma = ",", bool padding = true, bool forceMultiLine = false) {
        std::string result;

        const auto findCmtRange = [&](size_t start, bool inverse = false) -> std::pair<size_t, size_t> {
            size_t predCmtStart = start;
            size_t predCmtEnd = start;

            if (inverse) {
                if (predCmtEnd > 0) {
                    size_t j;
                    for (j = predCmtEnd - 1; j != 0; j--) {
                        if (tokens[j]->getChannel() > 1) {
                            predCmtStart = j;
                        } else {
                            break;
                        }
                    }
                    if (j == 0 && tokens[0]->getChannel() > 1) {
                        predCmtStart = 0;
                    }
                }
            } else {
                if (++predCmtStart < tokens.size()) {
                    if (tokens[predCmtStart]->getText() == comma) {
                        predCmtStart++;
                    }
                    if (predCmtStart < tokens.size() && tokens[predCmtStart]->getChannel() > 1) {
                        for (size_t j = predCmtStart; j <= tokens.size(); j++) {
                            if (j == tokens.size() || tokens[j]->getChannel() <= 1) {
                                predCmtEnd = j;
                                break;
                            }
                        }
                    }
                }
            }

            return std::make_pair(predCmtStart, predCmtEnd);
        };

        const auto insertComment = [&](const antlr4::Token *comment) {
            const auto &commentText = comment->getText();
            switch (comment->getChannel()) {
            case 2: {
                if (cmtPrefer == CommentPreference::Slash)
                    result += hash2slash(commentText);
                else
                    result += commentText;
            } break;
            case 3: {
                if (cmtPrefer == CommentPreference::Hash)
                    result += slash2hash(commentText);
                else
                    result += commentText;
            } break;
            case 4:
                result += commentText + " ";
                break;

            default:
                break;
            }
        };

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

        // check if the list is wrapped with braces
        // if so, we don't need to add line breaks before and after the list
        // that makes the code more compact
        const std::string snippet = context->getText();
        const std::string firstChar = snippet.substr(0, 1);
        const std::string lastChar = snippet.substr(snippet.size() - 1, 1);
        if (firstChar == "{" && lastChar == "}") {
            wrappedWithBraces = true;
        } else if (firstChar == "[" && lastChar == "]") {
            wrappedWithBraces = true;
        } else if (firstChar == "<" && lastChar == ">") {
            wrappedWithBraces = true;
        } else if (firstChar == "(" && lastChar == ")") {
            wrappedWithBraces = true;
        }

        if (multiLine) {
            // if wrapped with braces
            // we don't need to add line breaks before and after the list
            // and we don't need to add padding spaces
            // it's better to use the indent level of the outer context
            if (!wrappedWithBraces) {
                pushIndent();
                result += lineEnd();
            }
        } else if (padding) {
            result += " ";
        }

        int lastCommentLines = 0;
        size_t lastLine = list[0]->getStop()->getLine();

        for (size_t i = 0; i < list.size(); i++) {
            const size_t currLine = list[i]->getStart()->getLine();
            int remainedSkips = currLine - lastLine - lastCommentLines;

            if (multiLine && i != 0) {
                // sometimes the elements are written in one line
                // however, we want to keep them in separate lines
                // if is the first element of a multi-line list
                // we already have a line break before the first element
                remainedSkips = std::max(remainedSkips, 1);
            }

            for (int j = 0; j < remainedSkips; j++) {
                result += lineEnd();
            }

            size_t lastStopLine = list[i]->getStop()->getLine();

            if (i == 0) {
                auto [predCmtStart, predCmtEnd] = findCmtRange(list[i]->getStart()->getTokenIndex(), true);
                int cmtSkips = 0;

                for (size_t j = predCmtStart; j < predCmtEnd; j++) {
                    const auto &comment = tokens[j];
                    const auto &commentText = comment->getText();

                    cmtSkips = comment->getLine() - lastStopLine;

                    for (int k = 0; k < cmtSkips; k++) {
                        result += lineEnd();
                    }

                    insertComment(comment);

                    lastStopLine = comment->getLine() + countLines(commentText);
                }

                cmtSkips = currLine - lastStopLine;
                for (int j = 0; j < cmtSkips; j++) {
                    result += lineEnd();
                }
            }

            result += std::any_cast<std::string>(visit(list[i]));

            if (i < list.size() - 1 || trailingComma) {
                result += comma + (multiLine ? "" : " ");
            }

            lastStopLine = list[i]->getStop()->getLine();
            auto [predCmtStart, predCmtEnd] = findCmtRange(list[i]->getStop()->getTokenIndex(), false);

            for (size_t j = predCmtStart; j < predCmtEnd; j++) {
                const auto &comment = tokens[j];
                const auto &commentText = comment->getText();

                const int cmtSkips = comment->getLine() - lastStopLine;
                if (cmtSkips == 0 && multiLine) {
                    // if single, block comments are leaded by a space after comma already
                    result += " ";
                }

                for (int k = 0; k < cmtSkips; k++) {
                    result += lineEnd();
                }

                insertComment(comment);

                lastStopLine = comment->getLine() + countLines(commentText);
            }

            if (predCmtStart < predCmtEnd) {
                const size_t currLineEnd = list[i]->getStop()->getLine();
                const auto &lastComment = tokens[predCmtEnd - 1];
                const auto &lastText = lastComment->getText();
                const size_t lastCommentLineEnd = lastComment->getLine() + countLines(lastText);
                lastCommentLines = lastCommentLineEnd - currLineEnd;
            } else {
                lastCommentLines = 0;
            }

            lastLine = list[i]->getStop()->getLine() + countLines(list[i]->getStop()->getText());
        }

        if (multiLine) {
            if (!wrappedWithBraces) {
                popIndent();
                result += lineEnd();
            }
        } else if (padding && !trailingComma) {
            result += " ";
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
    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context, bool trailingComma, bool padding,
                               bool forceMultiLine);

  public:
    Formatter(const std::vector<antlr4::Token *> tokens) : tokens(tokens) {}

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
    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context) {
        return visitArgumentList(context, false, true, false);
    }
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