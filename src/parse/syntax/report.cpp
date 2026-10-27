// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * From the parser's errors to diagnostics.
 *
 * The parser states where it could not go on and what would have fitted there. What a person
 * did wrong is often a different thing (a bracket left open some lines earlier, a closing
 * bracket too many), which is found here from two more facts: the error's own kind where the
 * parser knew the construct it was in, and the brackets open at the offending token, from a
 * scan of the tokens that is independent of the parse.
 */

#include "camel/parse/syntax/report.h"

#include "camel/core/error/diagnostics/render.h"

#include <algorithm>
#include <format>
#include <optional>
#include <set>

using namespace camel::core::error;

namespace camel::parse::syntax {

namespace {

CharRange rangeOfToken(const TokenBuffer &buffer, uint32_t index) {
    const Token &token  = buffer[index];
    const size_t line   = buffer.lineOf(token.begin);
    const size_t column = buffer.columnOf(token.begin);
    return CharRange{{line, column}, {line, column + token.length()}};
}

/// The range from byte `begin` to byte `end`, in the positions a diagnostic counts (0-based
/// line, column in code points). Unlike rangeOfToken this survives a run that spans lines and a
/// cut inside a multi-byte character, which is what a fix's own range may be.
CharRange rangeOfBytes(const TokenBuffer &buffer, uint32_t begin, uint32_t end) {
    const char *source    = buffer.source().data();
    const size_t size     = buffer.source().size();
    const size_t beginLine = buffer.lineOf(begin);
    const size_t beginCol  = buffer.columnOf(begin);
    size_t line            = beginLine;
    size_t column          = beginCol;
    for (uint32_t i = begin; i < end && i < size; ++i) {
        if (source[i] == '\n') {
            ++line;
            column = 0;
        } else if ((static_cast<unsigned char>(source[i]) & 0xC0) != 0x80) {
            ++column;
        }
    }
    return CharRange{{beginLine, beginCol}, {line, column}};
}

/// The empty range at byte `offset`: where a fix inserts text.
CharRange pointAt(const TokenBuffer &buffer, uint32_t offset) {
    return rangeOfBytes(buffer, offset, offset);
}

/// The text a wanted item would be written as, when it has exactly one spelling: a keyword or an
/// operator spells itself out; a name, a number, a string and a whole expression are described
/// ("a name") and have no one spelling to insert.
std::optional<std::string> spellingOf(uint8_t item) {
    if (item >= SyntaxError::Expression) {
        return std::nullopt; // an expression, a declaration, a statement, a type, a pattern
    }
    const TokenKind kind = static_cast<TokenKind>(item);
    if (kind == TokenKind::Eof || (!isKeyword(kind) && !isPunctuation(kind))) {
        return std::nullopt;
    }
    return std::string(tokenKindText(kind));
}

char closerOf(char opener) { return opener == '(' ? ')' : opener == '[' ? ']' : '}'; }
char openerOf(char closer) { return closer == ')' ? '(' : closer == ']' ? '[' : '{'; }
bool isOpener(TokenKind k) {
    return k == TokenKind::LParen || k == TokenKind::LBracket || k == TokenKind::LBrace;
}
bool isCloser(TokenKind k) {
    return k == TokenKind::RParen || k == TokenKind::RBracket || k == TokenKind::RBrace;
}

/// How a token is named in a message.
std::string describeToken(const TokenBuffer &buffer, uint32_t index) {
    const Token &token = buffer[index];
    if (token.kind == TokenKind::Eof) {
        return "the end of the file";
    }
    std::string text(buffer.text(token));
    if (auto nl = text.find('\n'); nl != std::string::npos) {
        text = text.substr(0, nl) + "...";
    }
    if (displayWidth(text) > 24) {
        size_t cut = 20;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        text = text.substr(0, cut) + "...";
    }
    return "'" + text + "'";
}

std::string describeItem(uint8_t item) {
    switch (item) {
    case SyntaxError::Expression:
        return "an expression";
    case SyntaxError::Declaration:
        return "a declaration";
    case SyntaxError::Statement:
        return "a statement";
    case SyntaxError::TypeExpression:
        return "a type";
    case SyntaxError::PatternItem:
        return "a pattern";
    default:
        break;
    }
    const TokenKind kind = static_cast<TokenKind>(item);
    if (kind == TokenKind::Eof) {
        return "the end of the file";
    }
    if (isKeyword(kind) || isPunctuation(kind)) {
        return "'" + std::string(tokenKindText(kind)) + "'";
    }
    return std::string(tokenKindText(kind));
}

/// The expected set in words: "'{' or 'sync'", "',', ')' or ...".
std::string describeExpected(const std::vector<uint8_t> &items) {
    std::vector<std::string> out;
    for (uint8_t item : items) {
        std::string text = describeItem(item);
        if (std::find(out.begin(), out.end(), text) == out.end()) {
            out.push_back(std::move(text));
        }
    }
    if (out.empty()) {
        return "";
    }
    const size_t kMost = 6;
    std::string text;
    const size_t shown = std::min(out.size(), kMost);
    for (size_t i = 0; i < shown; ++i) {
        if (i > 0) {
            text += (i + 1 == shown && out.size() <= kMost) ? " or " : ", ";
        }
        text += out[i];
    }
    if (out.size() > kMost) {
        text += ", ...";
    }
    return text;
}

struct OpenDelimiter {
    char kind;
    uint32_t token;
};

/// The brackets of a file: for every significant token the brackets open before it, and the
/// closing brackets that close nothing.
struct BracketScan {
    std::vector<uint32_t> significant;               // token indices, in order
    std::vector<std::vector<OpenDelimiter>> openAt;  // per position in `significant`
    std::vector<std::pair<uint32_t, std::vector<OpenDelimiter>>> strays;

    explicit BracketScan(const TokenBuffer &buffer) {
        std::vector<OpenDelimiter> stack;
        for (uint32_t i = 0; i < buffer.size(); ++i) {
            const Token &token = buffer[i];
            if (isTrivia(token.kind)) {
                continue;
            }
            significant.push_back(i);
            openAt.push_back(stack);
            if (isOpener(token.kind)) {
                stack.push_back({buffer.text(token)[0], i});
            } else if (isCloser(token.kind)) {
                // A closer closes the nearest opener of its kind; openers above that one were
                // left open (reported where the parser misses their closer) and end here too.
                // Only a closer with no opener of its kind is stray.
                const char closer = buffer.text(token)[0];
                size_t match      = stack.size();
                while (match > 0 && stack[match - 1].kind != openerOf(closer)) {
                    --match;
                }
                if (match > 0) {
                    stack.resize(match - 1);
                } else {
                    strays.emplace_back(i, stack);
                }
            }
        }
    }

    size_t positionOf(uint32_t token) const {
        auto it = std::lower_bound(significant.begin(), significant.end(), token);
        return static_cast<size_t>(it - significant.begin());
    }
};

/// The decimal reading of a digit run written with a leading zero, when it is the only reading:
/// `09` cannot be octal, and a run of zeros is zero in every base. `077` is left alone, because
/// the writer may have meant octal 63 - the help names `0o77` for that - and a report that
/// cannot tell the two apart offers no edit.
std::optional<std::string> decimalOfLeadingZeroRun(const std::string &written) {
    if (written.size() < 2 || written[0] != '0') {
        return std::nullopt;
    }
    for (char c : written) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
    }
    const bool onlyZeros = written.find_first_not_of('0') == std::string::npos;
    if (!onlyZeros && written.find_first_of("89") == std::string::npos) {
        return std::nullopt; // `077` reads as both 63 and 77
    }
    const size_t first = written.find_first_not_of('0');
    return onlyZeros ? std::string("0") : written.substr(first);
}

/// Where a missing token goes and what is written there.
struct Insertion {
    uint32_t at = 0;  // the byte offset the text is inserted at
    std::string text;
};

/// The change that supplies the token `kind` missing in front of the token at `index`: its
/// spelling, with the space it needs and no second space next to one the file already has.
///
/// A bracket, and an operator that only ever stands in front of something, is inserted bare and
/// touching what follows - `>` closes an angle-bracket list only when nothing separates it from
/// the last argument. A separator touches the item before it, so it goes at the start of the
/// horizontal space that is already there and that space ends up after it (`a: int b` becomes
/// `a: int, b`, not `a: int , b`) and it brings a trailing space when there is none. A word stands
/// alone on both sides: it keeps the space in front of it and brings the one behind.
Insertion insertionFor(const TokenBuffer &buffer, uint32_t index, TokenKind kind) {
    const std::string text(tokenKindText(kind));
    switch (kind) {
    case TokenKind::LParen:
    case TokenKind::RParen:
    case TokenKind::LBracket:
    case TokenKind::RBracket:
    case TokenKind::LBrace:
    case TokenKind::RBrace:
    case TokenKind::Lt:
    case TokenKind::LAngle:
    case TokenKind::Gt:
    case TokenKind::Bang:
    case TokenKind::BangBang:
    case TokenKind::Tilde:
    case TokenKind::Question:
        return {buffer[index].begin, text};
    default:
        break;
    }
    const std::string &source = buffer.source();
    const uint32_t begin      = buffer[index].begin;
    uint32_t run              = begin;
    while (run > 0 && (source[run - 1] == ' ' || source[run - 1] == '\t')) {
        --run;
    }
    const bool spaced = run < begin;
    if (kind == TokenKind::Comma || kind == TokenKind::Semi || kind == TokenKind::Colon) {
        return spaced ? Insertion{run, text} : Insertion{begin, text + " "};
    }
    return spaced ? Insertion{begin, text + " "} : Insertion{begin, " " + text + " "};
}

/// The node of kind `kind` that covers `token`, innermost first; a null node when there is none.
/// The parser closes a declaration after it has read what trails it, so the last token of the
/// node that ends at `token` is the end of the trailing part.
Node findNodeCovering(const Node &node, NodeKind kind, uint32_t token) {
    for (size_t i = 0; i < node.childCount(); ++i) {
        const Child child = node.child(i);
        if (!child.isNode()) {
            continue;
        }
        const Node candidate = child.node();
        const TokenRef first = candidate.firstToken();
        const TokenRef last  = candidate.lastToken();
        if (!first || !last || first.index() > token || last.index() < token) {
            continue;
        }
        if (candidate.is(kind)) {
            return candidate;
        }
        if (Node deeper = findNodeCovering(candidate, kind, token)) {
            return deeper;
        }
    }
    return Node();
}

} // namespace

void reportLexicalErrors(const TokenBuffer &buffer, Diagnostics &diagnostics) {
    for (const LexError &error : buffer.errors()) {
        const size_t line   = buffer.lineOf(error.begin);
        const size_t column = buffer.columnOf(error.begin);
        const CharRange range{{line, column}, {line, column + 1}};
        switch (error.kind) {
        case LexErrorKind::InvalidNumber: {
            // The whole written run (`09`, `077`, `0o8`), not one character of it: the text is
            // what the reader has to change.
            const std::string text(buffer.source().substr(error.begin, error.end - error.begin));
            auto builder = diagnostics.of(LexicalDiag::InvalidNumberFormat)
                               .at(CharRange{{line, column}, {line, buffer.columnOf(error.end)}});
            if (auto decimal = decimalOfLeadingZeroRun(text)) {
                builder.fix(
                    "write the number without its leading zero",
                    rangeOfBytes(buffer, error.begin, error.end),
                    *decimal);
            }
            builder.commit(text);
            break;
        }
        case LexErrorKind::UnclosedString: {
            const std::string &source = buffer.source();
            const char quote = source[error.begin];
            // Close the string where its writer almost certainly meant to: the end of the line it
            // opens on. The lexer reads an unclosed string all the way to the end of the file, so
            // a quote there would compile and put every following line inside the string - a fix
            // that is applied without asking must not quietly change what the program means.
            size_t lineEnd = source.find('\n', error.begin);
            if (lineEnd == std::string::npos) {
                lineEnd = source.size();
            } else if (lineEnd > error.begin && source[lineEnd - 1] == '\r') {
                --lineEnd;
            }
            diagnostics.of(LexicalDiag::UnclosedStringLiteral)
                .at(range)
                .fix(
                    "close the string at the end of the line",
                    pointAt(buffer, static_cast<uint32_t>(lineEnd)),
                    std::string(1, quote))
                .commit(std::string(1, quote));
            break;
        }
        case LexErrorKind::UnterminatedComment:
            diagnostics.of(LexicalDiag::UnterminatedComment)
                .at(CharRange{{line, column}, {line, column + 2}})
                .fix("close the comment", pointAt(buffer, error.end), "*/")
                .commit("'/*' without its '*/'");
            break;
        case LexErrorKind::IllegalCharacter: {
            // The first character only, whole (the text is UTF-8).
            size_t length = 1;
            while (error.begin + length < error.end &&
                   (static_cast<unsigned char>(buffer.source()[error.begin + length]) & 0xC0) ==
                       0x80) {
                ++length;
            }
            diagnostics.of(LexicalDiag::IllegalCharacter)
                .at(range)
                .fix("remove it", rangeOfBytes(buffer, error.begin, error.begin + length), "")
                .commit(buffer.source().substr(error.begin, length));
            break;
        }
        }
    }
}

void reportSyntaxErrors(const Tree &tree, Diagnostics &diagnostics) {
    const TokenBuffer &buffer = tree.tokens();
    if (tree.errors().empty()) {
        return;
    }
    const BracketScan scan(buffer);
    std::set<uint32_t> reportedStrays;
    std::set<std::pair<int, uint32_t>> reported; // (code, token): one diagnostic per place

    auto at = [&](uint32_t token) { return rangeOfToken(buffer, token); };

    for (const SyntaxError &error : tree.errors()) {
        const uint32_t offending = error.token;
        const Token &offToken    = buffer[offending];
        const bool atEof         = offToken.kind == TokenKind::Eof;
        const std::string off    = atEof ? std::string() : std::string(buffer.text(offToken));
        const size_t position    = scan.positionOf(offending);
        const std::vector<OpenDelimiter> &open =
            position < scan.openAt.size() ? scan.openAt[position] : scan.openAt.back();
        const uint32_t previous = position > 0 ? scan.significant[position - 1] : offending;

        auto once = [&](SyntaxDiag code, uint32_t token) {
            return reported.emplace(static_cast<int>(code), token).second;
        };
        auto innermost = [&](char kind) -> const OpenDelimiter * {
            for (auto it = open.rbegin(); it != open.rend(); ++it) {
                if (it->kind == kind) {
                    return &*it;
                }
            }
            return nullptr;
        };
        auto missingCloser = [&](char closer, const OpenDelimiter *opener) {
            if (!once(SyntaxDiag::MissingClosingDelimiter, opener ? opener->token : offending)) {
                return;
            }
            auto builder = diagnostics.of(SyntaxDiag::MissingClosingDelimiter).at(at(offending));
            if (opener != nullptr) {
                builder.related(
                    at(opener->token),
                    std::format("the '{}' that is not closed is opened", openerOf(closer)));
            }
            // The closer goes right after the last token that was read: what is missing is the
            // bracket, and the formatter puts it on its own line if it belongs there.
            if (previous != offending) {
                builder.fix(
                    std::format("insert the '{}' that is missing", closer),
                    pointAt(buffer, buffer[previous].end),
                    std::string(1, closer));
            }
            builder.commit(
                std::string(1, closer),
                std::string(1, openerOf(closer)),
                describeToken(buffer, offending));
        };

        // The whitespace immediately in front of `token`, without the comments that may stand
        // further back: what a fix removes when a space is the mistake.
        auto spaceBefore = [&](uint32_t token) {
            uint32_t begin = buffer[token].begin;
            uint32_t end   = begin;
            const std::string &source = buffer.source();
            while (end > 0 && (source[end - 1] == ' ' || source[end - 1] == '\t')) {
                --end;
            }
            return rangeOfBytes(buffer, end, begin);
        };

        // 1. `;;`: one separator between statements.
        if (error.kind == SyntaxError::Kind::ExtraSemicolon) {
            if (once(SyntaxDiag::ExtraSemicolon, offending)) {
                diagnostics.of(SyntaxDiag::ExtraSemicolon)
                    .at(at(offending))
                    .fix("remove the extra ';'", at(offending), "")
                    .commit();
            }
            continue;
        }

        // 2. A closing bracket that closes nothing, at or before the place the parser gave up
        //    (or on its line: the parser may stop a token or two early).
        {
            bool handled = false;
            for (const auto &[stray, context] : scan.strays) {
                if (reportedStrays.contains(stray)) {
                    continue;
                }
                if (stray > offending && buffer.lineOf(buffer[stray].begin) !=
                                             buffer.lineOf(offToken.begin)) {
                    break;
                }
                reportedStrays.insert(stray);
                const char closer = buffer.text(stray)[0];
                auto builder =
                    diagnostics.of(SyntaxDiag::UnmatchedClosingDelimiter).at(at(stray));
                if (!context.empty()) {
                    builder.related(
                        at(context.back().token),
                        std::format(
                            "the innermost open bracket is this '{}', which '{}' does not close",
                            context.back().kind,
                            closer));
                }
                builder.fix(std::format("remove the unmatched '{}'", closer), at(stray), "");
                builder.commit(std::string(1, closer), std::string(1, openerOf(closer)));
                handled = true;
                break;
            }
            if (handled) {
                continue;
            }
        }

        // `x => e`. A lambda's parameters need their parentheses (design principles 5.9; the
        // rule on `composition-expected-type-edges`, item 4): where a
        // name stands directly before an arrow that fits nothing, the writer wrote the one
        // spelling the language does not have. It is reported by name rather than as a token that
        // did not fit, and before the repairs below, because the same mistake reaches them as a
        // missing ',' (`[x => 1]`, `f(a, x => 1)`) or as the '<' of an angle-bracket list
        // (`xs->map<x => x + 1>`), where the named diagnostic is the one that says what to write.
        // A rule body is the one place a name before an arrow is read: there the arrow is the
        // rule's own (`rule r(x: int) { x => x }`), and it is consumed before any of this.
        if (offToken.kind == TokenKind::FatArrow && previous != offending &&
            buffer[previous].kind == TokenKind::Identifier) {
            if (once(SyntaxDiag::LambdaParametersNeedParentheses, offending)) {
                auto builder =
                    diagnostics.of(SyntaxDiag::LambdaParametersNeedParentheses).at(at(offending));
                // The repair is one text when a single name stands before the arrow: wrap it. A
                // list of names (`x, y => e`) is not wrapped, because the names before the arrow
                // read the same way as the arguments of a call (`f(a, x => e)`), and a fix that
                // wraps the wrong range is a wrong change a tool would apply without asking.
                const size_t previousPosition = scan.positionOf(previous);
                const uint32_t beforePrevious =
                    previousPosition > 0 ? scan.significant[previousPosition - 1] : previous;
                if (beforePrevious == previous || buffer[beforePrevious].kind != TokenKind::Comma) {
                    builder.fix(
                        "put the parameters in parentheses",
                        at(previous),
                        std::format("({})", buffer.text(buffer[previous])));
                }
                builder.commit();
            }
            continue;
        }

        // 3. Rule 1: the parse failed directly inside an angle-bracket list.
        if (error.kind == SyntaxError::Kind::AngleCloseAfterSpace) {
            if (once(SyntaxDiag::AngleCloseAfterSpace, error.aux)) {
                auto builder = diagnostics.of(SyntaxDiag::AngleCloseAfterSpace).at(at(error.aux));
                // The space that has to go is the one between the last argument and the '>'
                // (error.aux); `offending` is where the still-open list was noticed instead.
                const CharRange space = spaceBefore(error.aux);
                if (!(space.start.line == space.end.line &&
                      space.start.character == space.end.character)) {
                    builder.fix("remove the space before '>'", space, "");
                }
                builder.commit(describeToken(buffer, offending));
            }
            continue;
        }
        const bool comparison = error.kind == SyntaxError::Kind::ComparisonNeedsSpace;
        if (comparison &&
            buffer.lineOf(buffer[error.aux].begin) == buffer.lineOf(offToken.begin)) {
            // `i<n` fails within the line; an argument list that is wrong further down is
            // reported where it is wrong, below, with a note at its `<`.
            if (once(SyntaxDiag::ComparisonNeedsSpace, error.aux)) {
                const size_t anglePosition = scan.positionOf(error.aux);
                const uint32_t host =
                    anglePosition > 0 ? scan.significant[anglePosition - 1] : error.aux;
                diagnostics.of(SyntaxDiag::ComparisonNeedsSpace)
                    .at(at(error.aux))
                    .fix(
                        "write a space before '<'",
                        pointAt(buffer, buffer[error.aux].begin),
                        " ")
                    .commit(describeToken(buffer, host), describeToken(buffer, offending));
            }
            continue;
        }
        if (error.kind == SyntaxError::Kind::LineStartsWithSign) {
            if (once(SyntaxDiag::LineStartsWithOperator, offending)) {
                auto builder = diagnostics.of(SyntaxDiag::LineStartsWithOperator).at(at(offending));
                // The operator cannot start a line, so the line it starts belongs to the one
                // before it: the break and the indent become one space.
                const std::string &source = buffer.source();
                uint32_t lineStart        = buffer[offending].begin;
                while (lineStart > 0 && source[lineStart - 1] != '\n') {
                    --lineStart;
                }
                if (lineStart > 0) {
                    builder.fix(
                        "join this line to the one before it",
                        rangeOfBytes(buffer, lineStart - 1, buffer[offending].begin),
                        " ");
                }
                builder.commit(off);
            }
            continue;
        }

        // 4. The parser needed exactly one token.
        if (error.kind == SyntaxError::Kind::Missing && error.expected.size() == 1 &&
            error.expected[0] < SyntaxError::Expression) {
            const TokenKind wanted = static_cast<TokenKind>(error.expected[0]);
            if (isCloser(wanted)) {
                const char closer = tokenKindText(wanted)[0];
                missingCloser(closer, innermost(openerOf(closer)));
                continue;
            }
            if (!atEof || open.empty()) {
                if (once(SyntaxDiag::MissingToken, offending)) {
                    auto builder = diagnostics.of(SyntaxDiag::MissingToken).at(at(offending));
                    if (auto spelling = spellingOf(error.expected[0])) {
                        const Insertion insertion =
                            insertionFor(buffer, offending, static_cast<TokenKind>(error.expected[0]));
                        builder.fix(
                            std::format("insert '{}'", *spelling),
                            pointAt(buffer, insertion.at),
                            insertion.text);
                    }
                    builder.commit(
                        describeExpected(error.expected), describeToken(buffer, offending));
                }
                continue;
            }
        }
        if (error.kind == SyntaxError::Kind::MissingThen) {
            if (once(SyntaxDiag::MissingThen, offending)) {
                const Insertion insertion =
                    insertionFor(buffer, offending, TokenKind::KwThen);
                diagnostics.of(SyntaxDiag::MissingThen)
                    .at(at(offending))
                    .fix("insert 'then'", pointAt(buffer, insertion.at), insertion.text)
                    .commit(describeToken(buffer, offending));
            }
            continue;
        }

        // 5. `if x = 1 then`: assignment where a comparison was meant.
        if (error.kind == SyntaxError::Kind::AssignmentInCondition) {
            if (once(SyntaxDiag::AssignmentInCondition, offending)) {
                diagnostics.of(SyntaxDiag::AssignmentInCondition).at(at(offending)).commit();
            }
            continue;
        }

        // 6. `rule f(x) { … } when c`: the condition of a declaration belongs before its body,
        //    so the trailing `when` is named rather than left as a token that did not fit.
        if (error.kind == SyntaxError::Kind::WhenAfterBody) {
            if (once(SyntaxDiag::WhenAfterBody, offending)) {
                auto builder = diagnostics.of(SyntaxDiag::WhenAfterBody).at(at(offending));
                // The clause moves, so the fix has two edits: the text of the clause is written
                // out in front of the body's '{' and taken away from where it stands. Both
                // places come from the tokens alone - the '{' is the one the '}' in front of
                // `when` closes, and the clause runs to the last token of the declaration it
                // ends.
                const OpenDelimiter *body = nullptr;
                if (position > 0 && position - 1 < scan.openAt.size()) {
                    const std::vector<OpenDelimiter> &beforeTheBrace = scan.openAt[position - 1];
                    for (auto it = beforeTheBrace.rbegin(); it != beforeTheBrace.rend(); ++it) {
                        if (it->kind == '{') {
                            body = &*it;
                            break;
                        }
                    }
                }
                const Node decl = findNodeCovering(tree.root(), NodeKind::RuleDecl, offending);
                const TokenRef clauseEnd = decl ? decl.lastToken() : TokenRef();
                if (body != nullptr && clauseEnd && clauseEnd.index() >= offending) {
                    const std::string clause(buffer.source().substr(
                        buffer[offending].end, clauseEnd.end() - buffer[offending].end));
                    builder.fix(
                        "move the condition in front of the body",
                        pointAt(buffer, buffer[body->token].begin),
                        "when" + clause + " ");
                    builder.alsoFix(
                        rangeOfBytes(buffer, buffer[offending].begin, clauseEnd.end()), "");
                }
                builder.commit(describeToken(buffer, offending));
            }
            continue;
        }

        // 7. A keyword where only a name can stand.
        if (error.kind == SyntaxError::Kind::KeywordAsName) {
            if (once(SyntaxDiag::KeywordAsIdentifier, offending)) {
                diagnostics.of(SyntaxDiag::KeywordAsIdentifier).at(at(offending)).commit(off);
            }
            continue;
        }

        // 8. A '(' or '[' still open where a statement starts, a block ends or the file ends;
        // 6b. `x as int..5`: the interval reading of `..` written where a type stands. The type
        //     of an `as` ends at the stream mark, and what follows reads as the far end of an
        //      interval (the rule on the stream surface, option (c)).
        if (error.kind == SyntaxError::Kind::AsRightSideIsType) {
            if (once(SyntaxDiag::AsRightSideIsType, offending)) {
                const Token &op = buffer[error.aux];
                diagnostics.of(SyntaxDiag::AsRightSideIsType)
                    .at(at(offending))
                    .commit(std::string(buffer.text(op)));
            }
            continue;
        }

        // 7. A '(' or '[' still open where a statement starts, a block ends or the file ends;
        //    or the end of the file with a block open.
        if (!open.empty()) {
            static const std::set<std::string> kStatementStart = {
                "let", "var", "return", "func", "type", "import", "export", "}"};
            const OpenDelimiter &top = open.back();
            if (atEof || (top.kind != '{' && kStatementStart.contains(off))) {
                missingCloser(closerOf(top.kind), &top);
                continue;
            }
        }

        // 9. Two items of a list without a ',' between them.
        if (!atEof && !isCloser(offToken.kind) && !error.expected.empty() &&
            std::find(error.expected.begin(), error.expected.end(),
                      static_cast<uint8_t>(TokenKind::Comma)) != error.expected.end() &&
            std::all_of(error.expected.begin(), error.expected.end(), [](uint8_t item) {
                const TokenKind kind = static_cast<TokenKind>(item);
                return item < SyntaxError::Expression &&
                       (kind == TokenKind::Comma || isCloser(kind));
            })) {
            if (once(SyntaxDiag::MissingComma, offending)) {
                const Insertion insertion =
                    insertionFor(buffer, offending, TokenKind::Comma);
                diagnostics.of(SyntaxDiag::MissingComma)
                    .at(at(offending))
                    .fix("insert ','", pointAt(buffer, insertion.at), insertion.text)
                    .commit(describeToken(buffer, offending), describeExpected(error.expected));
            }
            continue;
        }

        // 10. `let x` without a value: reported at the name.
        if (error.kind == SyntaxError::Kind::MissingInitializer) {
            if (once(SyntaxDiag::MissingInitializer, offending)) {
                diagnostics.of(SyntaxDiag::MissingInitializer).at(at(offending)).commit(off);
            }
            continue;
        }

        // 11. Anything else: the token that does not fit and what would have.
        const std::string wanted = describeExpected(error.expected);
        const std::string clause = wanted.empty() ? std::string() : "; expected " + wanted;
        if (atEof) {
            if (once(SyntaxDiag::UnexpectedEndOfFile, offending)) {
                diagnostics.of(SyntaxDiag::UnexpectedEndOfFile).at(at(offending)).commit(clause);
            }
            continue;
        }
        if (!once(SyntaxDiag::UnexpectedToken, offending)) {
            continue;
        }
        auto builder = diagnostics.of(SyntaxDiag::UnexpectedToken).at(at(offending));
        if (off == "else") {
            builder.note("'else' belongs to a conditional: if <condition> then <value> else <value>");
        } else if (off == "then") {
            builder.note("'then' follows the condition of an 'if'");
        }
        if (comparison) {
            builder.related(
                at(error.aux),
                "this '<' touches the token before it, so it opens an angle-bracket argument "
                "list (a comparison needs a space before '<'); the list is opened");
        } else if (offToken.kind == TokenKind::Lt && offToken.spaceBefore() &&
                   previous != offending && buffer[previous].kind == TokenKind::Identifier) {
            builder.note(
                "'<' after a space is the comparison operator; the '<' of an angle-bracket "
                "argument list touches the name before it: f<a>");
        }
        builder.commit(describeToken(buffer, offending), clause);
    }
}

} // namespace camel::parse::syntax
