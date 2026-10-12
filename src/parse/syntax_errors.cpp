/**
 * Copyright (c) 2024 the OpenCML Organization
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
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Classification of lexer and parser errors (see syntax_errors.h for the policy).
 *
 * The parser listener works from three things: the token ANTLR stopped at, the set of tokens
 * it says it expected (read back from its message, the one place every kind of error states
 * it), and the brackets that are open at that token, found by a scan of the token stream that
 * is independent of the parser's state.
 */

#include "camel/parse/syntax_errors.h"

#include "camel/parse/antlr/OpenCMLLexer.h"

#include <algorithm>
#include <format>
#include <set>

using namespace camel::core::error;

namespace camel::parse {

namespace {

bool isWord(const std::string &text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isalpha(c) || c == '_';
    });
}

char closerOf(char opener) { return opener == '(' ? ')' : opener == '[' ? ']' : '}'; }
char openerOf(char closer) { return closer == ')' ? '(' : closer == ']' ? '[' : '{'; }
bool isOpener(const std::string &t) { return t == "(" || t == "[" || t == "{"; }
bool isCloser(const std::string &t) { return t == ")" || t == "]" || t == "}"; }

/// How a token is named in a message.
std::string describeToken(antlr4::Token *token) {
    if (token == nullptr || token->getType() == antlr4::Token::EOF) {
        return "the end of the file";
    }
    std::string text = token->getText();
    if (auto nl = text.find('\n'); nl != std::string::npos) {
        text = text.substr(0, nl) + "...";
    }
    if (displayWidth(text) > 24) {
        // Cut at a code point boundary.
        size_t cut = 20;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        text = text.substr(0, cut) + "...";
    }
    return "'" + text + "'";
}

/// The members of the set in "... expecting 'x'" / "... expecting {'x', NAME, <EOF>}", as ANTLR
/// writes them: quoted literals, symbolic token names and <EOF>.
std::vector<std::string> expectedOf(const std::string &msg) {
    static const std::string kMarker = " expecting ";
    std::vector<std::string> items;
    auto at = msg.find(kMarker);
    if (at == std::string::npos) {
        return items;
    }
    const std::string set = msg.substr(at + kMarker.size());
    for (size_t i = 0; i < set.size();) {
        const char c = set[i];
        if (c == '\'') {
            // A quoted literal runs to the next quote; no token of the grammar contains one.
            const size_t close = set.find('\'', i + 1);
            if (close == std::string::npos) {
                break;
            }
            items.push_back(set.substr(i, close - i + 1));
            i = close + 1;
        } else if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '<') {
            size_t end = i;
            while (end < set.size() && set[end] != ',' && set[end] != '}' && set[end] != ' ') {
                ++end;
            }
            items.push_back(set.substr(i, end - i));
            i = end;
        } else {
            ++i;
        }
    }
    return items;
}

bool has(const std::vector<std::string> &items, const std::string &item) {
    return std::find(items.begin(), items.end(), item) != items.end();
}

/// The expected set in words: token names are replaced by what they stand for, and the long
/// sets that mean "any expression" or "any declaration" by those words.
std::string describeExpected(std::vector<std::string> items) {
    static const std::set<std::string> kExpressionStart = {
        "IDENTIFIER", "INTEGER", "REAL",    "STRING",   "MULTI_STR", "FSTRING",  "'null'",
        "'true'",     "'false'", "'('",     "'['",      "'{'",       "'<'",      "'-'",
        "'!'",        "'~'",     "'wait'",  "'if'",     "'match'",   "'try'",    "'sync'",
        "'macro'",    "'atomic'", "'shared'", "'func'", "'typeof'",  "'keyof'",  "'throw'",
        "'raise'",    "'+'",     "'...'",   "'inner'",  "'outer'"};
    static const std::set<std::string> kDeclarationStart = {
        "'@'",      "'let'",    "'var'",    "'use'",    "'func'",      "'type'",  "'with'",
        "'module'", "'import'", "'export'", "'using'",  "'namespace'", "'sync'",  "'macro'",
        "'inner'",  "'outer'",  "'atomic'", "'shared'", "'enum'",      "'return'", "'wait'"};
    std::vector<std::string> out;
    auto add = [&](const std::string &s) {
        if (!has(out, s)) {
            out.push_back(s);
        }
    };
    const bool expression =
        has(items, "IDENTIFIER") && (has(items, "INTEGER") || has(items, "STRING"));
    const bool declaration = has(items, "'func'") && has(items, "'let'") && has(items, "'import'");
    for (const std::string &item : items) {
        if (expression && kExpressionStart.contains(item)) {
            continue;
        }
        if (declaration && kDeclarationStart.contains(item)) {
            continue;
        }
        if (item == "IDENTIFIER") {
            add("a name");
        } else if (item == "INTEGER" || item == "REAL") {
            add("a number");
        } else if (item == "STRING" || item == "MULTI_STR" || item == "FSTRING") {
            add("a string");
        } else if (item == "<EOF>" || item == "EOF") {
            add("the end of the file");
        } else if (item == "SEP") {
            add("';'");
        } else {
            add(item);
        }
    }
    if (declaration) {
        out.insert(out.begin(), "a declaration");
    }
    if (expression) {
        out.insert(out.begin(), "an expression");
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
    antlr4::Token *token;
};

} // namespace

// ---- Lexer ----
void LexerErrorListener::syntaxError(
    antlr4::Recognizer *, antlr4::Token *, size_t line, size_t charPositionInLine,
    const std::string &msg, std::exception_ptr) {
    // "token recognition error at: '<text>'": the text the lexer could not start a token with.
    static const std::string kMarker = "at: '";
    std::string text;
    if (auto at = msg.find(kMarker); at != std::string::npos) {
        text = msg.substr(at + kMarker.size());
        if (!text.empty() && text.back() == '\'') {
            text.pop_back();
        }
    }
    const size_t zeroLine = line > 0 ? line - 1 : 0;
    const CharRange range{{zeroLine, charPositionInLine}, {zeroLine, charPositionInLine + 1}};
    const char first = text.empty() ? '\0' : text[0];
    if (first == '\'' || first == '"' || first == '`') {
        any_ = false;
        diagnostics_->of(LexicalDiag::UnclosedStringLiteral)
            .at(range)
            .commit(std::string(1, first));
        return;
    }
    // A run of characters that start no token (a word in another script) is one mistake.
    const bool continues = any_ && lastLine_ == zeroLine && lastColumn_ == charPositionInLine;
    any_                 = true;
    lastLine_            = zeroLine;
    lastColumn_          = charPositionInLine + 1;
    if (continues) {
        return;
    }
    // The first character only, whole (the text is UTF-8).
    size_t length = text.empty() ? 0 : 1;
    while (length < text.size() && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) {
        ++length;
    }
    diagnostics_->of(LexicalDiag::IllegalCharacter).at(range).commit(text.substr(0, length));
}

// ---- Parser ----
void ParserErrorListener::syntaxError(
    antlr4::Recognizer *recognizer, antlr4::Token *offending, size_t line,
    size_t charPositionInLine, const std::string &msg, std::exception_ptr) {
    if (hasErrors_) {
        // Later errors of the same parse are consequences of the recovery (see the header).
        return;
    }
    hasErrors_ = true;
    if (diagnostics_->count(DiagType::LexicalDiag) > 0) {
        return;
    }

    auto at = [&](antlr4::Token *token) {
        if (token != nullptr) {
            return CharRange::fromTokens(token, token);
        }
        const size_t zeroLine = line > 0 ? line - 1 : 0;
        return CharRange{{zeroLine, charPositionInLine}, {zeroLine, charPositionInLine}};
    };
    auto report = [&](SyntaxDiag code, antlr4::Token *where) {
        return diagnostics_->of(code).at(at(where));
    };

    // The tokens the parser sees (the default channel), and the offending one among them.
    std::vector<antlr4::Token *> tokens;
    if (auto *parser = dynamic_cast<antlr4::Parser *>(recognizer)) {
        if (auto *stream = dynamic_cast<antlr4::BufferedTokenStream *>(parser->getTokenStream())) {
            stream->fill();
            for (antlr4::Token *token : stream->getTokens()) {
                if (token->getChannel() == antlr4::Token::DEFAULT_CHANNEL) {
                    tokens.push_back(token);
                }
            }
        }
    }
    size_t offIndex = tokens.size();
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (offending != nullptr && tokens[i]->getTokenIndex() == offending->getTokenIndex()) {
            offIndex = i;
            break;
        }
    }
    antlr4::Token *previous = offIndex > 0 && offIndex <= tokens.size() && !tokens.empty()
                                  ? tokens[std::min(offIndex, tokens.size()) - 1]
                                  : nullptr;
    const bool atEof      = offending == nullptr || offending->getType() == antlr4::Token::EOF;
    const std::string off = atEof ? std::string() : offending->getText();

    // The brackets open at the offending token, and the first closing bracket of the file
    // that closes nothing.
    std::vector<OpenDelimiter> openAtOffending;
    antlr4::Token *strayCloser = nullptr;
    std::vector<OpenDelimiter> strayContext;
    {
        std::vector<OpenDelimiter> stack;
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i == offIndex) {
                openAtOffending = stack;
            }
            if (tokens[i]->getType() == antlr4::Token::EOF) {
                break;
            }
            const std::string text = tokens[i]->getText();
            if (isOpener(text)) {
                stack.push_back({text[0], tokens[i]});
            } else if (isCloser(text)) {
                if (!stack.empty() && stack.back().kind == openerOf(text[0])) {
                    stack.pop_back();
                } else if (strayCloser == nullptr) {
                    strayCloser  = tokens[i];
                    strayContext = stack;
                    break;
                }
            }
        }
        if (offIndex >= tokens.size()) {
            openAtOffending = stack;
        }
    }
    auto innermost = [&](char kind) -> antlr4::Token * {
        for (auto it = openAtOffending.rbegin(); it != openAtOffending.rend(); ++it) {
            if (it->kind == kind) {
                return it->token;
            }
        }
        return nullptr;
    };
    auto missingCloser = [&](char closer, antlr4::Token *opener) {
        auto builder = report(SyntaxDiag::MissingClosingDelimiter, offending);
        if (opener != nullptr) {
            builder.related(
                at(opener),
                std::format("the '{}' that is not closed is opened", openerOf(closer)));
        }
        builder.commit(
            std::string(1, closer),
            std::string(1, openerOf(closer)),
            describeToken(offending));
    };

    const std::vector<std::string> expected = expectedOf(msg);
    const bool missing                      = msg.rfind("missing ", 0) == 0;
    const bool noViable                     = msg.rfind("no viable alternative", 0) == 0;

    // 1. `;;`: the grammar takes one separator between statements.
    if (off == ";" && previous != nullptr && previous->getText() == ";") {
        report(SyntaxDiag::ExtraSemicolon, offending).commit();
        return;
    }
    // 2. A closing bracket that closes nothing, at or before the place the parser gave up (or
    //    on its line: the parser often stops a token or two early).
    if (strayCloser != nullptr &&
        (offending == nullptr || strayCloser->getTokenIndex() <= offending->getTokenIndex() ||
         strayCloser->getLine() == offending->getLine())) {
        const char closer = strayCloser->getText()[0];
        auto builder      = report(SyntaxDiag::UnmatchedClosingDelimiter, strayCloser);
        if (!strayContext.empty()) {
            builder.related(
                at(strayContext.back().token),
                std::format(
                    "the innermost open bracket is this '{}', which '{}' does not close",
                    strayContext.back().kind,
                    closer));
        }
        builder.commit(std::string(1, closer), std::string(1, openerOf(closer)));
        return;
    }
    // 3. "missing X at Y": the parser knows the one token that would let it go on.
    if (missing) {
        // missing 'X' at 'Y'  /  missing NAME at 'Y'
        std::string wanted = msg.substr(8, msg.find(" at ") == std::string::npos
                                               ? std::string::npos
                                               : msg.find(" at ") - 8);
        if (wanted == "')'" || wanted == "']'" || wanted == "'}'") {
            missingCloser(wanted[1], innermost(openerOf(wanted[1])));
            return;
        }
        if (wanted == "'then'") {
            report(SyntaxDiag::MissingThen, offending).commit(describeToken(offending));
            return;
        }
        report(SyntaxDiag::MissingToken, offending)
            .commit(describeExpected({wanted}), describeToken(offending));
        return;
    }
    // 4. `if x = 1 then`: assignment where a comparison was meant.
    if (off == "=" && has(expected, "'then'")) {
        report(SyntaxDiag::AssignmentInCondition, offending).commit();
        return;
    }
    // 5. A keyword where only a name can stand (not where any expression can).
    if (!atEof && isWord(off) && offending->getType() != OpenCMLLexer::IDENTIFIER &&
        has(expected, "IDENTIFIER") && !has(expected, "INTEGER")) {
        report(SyntaxDiag::KeywordAsIdentifier, offending).commit(off);
        return;
    }
    // 6. A '(' or '[' still open where a statement starts, a block ends or the file ends; or
    //    the end of the file with a block open.
    if (!openAtOffending.empty()) {
        static const std::set<std::string> kStatementStart = {
            "let", "var", "return", "func", "type", "import", "export", "module", "use", "}"};
        const OpenDelimiter &top = openAtOffending.back();
        if (atEof || (top.kind != '{' && kStatementStart.contains(off))) {
            missingCloser(closerOf(top.kind), top.token);
            return;
        }
    }
    // 7. Two items of a list without a ',' between them.
    if (!atEof && !expected.empty() && has(expected, "','") && !isCloser(off) &&
        std::all_of(expected.begin(), expected.end(), [](const std::string &e) {
            return e == "','" || e == "')'" || e == "']'" || e == "'}'";
        })) {
        report(SyntaxDiag::MissingComma, offending)
            .commit(describeToken(offending), describeExpected(expected));
        return;
    }
    // 8. `let x` without a value.
    if (expected.size() == 2 && has(expected, "':'") && has(expected, "'='") &&
        previous != nullptr) {
        report(SyntaxDiag::MissingInitializer, previous).commit(previous->getText());
        return;
    }
    // 9. Anything else: the token that does not fit and, when the parser states it, what
    //    would have. ("no viable alternative" states nothing.)
    const std::string wanted = noViable ? std::string() : describeExpected(expected);
    const std::string clause = wanted.empty() ? std::string() : "; expected " + wanted;
    if (atEof) {
        report(SyntaxDiag::UnexpectedEndOfFile, offending).commit(clause);
        return;
    }
    if (!noViable && expected.empty() && msg.rfind("mismatched", 0) != 0 &&
        msg.rfind("extraneous", 0) != 0) {
        // A message of a form not known here: passed on as it is.
        report(SyntaxDiag::UnknownSyntaxError, offending).commit(msg);
        return;
    }
    auto builder = report(SyntaxDiag::UnexpectedToken, offending);
    if (off == "else") {
        builder.note("'else' belongs to a conditional: if <condition> then <value> else <value>");
    } else if (off == "then") {
        builder.note("'then' follows the condition of an 'if'");
    }
    builder.commit(describeToken(offending), clause);
}

} // namespace camel::parse
