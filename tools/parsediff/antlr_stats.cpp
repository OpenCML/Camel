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
 * `camel-parsediff stats`: where a corpus stands with respect to the two rules the hand-written
 * parser adds, measured on the parse the ANTLR parser gives today.
 *
 * Rule 1 (angle brackets are adjacent): comparisons whose `<` touches the token before it,
 * angle-bracket argument lists whose `<` does not, and how the closing `>` is written.
 * Rule 2 (a line break ends a statement): every line break inside a statement that is not
 * inside a bracket, classified by the token before and after it, and the statements that share
 * a line without a `;`.
 *
 * The classification is by tokens and by the grammar rule that holds the two tokens around the
 * break. It is an estimate made before the new parser exists; the authoritative measurement is
 * the AST comparison of the two parsers (mode `diff`).
 */

#include "modes.h"

#include "antlr4-runtime/antlr4-runtime.h"
#include "camel/parse/antlr/OpenCMLLexer.h"
#include "camel/parse/antlr/OpenCMLParser.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace parsediff {

namespace {

using antlr4::ParserRuleContext;
using antlr4::Token;
using antlr4::tree::ParseTree;
using antlr4::tree::TerminalNode;

struct FileStats {
    std::string path;
    std::vector<std::string> sourceLines;
    std::vector<Token *> tokens;                       // default channel, in order
    std::unordered_map<size_t, size_t> positionOf;     // token index -> position in `tokens`
    std::unordered_map<size_t, TerminalNode *> nodeOf; // token index -> its leaf
    std::vector<std::string> ruleNames;
};

std::map<std::string, size_t> g_counts;
std::map<std::string, std::vector<std::string>> g_examples;
// The kinds every occurrence of which is listed, not only counted.
const std::set<std::string> kListed = {
    "R1.lt_compare_adjacent",   "R1.gt_compare_adjacent_in_angle", "R1.angle_args_spaced",
    "R1.type_args_spaced",      "R1.anno_args_spaced",             "R1.angle_close_spaced_expr",
    "R2.split",                 "R2.same_line_no_sep",
};

size_t endLine(Token *token) {
    const std::string text = token->getText();
    return token->getLine() + static_cast<size_t>(std::count(text.begin(), text.end(), '\n'));
}

void record(const FileStats &file, const std::string &kind, Token *at, const std::string &more) {
    ++g_counts[kind];
    const std::string head = kind.substr(0, kind.find(':'));
    if (!kListed.contains(head)) {
        return;
    }
    std::ostringstream os;
    os << file.path << ":" << at->getLine() << ":" << at->getCharPositionInLine() + 1;
    if (!more.empty()) {
        os << "  " << more;
    }
    if (at->getLine() >= 1 && at->getLine() <= file.sourceLines.size()) {
        std::string line = file.sourceLines[at->getLine() - 1];
        const size_t first = line.find_first_not_of(" \t");
        os << "  | " << (first == std::string::npos ? "" : line.substr(first, 110));
    }
    g_examples[head].push_back(os.str());
}

Token *previousOf(const FileStats &file, Token *token) {
    auto it = file.positionOf.find(token->getTokenIndex());
    if (it == file.positionOf.end() || it->second == 0) {
        return nullptr;
    }
    return file.tokens[it->second - 1];
}

bool adjacent(const FileStats &file, Token *token) {
    Token *previous = previousOf(file, token);
    return previous != nullptr && previous->getStopIndex() + 1 == token->getStartIndex();
}

bool newlineBefore(const FileStats &file, Token *token) {
    Token *previous = previousOf(file, token);
    return previous != nullptr && token->getLine() > endLine(previous);
}

template <typename T> bool is(ParseTree *tree) { return dynamic_cast<T *>(tree) != nullptr; }

bool isBracketContext(ParseTree *tree) {
    using P = OpenCMLParser;
    return is<P::TupleDataContext>(tree) || is<P::ParentArguesContext>(tree) ||
           is<P::ParentParamsContext>(tree) || is<P::IndicesContext>(tree) ||
           is<P::ArrayDataContext>(tree) || is<P::StructDataContext>(tree) ||
           is<P::StructTypeContext>(tree) || is<P::TupleTypeContext>(tree) ||
           is<P::ParentIdentsContext>(tree) || is<P::BracedIdentsContext>(tree) ||
           is<P::BracketIdentsContext>(tree) || is<P::AngledValuesContext>(tree) ||
           is<P::AngledParamsContext>(tree);
}

/// Whether `context` is directly inside an angle-bracket list: no round, square or curly
/// bracket and no block lies between it and the list.
bool atAngleTop(ParseTree *context) {
    using P = OpenCMLParser;
    for (ParseTree *p = context->parent; p != nullptr; p = p->parent) {
        if (is<P::AngledValuesContext>(p) || is<P::AngledParamsContext>(p) ||
            is<P::TypeOrDataContext>(p)) {
            return true;
        }
        if (isBracketContext(p) || is<P::StmtBlockContext>(p) || is<P::CtrlExprContext>(p) ||
            is<P::PatternContext>(p)) {
            // An `if` or `match` is not a bracket, but the `>` of its condition or its arms is
            // not what closes a list either way; counted with the nested ones.
            return is<P::CtrlExprContext>(p) ? atAngleTop(p) : false;
        }
    }
    return false;
}

std::string tokenClass(Token *token) {
    switch (token->getType()) {
    case OpenCMLLexer::IDENTIFIER:
        return "IDENT";
    case OpenCMLLexer::INTEGER:
    case OpenCMLLexer::REAL:
        return "NUMBER";
    case OpenCMLLexer::STRING:
    case OpenCMLLexer::MULTI_STR:
    case OpenCMLLexer::FSTRING:
        return "STRING";
    default:
        return token->getText();
    }
}

const std::set<std::string> kLineEndContinues = {
    "+",  "-",  "*",   "/",   "%",    "^",  "@",    "&&",  "||",     "==",    "!=",   "===",
    "!==", "<", ">",   "<=",  ">=",   "=",  "+=",   "-=",  "*=",     "/=",    "%=",   "^=",
    "@=", "&=", "|=",  "->",  "?->",  "..", "?..",  ".",   "??",     "!!",    ",",    "=>",
    ":",  "as", "is",  "then", "else", "if", "match", "case", "return", "throw", "raise", "wait",
    "let", "var", "func", "type", "with", "import", "export", "from", "in",  "for",   "of",
    "|",  "&",  "\\",  "!",   "~",    "module", "use", "using", "namespace", "typeof", "typeas"};
const std::set<std::string> kLineStartContinues = {
    "->", "?->", ".",  "..", "?..", "??", "!!", "*",  "/",  "%",  "^",    "&&",   "||",
    "==", "!=",  "===", "!==", "<", ">",  "<=", ">=", "as", "is", "=",    "+=",   "-=",
    "*=", "/=",  "%=", "^=", "@=",  "&=", "|=", "|",  "&",  "\\", ",",    "=>",   ":",
    "then", "else", "catch", "finally", "from", "in", "of"};


/// The line breaks inside the statement `unit` that belong to it and not to a statement
/// nested in it.
void classifyBreaks(FileStats &file, ParserRuleContext *unit) {
    using P = OpenCMLParser;
    Token *start = unit->getStart();
    Token *stop  = unit->getStop();
    if (start == nullptr || stop == nullptr) {
        return;
    }
    const size_t from = file.positionOf[start->getTokenIndex()];
    const size_t to   = file.positionOf[stop->getTokenIndex()];
    bool any          = false;
    int roundSquare   = 0;
    for (size_t i = from; i < to; ++i) {
        Token *a                = file.tokens[i];
        Token *b                = file.tokens[i + 1];
        const std::string aText = a->getText();
        if (aText == "(" || aText == "[") {
            ++roundSquare;
        } else if (aText == ")" || aText == "]") {
            --roundSquare;
        }
        if (b->getLine() <= endLine(a)) {
            continue;
        }
        // The lowest rule that holds both tokens.
        std::set<ParseTree *> above;
        for (ParseTree *p = file.nodeOf[a->getTokenIndex()]; p != nullptr; p = p->parent) {
            above.insert(p);
        }
        ParseTree *lca = file.nodeOf[b->getTokenIndex()];
        while (lca != nullptr && !above.contains(lca)) {
            lca = lca->parent;
        }
        bool nested  = false;
        bool bracket = roundSquare > 0;
        for (ParseTree *p = lca; p != nullptr && p != unit; p = p->parent) {
            if (is<P::StmtContext>(p) || is<P::DeclContext>(p) || is<P::NamespaceItemContext>(p)) {
                nested = true;
                break;
            }
            if (isBracketContext(p) || is<P::SpecTypeContext>(p)) {
                bracket = true;
            }
        }
        if (nested) {
            continue;
        }
        const std::string bText = b->getText();
        if (is<P::StmtListContext>(lca) || is<P::StmtBlockContext>(lca) ||
            is<P::BlockStmtContext>(lca) || is<P::NamespaceDeclContext>(lca) ||
            is<P::ProgramContext>(lca)) {
            continue; // between statements, or around the braces of a block
        }
        if (is<P::CtrlExprContext>(lca) && (aText == "{" || bText == "}" || bText == "case")) {
            continue; // the braces and the cases of a match
        }
        any = true;
        if (bracket) {
            record(file, "R2.cont.bracket", b, "");
            continue;
        }
        auto *lcaRule         = dynamic_cast<ParserRuleContext *>(lca);
        const std::string rule =
            lcaRule != nullptr ? file.ruleNames[lcaRule->getRuleIndex()] : std::string("?");
        // A `>` that closes a list (`with <T: type>` before `func`) is not an operator.
        ParseTree *aParent = file.nodeOf[a->getTokenIndex()]->parent;
        const bool aCloses = aText == ">" && (is<P::AngledParamsContext>(aParent) ||
                                              is<P::AngledValuesContext>(aParent) ||
                                              is<P::SpecTypeContext>(aParent));
        if (!aCloses && kLineEndContinues.contains(aText) &&
            a->getType() != OpenCMLLexer::STRING) {
            record(file, "R2.cont.line_end:" + aText, b, "");
        } else if (kLineStartContinues.contains(bText)) {
            record(file, "R2.cont.line_start:" + bText, b, "");
        } else if (
            rule.size() > 4 && rule.compare(rule.size() - 4, 4, "Expr") == 0 &&
            rule != "ctrlExpr" && rule != "blockExpr") {
            // An operator chain continued by a token that can also start a statement.
            record(file, "R2.split:" + tokenClass(b), b, "in " + rule);
        } else if (rule == "keyUnionDiffType" || rule == "typeExpr" || rule == "arrayType") {
            record(file, "R2.split:" + tokenClass(b), b, "in " + rule);
        } else {
            record(file, "R2.cont.required:" + rule + ":" + tokenClass(b), b, "");
        }
    }
    ++g_counts["R2.statements"];
    if (any) {
        ++g_counts["R2.statements_multiline"];
    }
}

void sameLine(FileStats &file, ParserRuleContext *list) {
    ParserRuleContext *previous = nullptr;
    bool separated               = false;
    for (ParseTree *child : list->children) {
        if (auto *leaf = dynamic_cast<TerminalNode *>(child)) {
            if (leaf->getSymbol()->getType() == OpenCMLLexer::SEP) {
                separated = true;
            }
            continue;
        }
        auto *item = dynamic_cast<ParserRuleContext *>(child);
        if (item == nullptr || item->getStart() == nullptr || item->getStop() == nullptr) {
            continue;
        }
        if (previous != nullptr && !separated &&
            item->getStart()->getLine() == endLine(previous->getStop())) {
            record(file, "R2.same_line_no_sep", item->getStart(), "");
        }
        previous  = item;
        separated = false;
    }
}

void walk(FileStats &file, ParseTree *tree) {
    using P = OpenCMLParser;
    if (auto *rel = dynamic_cast<P::RelationalExprContext *>(tree)) {
        for (size_t i = 1; i + 1 < rel->children.size(); i += 2) {
            auto *leaf = dynamic_cast<TerminalNode *>(rel->children[i]);
            if (leaf == nullptr) {
                continue;
            }
            Token *op              = leaf->getSymbol();
            const std::string text = op->getText();
            if (text == "<") {
                record(
                    file,
                    adjacent(file, op) ? "R1.lt_compare_adjacent" : "R1.lt_compare_spaced",
                    op,
                    "");
            } else if (text == ">") {
                if (atAngleTop(rel)) {
                    record(
                        file,
                        adjacent(file, op) ? "R1.gt_compare_adjacent_in_angle"
                                           : "R1.gt_compare_spaced_in_angle",
                        op,
                        "");
                } else {
                    ++g_counts[adjacent(file, op) ? "R1.gt_compare_adjacent_elsewhere"
                                                  : "R1.gt_compare_spaced_elsewhere"];
                }
            }
        }
    }
    auto closing = [&](ParserRuleContext *list, bool endsInExpression, const char *what) {
        Token *close = list->getStop();
        if (close == nullptr || close->getText() != ">") {
            return;
        }
        if (adjacent(file, close)) {
            ++g_counts[std::string("R1.angle_close_adjacent")];
            return;
        }
        const std::string how = newlineBefore(file, close) ? "line_start" : "space";
        if (endsInExpression) {
            record(file, "R1.angle_close_spaced_expr:" + how, close, what);
        } else {
            ++g_counts["R1.angle_close_spaced_other:" + how];
        }
    };
    if (auto *angled = dynamic_cast<P::AngledValuesContext *>(tree)) {
        Token *open       = angled->getStart();
        const bool onAnno = is<P::FuncAnnoContext>(angled->parent);
        if (!adjacent(file, open)) {
            record(file, onAnno ? "R1.anno_args_spaced" : "R1.angle_args_spaced", open, "");
        } else {
            ++g_counts["R1.angle_args_adjacent"];
        }
        closing(angled, angled->argumentList() != nullptr, "arguments");
    }
    if (auto *params = dynamic_cast<P::AngledParamsContext *>(tree)) {
        bool endsInExpression = false;
        if (params->pairedParams() != nullptr && !params->pairedParams()->keyParamPair().empty()) {
            endsInExpression = params->pairedParams()->keyParamPair().back()->dataExpr() != nullptr;
        }
        closing(params, endsInExpression, "parameters");
    }
    if (auto *spec = dynamic_cast<P::SpecTypeContext *>(tree)) {
        if (!spec->typeOrData().empty()) {
            auto *leaf = dynamic_cast<TerminalNode *>(spec->children[1]);
            if (leaf != nullptr && !adjacent(file, leaf->getSymbol())) {
                record(file, "R1.type_args_spaced", leaf->getSymbol(), "");
            } else {
                ++g_counts["R1.type_args_adjacent"];
            }
            closing(spec, spec->typeOrData().back()->dataExpr() != nullptr, "type arguments");
        }
    }
    if (is<P::StmtContext>(tree) || is<P::DeclContext>(tree) || is<P::NamespaceItemContext>(tree)) {
        classifyBreaks(file, dynamic_cast<ParserRuleContext *>(tree));
    }
    if (is<P::StmtListContext>(tree) || is<P::ProgramContext>(tree) ||
        is<P::NamespaceDeclContext>(tree)) {
        sameLine(file, dynamic_cast<ParserRuleContext *>(tree));
    }
    for (ParseTree *child : tree->children) {
        if (auto *leaf = dynamic_cast<TerminalNode *>(child)) {
            (void)leaf;
            continue;
        }
        walk(file, child);
    }
}

void indexLeaves(FileStats &file, ParseTree *tree) {
    if (auto *leaf = dynamic_cast<TerminalNode *>(tree)) {
        file.nodeOf[leaf->getSymbol()->getTokenIndex()] = leaf;
        return;
    }
    for (ParseTree *child : tree->children) {
        indexLeaves(file, child);
    }
}

} // namespace

int runAntlrStats(const std::vector<std::string> &files) {
    size_t parsed = 0, rejected = 0;
    for (const std::string &path : files) {
        const std::string text = readFile(path);
        antlr4::ANTLRInputStream input(text);
        OpenCMLLexer lexer(&input);
        lexer.removeErrorListeners();
        antlr4::CommonTokenStream stream(&lexer);
        OpenCMLParser parser(&stream);
        parser.removeErrorListeners();
        // As CamelParser does: SLL first, the full LL parse when that gives up.
        auto *interpreter = parser.getInterpreter<antlr4::atn::ParserATNSimulator>();
        ParseTree *tree   = nullptr;
        try {
            interpreter->setPredictionMode(antlr4::atn::PredictionMode::SLL);
            parser.setErrorHandler(std::make_shared<antlr4::BailErrorStrategy>());
            tree = parser.program();
        } catch (antlr4::ParseCancellationException &) {
            parser.reset();
            interpreter->setPredictionMode(antlr4::atn::PredictionMode::LL);
            parser.setErrorHandler(std::make_shared<antlr4::DefaultErrorStrategy>());
            tree = parser.program();
        }
        if (tree == nullptr || parser.getNumberOfSyntaxErrors() > 0 ||
            lexer.getNumberOfSyntaxErrors() > 0) {
            ++rejected;
            continue;
        }
        ++parsed;
        FileStats file;
        file.path      = path;
        file.ruleNames = parser.getRuleNames();
        std::istringstream lines(text);
        for (std::string line; std::getline(lines, line);) {
            file.sourceLines.push_back(line);
        }
        for (Token *token : stream.getTokens()) {
            if (token->getChannel() == Token::DEFAULT_CHANNEL) {
                file.positionOf[token->getTokenIndex()] = file.tokens.size();
                file.tokens.push_back(token);
            }
        }
        indexLeaves(file, tree);
        walk(file, tree);
    }
    std::cout << "FILES parsed=" << parsed << " rejected=" << rejected << "\n\n";
    for (const auto &[kind, lines] : g_examples) {
        std::cout << "== " << kind << " (" << lines.size() << ")\n";
        for (const std::string &line : lines) {
            std::cout << "  " << line << "\n";
        }
    }
    std::cout << "\n== counts\n";
    for (const auto &[kind, count] : g_counts) {
        std::cout << count << "\t" << kind << "\n";
    }
    return 0;
}

} // namespace parsediff
