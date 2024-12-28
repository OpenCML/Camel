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
 * Created: May. 17, 2024
 * Updated: Oct. 22, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include <algorithm>
#include <regex>
#include <string>
#include <unordered_set>

#include "antlr/OpenCMLVisitor.h"
#include "antlr4-runtime.h"

class Formatter : public OpenCMLVisitor {
  private:
    enum class QuotePreference { Single, Double };
    enum class CommentPreference { Hash, Slash };

    // bool compact = false;
    // unsigned int threshold = 80;
    std::string indent = "    ";
    std::string newline = "\r\n";
    bool preferSemis = false;
    QuotePreference quotePrefer = QuotePreference::Single;

    std::string currentIndent = "";

    const std::vector<antlr4::Token *> tokens;

    int indentLevel = -1;
    std::unordered_set<size_t> insertedCommentIndices;

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

    std::string formatStringLiteral(const std::string &input);

    void insertComment(antlr4::Token *comment, std::string &result);

    inline std::string lineEnd() { return newline + currentIndent; }

    inline size_t countLines(const std::string &input) { return std::count(input.begin(), input.end(), '\n'); }

    template <typename T>
    std::string formatBiOpsList(const std::vector<T *> &list,
                                std::vector<antlr4::tree::ParseTree *> &children, // children of the context
                                bool padding = true                               // padding spaces
    ) {
        // expr (op expr)*
        std::string result = std::any_cast<std::string>(visit(list[0]));
        if (list.size() == 1) {
            return result;
        }
        if (padding) {
            for (size_t i = 1; i < list.size(); i++) {
                result += " " + children[i * 2 - 1]->getText() + " " + std::any_cast<std::string>(visit(list[i]));
            }
        } else {
            for (size_t i = 1; i < list.size(); i++) {
                result += children[i * 2 - 1]->getText() + std::any_cast<std::string>(visit(list[i]));
            }
        }
        return result;
    }

    enum FormatListFlags {
        NONE = 0,
        PADDING = 1 << 0,    // Include padding spaces
        MULTILINE = 1 << 1,  // Force multi-line formatting
        PUSH_SCOPE = 1 << 2, // Push indent for multi-line
        TRAIL_COMMA = 1 << 3 // Include trailing comma
    };

    template <typename T>
    std::string formatList(const std::vector<T *> &list,
                           const std::string &&iComma, // inline comma
                           const std::string &&nComma, // new line comma
                           int flags = 0,              // combined flags
                           int maxSkips = 2            // maximum number of line skips
    ) {
        // Parse flags
        bool tComma = flags & TRAIL_COMMA;
        bool padding = flags & PADDING;
        bool multiLine = flags & MULTILINE;
        bool pushScope = flags & PUSH_SCOPE;
        return formatList(list, iComma, nComma, tComma, padding, multiLine, pushScope, maxSkips);
    }

    template <typename T>
    std::string formatList(const std::vector<T *> &list,
                           const std::string &&iComma, // inline comma
                           const std::string &&nComma, // new line comma
                           bool tComma,                // trailing comma
                           bool padding,               // padding spaces
                           bool multiLine,             // force multi-line
                           bool pushScope,             // push indent
                           int maxSkips                // maximum number of line skips
    ) {
        std::string result;
        if (list.empty()) {
            return result;
        }

        // given a start index, find the range of comments in the token stream
        const auto findCmtRange = [&](size_t start, bool reverse = false) -> std::pair<size_t, size_t> {
            size_t predCmtStart = start, predCmtEnd = start;

            if (!reverse) { // forward lookup
                predCmtStart++;
                if (predCmtStart < tokens.size() && tokens[predCmtStart]->getText()[0] == iComma[0]) {
                    // here we assumes that the the first character of the iComma
                    // will always be the comma itself (etc. ','), not a comma with a space (etc. ', '),
                    // ether only a space (etc. ' ')
                    predCmtStart++;
                }
                predCmtEnd = predCmtStart;
                size_t j;
                for (j = predCmtStart; j < tokens.size(); j++) {
                    if (tokens[j]->getChannel() <= 1) {
                        predCmtEnd = j;
                        break;
                    }
                }
                if (j == tokens.size() - 1 && tokens[tokens.size() - 1]->getChannel() > 1) {
                    predCmtEnd = tokens.size();
                }
            } else { // backward lookup
                if (predCmtEnd > 1 && tokens[predCmtEnd - 1]->getText()[0] == iComma[0]) {
                    predCmtEnd--;
                }
                predCmtStart = predCmtEnd;
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
            }

            return std::make_pair(predCmtStart, predCmtEnd);
        };

        if (!multiLine) {
            // the elements are on one line if and only if
            // all elements' last token is on the same line
            // with the first token of the next element
            // that's to say, although the multi-line is set to false,
            // the list may still be formatted in multiple lines
            // if the elements are not in one line already
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

        if (padding) {
            if (multiLine) {
                if (pushScope) {
                    pushIndent();
                }
                result += lineEnd();
            } else {
                result += " ";
            }
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

            int numSkips = std::min(remainedSkips, maxSkips);
            for (int j = 0; j < numSkips; j++) {
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

                    // incase the comment is already inserted
                    // for example, stmtList(exprList(exprList))
                    if (insertedCommentIndices.find(j) == insertedCommentIndices.end()) {
                        insertComment(comment, result);
                        insertedCommentIndices.insert(j);
                        lastStopLine = comment->getLine() + countLines(commentText);
                    } else {
                        lastStopLine = currLine;
                    }
                }

                cmtSkips = currLine - lastStopLine;
                for (int j = 0; j < cmtSkips; j++) {
                    result += lineEnd();
                }
            }

            result += std::any_cast<std::string>(visit(list[i]));

            if (i < list.size() - 1 || tComma) {
                result += multiLine ? nComma : iComma;
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

                if (insertedCommentIndices.find(j) == insertedCommentIndices.end()) {
                    insertComment(comment, result);
                    insertedCommentIndices.insert(j);
                }

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

        if (padding) {
            if (multiLine) {
                if (pushScope) {
                    popIndent();
                }
                result += lineEnd();
            } else if (!tComma) {
                result += " ";
            }
        }

        return result;
    }

  public:
    Formatter(const std::vector<antlr4::Token *> tokens) : tokens(tokens) {}

    std::any visitProgram(OpenCMLParser::ProgramContext *context);

    std::any visitDecl(OpenCMLParser::DeclContext *context);

    std::any visitStmt(OpenCMLParser::StmtContext *context);

    std::any visitStmtList(OpenCMLParser::StmtListContext *context);

    std::any visitModuleDecl(OpenCMLParser::ModuleDeclContext *context);

    std::any visitImportDecl(OpenCMLParser::ImportDeclContext *context);

    std::any visitExportDecl(OpenCMLParser::ExportDeclContext *context);

    std::any visitStmtBlock(OpenCMLParser::StmtBlockContext *context);

    std::any visitLambdaExpr(OpenCMLParser::LambdaExprContext *context);

    std::any visitFuncDecl(OpenCMLParser::FuncDeclContext *context);

    std::any visitParentIdents(OpenCMLParser::ParentIdentsContext *context);

    std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *context);

    std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *context);

    std::any visitCarrier(OpenCMLParser::CarrierContext *context);

    std::any visitLetDecl(OpenCMLParser::LetDeclContext *context);

    std::any visitUseDecl(OpenCMLParser::UseDeclContext *context);

    std::any visitRetStmt(OpenCMLParser::RetStmtContext *context);

    std::any visitTypeDecl(OpenCMLParser::TypeDeclContext *context);

    std::any visitEnumDecl(OpenCMLParser::EnumDeclContext *context);

    std::any visitExprStmt(OpenCMLParser::ExprStmtContext *context);

    std::any visitAnnotation(OpenCMLParser::AnnotationContext *context);

    std::any visitAnnotations(OpenCMLParser::AnnotationsContext *context);

    std::any visitModifiers(OpenCMLParser::ModifiersContext *context);

    std::any visitIndexValue(OpenCMLParser::IndexValueContext *context);

    std::any visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context);

    std::any visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context);

    std::any visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context);

    std::any visitIdentList(OpenCMLParser::IdentListContext *context);

    std::any visitValueList(OpenCMLParser::ValueListContext *context);

    std::any visitIndexValues(OpenCMLParser::IndexValuesContext *context);

    std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context);

    std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context);

    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context);

    std::any visitMemberAccess(OpenCMLParser::MemberAccessContext *context);

    std::any visitParentParams(OpenCMLParser::ParentParamsContext *context);

    std::any visitParentArgues(OpenCMLParser::ParentArguesContext *context);

    std::any visitAngledParams(OpenCMLParser::AngledParamsContext *context);

    std::any visitAngledValues(OpenCMLParser::AngledValuesContext *context);

    std::any visitBlockExpr(OpenCMLParser::BlockExprContext *context);

    std::any visitDataExpr(OpenCMLParser::DataExprContext *context);

    std::any visitPattern(OpenCMLParser::PatternContext *context);

    std::any visitMatchCase(OpenCMLParser::MatchCaseContext *context);

    std::any visitCatchClause(OpenCMLParser::CatchClauseContext *context);

    std::any visitStructExpr(OpenCMLParser::StructExprContext *context);

    std::any visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context);

    std::any visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context);

    std::any visitEqualityExpr(OpenCMLParser::EqualityExprContext *context);

    std::any visitRelationalExpr(OpenCMLParser::RelationalExprContext *context);

    std::any visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context);

    std::any visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context);

    std::any visitNullableExpr(OpenCMLParser::NullableExprContext *context);

    std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context);

    std::any visitLinkExpr(OpenCMLParser::LinkExprContext *context);

    std::any visitBindExpr(OpenCMLParser::BindExprContext *context);

    std::any visitWithExpr(OpenCMLParser::WithExprContext *context);

    std::any visitAnnoExpr(OpenCMLParser::AnnoExprContext *context);

    std::any visitDictExpr(OpenCMLParser::DictExprContext *context);

    std::any visitListExpr(OpenCMLParser::ListExprContext *context);

    std::any visitPrimaryData(OpenCMLParser::PrimaryDataContext *context);

    std::any visitLiteral(OpenCMLParser::LiteralContext *context);

    std::any visitTypeExpr(OpenCMLParser::TypeExprContext *context);

    std::any visitUnionType(OpenCMLParser::UnionTypeContext *context);

    std::any visitUnionUnit(OpenCMLParser::UnionUnitContext *context);

    std::any visitListType(OpenCMLParser::ListTypeContext *context);

    std::any visitTypeOrData(OpenCMLParser::TypeOrDataContext *context);

    std::any visitArgsType(OpenCMLParser::ArgsTypeContext *context);

    std::any visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context);

    std::any visitDictExprType(OpenCMLParser::DictExprTypeContext *context);

    std::any visitDictType(OpenCMLParser::DictTypeContext *context);

    std::any visitTupleType(OpenCMLParser::TupleTypeContext *context);

    std::any visitLambdaType(OpenCMLParser::LambdaTypeContext *context);

    std::any visitIdentDef(OpenCMLParser::IdentDefContext *context);

    std::any visitIdentRef(OpenCMLParser::IdentRefContext *context);
};
