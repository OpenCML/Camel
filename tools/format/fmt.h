// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <regex>
#include <string>
#include <unordered_set>

#include <any>
#include <utility>
#include <vector>

#include "camel/parse/syntax/cst.h"
#include "camel/utils/assert.h"

namespace syn = camel::parse::syntax;
namespace cst = camel::parse::syntax::cst;
using TK      = camel::parse::syntax::TokenKind;
using NK      = camel::parse::syntax::NodeKind;

/// Formats a parsed source text: walks the concrete syntax tree, one function per production,
/// and puts the comments back from the token buffer.
class Formatter {
  public:
    struct Options {
        std::string indent      = "    ";
        std::string newline     = "\n";
        std::string quotePrefer = "single";
    };

  private:
    enum class QuotePreference { Single, Double };
    enum class CommentPreference { Hash, Slash };

    // bool compact = false;
    // unsigned int threshold = 80;
    std::string indent          = "    ";
    std::string newline         = "\n";
    bool preferSemis            = false;
    QuotePreference quotePrefer = QuotePreference::Single;

    std::string currentIndent = "";

    /// A token as the formatter needs it. `channel` is 0 for a token of the program and 2, 3, 4
    /// for a `#` line, a `//` comment and a block comment; `line` is zero-based.
    struct FmtToken {
        int channel       = 0;
        size_t lineNumber = 0;
        std::string content;
        size_t line() const { return lineNumber; }
        const std::string &text() const { return content; }
    };

    const syn::Tree &tree;
    std::vector<FmtToken> tokens;

    /// The comments already written into the output, by token index. A comment can fall inside two
    /// ranges once the ranges cover the tokens they stand between, and the second one to reach it
    /// must not write it twice; `format()` also asks which ones nothing wrote.
    std::vector<bool> writtenComments;

    /// The comments that no range and no visitor reached, written at the end of the file by
    /// `format()`. A comment is source text: losing one silently rewrites the source, so this is
    /// the floor, and it is reported rather than hidden (the format gate refuses a non-zero count).
    size_t unplacedComments = 0;

    template <typename List> static std::vector<syn::Node> vec(const List &list) {
        std::vector<syn::Node> nodes;
        for (const auto node : list) {
            nodes.push_back(node);
        }
        return nodes;
    }
    /// The items of a namespace: its child nodes but the name.
    static std::vector<syn::Node> itemsOf(syn::Node space) {
        std::vector<syn::Node> nodes;
        for (const syn::Node node : space.nodes()) {
            if (!node.is(NK::IdentRef)) {
                nodes.push_back(node);
            }
        }
        return nodes;
    }

    int indentLevel = 0;

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

    std::string formatStringLiteral(const std::string &input, bool multiLine);

    void configure(const Options &options);

    void insertComment(size_t index, std::string &result);

    bool commentWritten(size_t index) const {
        return index < writtenComments.size() && writtenComments[index];
    }
    void markCommentWritten(size_t index) {
        if (index < writtenComments.size()) {
            writtenComments[index] = true;
        }
    }

    /// The comments between the end of an item and the token that follows it - the closing bracket
    /// of the construct around it, or `else` after the branch of an `if`. No list range reaches
    /// past its last item and nothing else looks there: they are written where the item ends, on
    /// its own line when they stood on one. Returns true when a `//` comment was written, which
    /// ends the line it is on and would swallow whatever the caller writes next.
    bool writeInlineComments(
        std::string &result, size_t afterToken, size_t beforeToken, size_t afterEndLine) {
        bool endsLine   = false;
        size_t lastStop = afterEndLine;
        for (size_t i = afterToken + 1; i < beforeToken && i < tokens.size(); i++) {
            if (tokens[i].channel <= 1 || commentWritten(i)) {
                continue;
            }
            const long skips = static_cast<long>(tokens[i].line()) - static_cast<long>(lastStop);
            if (skips <= 0) {
                result += " ";
            } else {
                result += lineEnd();
            }
            writeCommentText(result, i);
            lastStop = tokens[i].line() + countLines(tokens[i].text());
            endsLine = tokens[i].channel != 4;
        }
        return endsLine;
    }

    /// Writes one comment, once, and nothing else: the caller owns the line ends around it.
    void writeCommentText(std::string &result, size_t index) {
        if (commentWritten(index) || tokens[index].channel <= 1) {
            return;
        }
        markCommentWritten(index);
        result += tokens[index].text();
    }

    /// Writes one comment - once - with whatever must follow it so that it cannot swallow the text
    /// after it: a `//` or `#` line ends there, and a block comment keeps its own line unless the
    /// code that follows stands on its line in the input.
    void writeComment(std::string &result, size_t index) {
        if (commentWritten(index) || tokens[index].channel <= 1) {
            return;
        }
        const std::string &commentText = tokens[index].text();
        const bool sameLine            = index + 1 < tokens.size() &&
                              tokens[index + 1].line() ==
                                  tokens[index].line() + countLines(commentText);
        writeCommentText(result, index);
        if (tokens[index].channel != 4) {
            result += lineEnd();
        } else if (sameLine) {
            result += " ";
        } else {
            result += lineEnd();
        }
    }

    /// The comments that stand right above a node's first token and are still unwritten: a comment
    /// between two tokens a node writes itself (a parameter and its type, say) is in no gap any
    /// list looks at. Writing it here, at the token it precedes, is what makes the next pass find
    /// it in the same gap and write it in the same place.
    std::string commentsBefore(syn::Node context) {
        const size_t first = context->firstToken().index();
        if (first == 0 || writtenComments.empty()) {
            return "";
        }
        size_t runStart = first;
        while (runStart > 0 && tokens[runStart - 1].channel > 1) {
            runStart--;
        }
        std::string prefix;
        for (size_t i = runStart; i < first; i++) {
            writeComment(prefix, i);
        }
        return prefix;
    }

    inline std::string lineEnd(int repeat = 1) {
        if (repeat <= 0) {
            return "";
        }
        std::string result;
        for (int i = 0; i < repeat; ++i) {
            result += newline;
        }
        return result + currentIndent;
    }

    inline size_t countLines(const std::string &input) {
        return std::count(input.begin(), input.end(), '\n');
    }

    /// operand (operator operand)*: the child nodes are the operands, the tokens between them
    /// the operators.
    std::string formatBiOpsList(syn::Node context, bool padding = true) {
        std::string result;
        const size_t n = context.childCount();
        for (size_t i = 0; i < n; i++) {
            const syn::Child child = context.child(i);
            if (child.isNode()) {
                result += std::any_cast<std::string>(visit(child.node()));
            } else if (padding) {
                result += " " + child.text() + " ";
            } else {
                result += child.text();
            }
        }
        return result;
    }

    enum FormatListFlags {
        PaddingSP  = 1 << 0, // Padding with spaces
        PaddingNL  = 1 << 1, // Padding with new lines
        Multiline  = 1 << 2, // Force multi-line formatting
        InOneLine  = 1 << 3, // Force in one line
        PushScope  = 1 << 4, // Push indent for multi-line
        TrailingC  = 1 << 5, // Include trailing comma
        PLeftOnly  = 1 << 6, // Padding on left only
        PRightOnly = 1 << 7, // Padding on right only
    };

    std::string formatList(
        const std::vector<syn::Node> &list, syn::Node context,
        const std::string iComma,                               // inline comma
        const std::string nComma,                               // new line comma
        int flags                                          = 0, // combined flags
        int maxSkips                                       = 2, // maximum number of line skips
        std::vector<std::pair<size_t, size_t>> tokenRanges = {}) {
        // Parse flags
        bool tComma     = flags & TrailingC;
        bool paddingSP  = flags & PaddingSP;
        bool paddingNL  = flags & PaddingNL;
        bool multiLine  = flags & Multiline;
        bool inOneLine  = flags & InOneLine;
        bool pushScope  = flags & PushScope;
        bool pLeftOnly  = flags & PLeftOnly;
        bool pRightOnly = flags & PRightOnly;

        std::string result;
        if (list.empty()) {
            if (!context) {
                // can pass a nullptr to avoid the comment processing
                // this can be helpful when the list is not continuous
                return result;
            }
            // TODO: handle empty list to proc comments
            // here temporarily ignores the ctrl flags
            const size_t firstTokIdx = context->firstToken()->index();
            const size_t lastTokIdx  = context->lastToken()->index();
            bool foundComment        = false;
            if (pushScope) {
                pushIndent();
            }
            for (size_t i = firstTokIdx + 1; i < lastTokIdx; i++) {
                if (tokens[i].channel > 1) {
                    result += lineEnd();
                    insertComment(i, result);
                    foundComment = true;
                }
            }
            if (pushScope) {
                popIndent();
            }
            if (foundComment) {
                result += lineEnd();
            }
            return result;
        }

        // given a token index range, find the range of comments in the token stream
        const auto findCmtRange = [&](size_t index,
                                      bool reverse = false) -> std::pair<size_t, size_t> {
            ASSERT(
                !(reverse && index != 0),
                "Reverse lookup should always start from the last element");
            size_t start = 0, end = tokens.size() - 1;
            if (!tokenRanges.empty()) {
                // the last pair of the tokenRanges is the range of the reverse lookup
                ASSERT(
                    tokenRanges.size() == list.size() + 1,
                    "Token ranges should match the list size plus one for reverse lookup");
                if (!reverse) {
                    start = tokenRanges[index].first;
                    end   = tokenRanges[index].second;
                } else {
                    start = tokenRanges[list.size()].first;
                    end   = tokenRanges[list.size()].second;
                }
            } else {
                if (!reverse) {
                    start = list[index]->lastToken()->index();
                    if (index == list.size() - 1) {
                        end = context->lastToken()->index() + 1;
                    } else {
                        end = list[index + 1]->firstToken()->index();
                    }
                } else {
                    start = context->firstToken()->index();
                    // A list that starts its context (the statements of a block) has its leading
                    // comments before the context: step back over that whole run of comments, to
                    // the token the run follows. None of them is anybody else's, and a comment
                    // above the last one had nothing looking for it once. A context with a token
                    // of its own first (`case` before the patterns) has none there, and the
                    // comment before it belongs to the item above.
                    if (start > 0 && list[0]->firstToken()->index() == start) {
                        while (start > 0 && tokens[start - 1].channel > 1) {
                            start--;
                        }
                    }
                    end = list[0]->firstToken()->index();
                }
                ASSERT(start <= end, "Start index should not be greater than end index");
            }

            size_t predCmtStart = start, predCmtEnd = end;
            if (!reverse) { // forward lookup
                size_t j;
                for (j = start; j < end; j++) {
                    // find the first comment token
                    if (tokens[j].channel > 1) {
                        predCmtStart = j;
                        break;
                    }
                }
                if (j == end) {
                    return std::make_pair(start, start);
                }
                predCmtEnd = predCmtStart;
                for (j = predCmtStart; j < end; j++) {
                    // find until the first non-comment token
                    if (tokens[j].channel == 1) {
                        predCmtEnd = j;
                        break;
                    }
                }
                if (j == end) {
                    predCmtEnd = end;
                }
            } else { // backward lookup
                for (size_t j = end - 1; j >= start; j--) {
                    // find the first comment token
                    if (tokens[j].channel > 1) {
                        predCmtEnd = j + 1;
                        break;
                    }
                    if (j == start) {
                        return std::make_pair(end, end);
                    }
                }
                predCmtStart = predCmtEnd;
                for (size_t j = predCmtEnd - 1; j >= start; j--) {
                    // find until the first non-comment token
                    if (tokens[j].channel == 1) {
                        predCmtStart = j + 1;
                        break;
                    }
                    if (j == start) {
                        predCmtStart = start;
                        break;
                    }
                }
            }

            return std::make_pair(predCmtStart, predCmtEnd);
        };

        if (!multiLine && !inOneLine) {
            // the elements are on one line if and only if
            // all elements' last token is on the same line
            // with the first token of the next element
            // that's to say, although the multi-line is set to false,
            // the list may still be formatted in multiple lines
            // if the elements are not in one line already
            auto lastEleLine = list[0]->lastToken()->line();
            for (size_t i = 1; i < list.size(); i++) {
                const auto &currEleLine = list[i]->firstToken()->line();
                if (currEleLine > lastEleLine) {
                    multiLine = true;
                    break;
                }
                lastEleLine = list[i]->lastToken()->line();
            }
        }

        if (paddingNL && multiLine) {
            if (pushScope) {
                pushIndent();
            }
            if (!pRightOnly) {
                result += lineEnd();
            }
        } else if (paddingSP && !pRightOnly) {
            result += " ";
        }

        int lastCommentLines = 0;
        size_t lastLine      = list[0]->lastToken()->line();

        for (size_t i = 0; i < list.size(); i++) {
            const size_t currLine = list[i]->firstToken()->line();
            int remainedSkips     = currLine - lastLine - lastCommentLines;

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

            size_t lastStopLine = list[i]->lastToken()->line();

            if (i == 0 && list[0]->firstToken()->index() > 0) {
                auto [predCmtStart, predCmtEnd] = findCmtRange(0, true);
                int cmtSkips                    = 0;
                bool firstComment               = true;
                for (size_t j = predCmtStart; j < predCmtEnd; j++) {
                    const auto &comment = tokens[j];
                    // A token that is not a comment gets no line of its own: the range may
                    // include the separator after the element, and writing a line end for it
                    // left a blank line that the next pass turned into two.
                    if (comment.channel <= 1) {
                        continue;
                    }
                    if (commentWritten(j)) {
                        continue;
                    }
                    const auto &commentText = comment.text();

                    // The break after `{` (or after `;`) has been written already: the first
                    // comment goes on the line it opened, not one line further down. Counting
                    // from the element's own last line made every pass add another blank line
                    // before a comment that starts a `match type` body.
                    cmtSkips = firstComment ? 0 : comment.line() - lastStopLine;

                    for (int k = 0; k < cmtSkips; k++) {
                        result += lineEnd();
                    }

                    firstComment = false;
                    insertComment(j, result);
                    lastStopLine = comment.line() + countLines(commentText);
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

            lastStopLine                    = list[i]->lastToken()->endLine();
            auto [predCmtStart, predCmtEnd] = findCmtRange(i, false);
            for (size_t j = predCmtStart; j < predCmtEnd; j++) {
                const auto &comment = tokens[j];
                // A token that is not a comment still ends the line a comment above it is on:
                // skipping it joined `} from ...` onto the comment, which commented the code out
                // (the fuzz check caught it).
                if (comment.channel <= 1) {
                    result += lineEnd();
                    ++lastStopLine;
                    continue;
                }
                if (commentWritten(j)) {
                    // Written by the visitor of a node inside this range (the comment before the
                    // closing bracket of an array literal, say): writing the line ends for it
                    // again would leave the blank lines it already has.
                    continue;
                }
                const auto &commentText = comment.text();

                const int cmtSkips = comment.line() - lastStopLine;
                if (cmtSkips == 0 && multiLine) {
                    // if single, block comments are leaded by a space after comma already
                    result += " ";
                }

                for (int k = 0; k < cmtSkips; k++) {
                    result += lineEnd();
                }

                insertComment(j, result);
                lastStopLine = comment.line() + countLines(commentText);
            }

            if (i + 1 == list.size() && tokenRanges.empty()) {
                // The comments between the last item and the closing bracket of the construct
                // around it (`]` of an array literal, `)` of a call) are in no range: the ranges
                // stop at the item, and the bracket belongs to the visitor above, which writes it
                // straight after this text. They are written where they stood, and a `//` comment
                // ends its line so that the bracket is not commented out with it.
                const size_t itemEnd = list[i]->lastToken()->index();
                size_t closing       = itemEnd + 1;
                while (closing < tokens.size() && tokens[closing].channel > 1) {
                    closing++;
                }
                const bool closingBracket =
                    closing < tokens.size() && (tokens[closing].text() == "]" ||
                                                tokens[closing].text() == ")" ||
                                                tokens[closing].text() == ">");
                if (closingBracket &&
                    writeInlineComments(result, itemEnd, closing, list[i]->lastToken()->endLine()) &&
                    !(paddingNL && multiLine)) {
                    result += lineEnd();
                }
            }

            if (predCmtStart < predCmtEnd) {
                const size_t currLineEnd        = list[i]->lastToken()->endLine();
                const auto &lastComment         = tokens[predCmtEnd - 1];
                const auto &lastText            = lastComment.text();
                const size_t lastCommentLineEnd = lastComment.line() + countLines(lastText);
                lastCommentLines                = lastCommentLineEnd - currLineEnd;
            } else {
                lastCommentLines = 0;
            }

            lastLine = list[i]->lastToken()->line() + countLines(list[i]->lastToken()->text());
        }

        if (paddingNL && multiLine) {
            if (pushScope) {
                popIndent();
            }
            if (!pLeftOnly) {
                result += lineEnd();
            }
        } else if (paddingSP && !tComma && !pLeftOnly) {
            result += " ";
        }

        return result;
    }

  public:
    explicit Formatter(const syn::Tree &tree);

    Formatter(const syn::Tree &tree, Options options);

    /// The formatted text of the tree (which must have no errors).
    std::string format();

    /// The comments of the input that `format()` had to write at the end of the file because no
    /// range and no visitor wrote them: kept, but not kept in their place.
    size_t unplacedCommentCount() const { return unplacedComments; }

    /// Any node, by its kind.
    std::any visit(syn::Node context);
    std::any visitExpr(syn::Node context) { return visit(context); }
    std::any visitType(syn::Node context) { return visit(context); }

  private:
    /// `visit` without the comments that stand above the node: the dispatch on the node's kind.
    std::any dispatch(syn::Node context);

  public:

    // Auto-generated visitor methods

    std::any visitProgram(cst::Program context);

    std::any visitStmtList(cst::StmtList context);

    std::any visitModuleName(cst::ModuleName context);

    std::any visitModuleDecl(cst::ModuleDecl context);

    std::any visitImportDecl(cst::ImportDecl context);

    std::any visitExportDecl(cst::ExportDecl context);

    std::any visitUsingNamespaceDecl(cst::UsingNamespaceDecl context);

    std::any visitBlockStmt(cst::BlockStmt context);

    std::any visitStmtBlock(cst::StmtBlock context);

    std::any visitBlockExpr(syn::Node context);

    std::any visitFuncData(cst::FuncData context);

    std::any visitFuncAnno(cst::FuncAnno context);

    std::any visitTagDecl(cst::TagDecl context);
    std::any visitTagPositions(cst::TagPositions context);
    std::any visitTagReaders(cst::TagReaders context);
    std::any visitTagUse(cst::TagUse context);
    std::any visitTagRef(cst::TagRef context);

    std::any visitFuncDecl(cst::FuncDecl context);
    std::any visitWhenClause(cst::WhenClause context);
    std::any visitRuleDecl(cst::RuleDecl context);
    std::any visitAnalysisDecl(cst::AnalysisDecl context);
    std::any visitAnalysisClause(cst::AnalysisClause context);
    std::any visitRuleContext(cst::RuleContext context);
    std::any visitTypeParams(cst::TypeParams context);
    std::any visitRuleBody(cst::RuleBody context);

    std::any visitNamespaceDecl(cst::NamespaceDecl context);

    std::any visitParentIdents(cst::ParentIdents context);

    std::any visitBracedIdents(cst::BracedIdents context);

    std::any visitBracketIdents(cst::BracketIdents context);

    std::any visitDataDecl(cst::DataDecl context);

    std::any visitResourceDecl(cst::ResourceDecl context);
    std::any visitResourceClause(cst::ResourceClause context);

    std::any visitTypeDecl(cst::TypeDecl context);

    std::any visitRetStmt(cst::RetStmt context);

    std::any visitModifiers(cst::Modifiers context);

    std::any visitSpreadValue(cst::SpreadValue context);

    std::any visitRestValue(cst::RestValue context);

    std::any visitKeyTypePair(cst::KeyTypePair context);

    std::any visitKeyValuePair(cst::KeyValuePair context);

    std::any visitKeyParamPair(cst::KeyParamPair context);
    std::any visitTupleParam(cst::TupleParam context);

    std::any visitDataList(cst::DataList context);

    std::any visitIdentList(cst::IdentList context);

    std::any visitIndexValues(cst::IndexValues context);

    std::any visitPairedValues(cst::PairedValues context);

    std::any visitPairedParams(cst::PairedParams context);

    std::any visitArgumentList(cst::ArgumentList context);

    std::any visitIndices(cst::Indices context);

    std::any visitSliceIndices(cst::SliceIndices context);

    std::any visitParentParams(cst::ParentParams context);

    std::any visitParentArgues(cst::ParentArgues context);

    std::any visitAngledParams(cst::AngledParams context);

    std::any visitAngledValues(cst::AngledValues context);

    std::any visitPattern(cst::Pattern context);

    std::any visitMatchCase(cst::MatchCase context);

    std::any visitMatchTypeExpr(cst::MatchTypeExpr context);

    /// The type after `fields` / `match type`, where the parser reads a `(` as the start of a
    /// type (H79 ruling 1): a function type, whose text starts with `(`, is written with one
    /// more pair of parentheses so that the output parses back to the same tree.
    std::string formatTypeInTypePosition(syn::Node type);

    std::any visitTypeCase(cst::TypeCase context);

    std::any visitStaticForExpr(cst::StaticForExpr context);

    std::any visitStaticLoopExpr(cst::StaticLoopExpr context);

    std::any visitTypeCasePattern(cst::TypeCasePattern context);

    std::any visitCatchClause(cst::CatchClause context);

    std::any visitCtrlExpr(syn::Node context);

    std::any visitWaitExpr(cst::WaitExpr context);

    std::any visitAssignExpr(cst::AssignExpr context);

    std::any visitLogicalOrExpr(cst::LogicalOrExpr context);

    std::any visitLogicalAndExpr(cst::LogicalAndExpr context);

    std::any visitEqualityExpr(cst::EqualityExpr context);

    std::any visitRelationalExpr(cst::RelationalExpr context);

    std::any visitAdditiveExpr(cst::AdditiveExpr context);

    std::any visitMultiplicativeExpr(cst::MultiplicativeExpr context);

    std::any visitNullableExpr(cst::NullableExpr context);

    std::any visitUnaryExpr(syn::Node context);

    std::any visitLinkExpr(cst::LinkExpr context);

    std::any visitCompExpr(cst::CompExpr context);

    std::any visitAnnoExpr(cst::AnnoExpr context);

    std::any visitAccessExpr(cst::AccessExpr context);
    std::any visitMemberAccess(cst::MemberAccess context);

    std::any visitStructData(cst::StructData context);

    std::any visitArrayData(cst::ArrayData context);

    std::any visitTupleData(cst::TupleData context);

    std::any visitLiteral(cst::Literal context);

    std::any visitTypeExpr(cst::TypeExpr context);

    std::any visitInterType(cst::InterType context);

    std::any visitDiffType(cst::DiffType context);

    std::any visitKeyUnionDiffType(cst::KeyUnionDiffType context);

    std::any visitKeyInterType(cst::KeyInterType context);

    std::any visitTypeUnit(cst::TypeUnit context);

    std::any visitArrayType(cst::ArrayType context);

    std::any visitArrayDim(cst::ArrayDim context);

    std::any visitConstData(cst::ConstData context);

    std::any visitSpecType(cst::SpecType context);

    std::any visitStructType(cst::StructType context);

    std::any visitEnumType(cst::EnumType context);

    std::any visitEnumVariant(cst::EnumVariant context);

    std::any visitEnumField(cst::EnumField context);

    std::any visitEnumTuplePattern(cst::EnumTuplePattern context);

    std::any visitEnumStructPattern(cst::EnumStructPattern context);

    std::any visitTypeList(cst::TypeList context);

    std::any visitTypeArguments(cst::TypeArguments context);

    std::any visitNamedTypeArgument(cst::NamedTypeArgument context);

    std::any visitTupleType(cst::TupleType context);

    std::any visitFuncType(cst::FuncType context);

    std::any visitIdentDef(cst::IdentDef context);

    std::any visitIdentRef(cst::IdentRef context);

    // End of auto-generated visitor methods
};
