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

} // namespace

void reportLexicalErrors(const TokenBuffer &buffer, Diagnostics &diagnostics) {
    for (const LexError &error : buffer.errors()) {
        const size_t line   = buffer.lineOf(error.begin);
        const size_t column = buffer.columnOf(error.begin);
        const CharRange range{{line, column}, {line, column + 1}};
        switch (error.kind) {
        case LexErrorKind::InvalidNumber: {
            // The whole written run (`09`, `077`, `0o8`), not one character of it: the text is
            // what the reader has to change (H15i).
            const std::string text(buffer.source().substr(error.begin, error.end - error.begin));
            diagnostics.of(LexicalDiag::InvalidNumberFormat)
                .at(CharRange{{line, column}, {line, buffer.columnOf(error.end)}})
                .commit(text);
            break;
        }
        case LexErrorKind::UnclosedString:
            diagnostics.of(LexicalDiag::UnclosedStringLiteral)
                .at(range)
                .commit(std::string(1, buffer.source()[error.begin]));
            break;
        case LexErrorKind::UnterminatedComment:
            diagnostics.of(LexicalDiag::UnterminatedComment)
                .at(CharRange{{line, column}, {line, column + 2}})
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
            builder.commit(
                std::string(1, closer),
                std::string(1, openerOf(closer)),
                describeToken(buffer, offending));
        };

        // 1. `;;`: one separator between statements.
        if (error.kind == SyntaxError::Kind::ExtraSemicolon) {
            if (once(SyntaxDiag::ExtraSemicolon, offending)) {
                diagnostics.of(SyntaxDiag::ExtraSemicolon).at(at(offending)).commit();
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
                builder.commit(std::string(1, closer), std::string(1, openerOf(closer)));
                handled = true;
                break;
            }
            if (handled) {
                continue;
            }
        }

        // 3. Rule 1: the parse failed directly inside an angle-bracket list.
        if (error.kind == SyntaxError::Kind::AngleCloseAfterSpace) {
            if (once(SyntaxDiag::AngleCloseAfterSpace, error.aux)) {
                diagnostics.of(SyntaxDiag::AngleCloseAfterSpace)
                    .at(at(error.aux))
                    .commit(describeToken(buffer, offending));
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
                    .commit(describeToken(buffer, host), describeToken(buffer, offending));
            }
            continue;
        }
        if (error.kind == SyntaxError::Kind::LineStartsWithSign) {
            if (once(SyntaxDiag::LineStartsWithOperator, offending)) {
                diagnostics.of(SyntaxDiag::LineStartsWithOperator).at(at(offending)).commit(off);
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
                    diagnostics.of(SyntaxDiag::MissingToken)
                        .at(at(offending))
                        .commit(describeExpected(error.expected), describeToken(buffer, offending));
                }
                continue;
            }
        }
        if (error.kind == SyntaxError::Kind::MissingThen) {
            if (once(SyntaxDiag::MissingThen, offending)) {
                diagnostics.of(SyntaxDiag::MissingThen)
                    .at(at(offending))
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

        // 6. A keyword where only a name can stand.
        if (error.kind == SyntaxError::Kind::KeywordAsName) {
            if (once(SyntaxDiag::KeywordAsIdentifier, offending)) {
                diagnostics.of(SyntaxDiag::KeywordAsIdentifier).at(at(offending)).commit(off);
            }
            continue;
        }

        // 6b. `x as int..5`: the interval reading of `..` written where a type stands. The type
        //     of an `as` ends at the stream mark, and what follows reads as the far end of an
        //     interval (the author's ruling of 2026-10-24 on the stream surface, option (c)).
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
                "let", "var", "return", "func", "type", "import", "export", "module", "}"};
            const OpenDelimiter &top = open.back();
            if (atEof || (top.kind != '{' && kStatementStart.contains(off))) {
                missingCloser(closerOf(top.kind), &top);
                continue;
            }
        }

        // 8. Two items of a list without a ',' between them.
        if (!atEof && !isCloser(offToken.kind) && !error.expected.empty() &&
            std::find(error.expected.begin(), error.expected.end(),
                      static_cast<uint8_t>(TokenKind::Comma)) != error.expected.end() &&
            std::all_of(error.expected.begin(), error.expected.end(), [](uint8_t item) {
                const TokenKind kind = static_cast<TokenKind>(item);
                return item < SyntaxError::Expression &&
                       (kind == TokenKind::Comma || isCloser(kind));
            })) {
            if (once(SyntaxDiag::MissingComma, offending)) {
                diagnostics.of(SyntaxDiag::MissingComma)
                    .at(at(offending))
                    .commit(describeToken(buffer, offending), describeExpected(error.expected));
            }
            continue;
        }

        // 9. `let x` without a value: reported at the name.
        if (error.kind == SyntaxError::Kind::MissingInitializer) {
            if (once(SyntaxDiag::MissingInitializer, offending)) {
                diagnostics.of(SyntaxDiag::MissingInitializer).at(at(offending)).commit(off);
            }
            continue;
        }

        // 10. Anything else: the token that does not fit and what would have.
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
