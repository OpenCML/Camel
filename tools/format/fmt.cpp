// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "fmt.h"
#include <algorithm>
#include <cctype>
#include <regex>

using namespace std;

Formatter::Formatter(const syn::Tree &tree) : Formatter(tree, Options{}) {}

Formatter::Formatter(const syn::Tree &tree, Options options) : tree(tree) {
    // The tokens with their lines; comments are what the formatter puts back between nodes.
    const syn::TokenBuffer &buffer = tree.tokens();
    tokens.reserve(buffer.size());
    for (const syn::Token &token : buffer.tokens()) {
        int channel = 0;
        if (token.kind == TK::Command) {
            channel = 2;
        } else if (token.kind == TK::LineComment || token.kind == TK::DocComment) {
            channel = 3;
        } else if (token.kind == TK::BlockComment) {
            channel = 4;
        }
        tokens.push_back(
            FmtToken{channel, buffer.lineOf(token.begin), std::string(buffer.text(token))});
    }
    writtenComments.assign(tokens.size(), false);
    configure(options);
}

std::string Formatter::format() {
    std::string result = any_cast<string>(visitProgram(cst::Program(tree.root())));
    // No comment of the input may be left unwritten: a comment is source text, and a formatter that
    // loses one rewrites the source silently. A comment that no range and no visitor reached is
    // written at the end of the file, one per line. `unplacedComments` counts them, and the format
    // gate refuses a run where the count is not zero: a comment written here is kept, but it is not
    // kept where it stood.
    for (size_t i = 0; i < tokens.size(); i++) {
        if (tokens[i].channel > 1 && !commentWritten(i)) {
            ++unplacedComments;
            result += lineEnd();
            writeCommentText(result, i);
        }
    }
    if (unplacedComments > 0) {
        result += lineEnd();
    }
    return result;
}

void Formatter::configure(const Options &options) {
    indent  = options.indent.empty() ? "    " : options.indent;
    newline = options.newline.empty() ? "\n" : options.newline;
    quotePrefer =
        options.quotePrefer == "double" ? QuotePreference::Double : QuotePreference::Single;
    indentLevel = 0;
    currentIndent.clear();
}

string Formatter::formatStringLiteral(const string &input, bool multiLine = false) {
    if (multiLine) {
        // A triple-quoted string is raw text and that text is the value: trimming it, putting
        // the delimiters on lines of their own or re-indenting it adds or removes newlines and
        // spaces from what the program means (the format check caught it). Kept as written,
        // like a string that holds both kinds of quotes.
        return input;
    }
    char quoteChar = (quotePrefer == QuotePreference::Single) ? '\'' : '"';
    const string slicedStr = input.substr(1, input.size() - 2);

    string procStr = slicedStr;

    bool hasSingleQuote = (slicedStr.find('\'') != string::npos);
    bool hasDoubleQuote = (slicedStr.find('"') != string::npos);

    if (hasSingleQuote && hasDoubleQuote) {
        // Both kinds of quotes: whichever delimits the string, the other is escaped in it
        // already. Kept as written (escaping again changed the text of the string).
        return input;
    }
    if (hasSingleQuote && !hasDoubleQuote) {
        quoteChar = '"';
    } else if (!hasSingleQuote && hasDoubleQuote) {
        quoteChar = '\'';
    } else {
        // escape all quotes
        string escapedStr;
        for (const char &c : procStr) {
            if (c == quoteChar) {
                escapedStr += '\\';
            }
            escapedStr += c;
        }
        procStr = escapedStr;
    }

    return quoteChar + procStr + quoteChar;
}

inline bool isMultiLine(syn::Node context) {
    if (context->childCount() < 2) {
        return false;
    }
    const size_t firstTokenLine = context->firstToken()->line();
    const syn::Child second     = context->child(1);
    const size_t secondTokenLine =
        second.isNode() ? second.node().firstToken().line() : context->lastToken()->line();
    return firstTokenLine != secondTokenLine;
}

inline bool hasTrailingComma(syn::Node context) {
    if (!context || context->childCount() < 3) {
        return false;
    }
    return context->child(context->childCount() - 2)->text() == ",";
}

void Formatter::insertComment(size_t index, string &result) {
    const FmtToken &comment = tokens[index];
    size_t tokChannel       = comment.channel;
    if (measureOnly || tokChannel == 0 || tokChannel > 4) {
        return;
    }
    if (commentWritten(index)) {
        // Written already: a range that grew over a comment another range had covered writes it
        // once.
        return;
    }
    markCommentWritten(index);
    const auto &commentText = comment.text();
    result += commentText;
    if (tokChannel == 4) {
        // A block comment is followed by a space when the code after it is on the same line: the
        // space separates the two. When the comment ends its line - the usual case for one written
        // on a line of its own, and for a trailing comment - that space was trailing whitespace
        // that every pass then had to strip again.
        if (index + 1 < tokens.size() &&
            tokens[index + 1].line() == comment.line() + countLines(commentText)) {
            result += " ";
        }
    }
};

/*
program : SEP? (decl SEP?)* EOF;
*/
any Formatter::visitProgram(cst::Program context) {
    vector<syn::Node> head, tail;
    vector<pair<syn::Node, size_t>> headWithIndex, tailWithIndex;
    vector<pair<size_t, size_t>> declTokenRanges, headTokenRanges, tailTokenRanges;

    size_t lastIndex              = 0;
    const vector<syn::Node> decls = vec(context->nodes());
    for (size_t i = 0; i < decls.size(); i++) {
        auto child = decls[i];
        if (child.is(NK::ImportDecl)) {
            headWithIndex.push_back({child, i});
        } else {
            tailWithIndex.push_back({child, i});
        }
        declTokenRanges.push_back({lastIndex, child->firstToken()->index()});
        lastIndex = child->lastToken()->index() + 1;
    }
    // here we set the last index to the end - 1 of the token stream
    // to exclude the EOF token
    declTokenRanges.push_back({lastIndex, tokens.size() - 1});

    sort(
        headWithIndex.begin(),
        headWithIndex.end(),
        [](const pair<syn::Node, size_t> &a, const pair<syn::Node, size_t> &b) {
            return a.first->get<cst::ModuleName>()->text() <
                   b.first->get<cst::ModuleName>()->text();
        });

    if (!headWithIndex.empty()) {
        for (const auto &pair : headWithIndex) {
            head.push_back(pair.first);
            // The comments after an import. Those between the last import and the first
            // other declaration belong to that declaration (its documentation) and are
            // written with it; they used to be written twice.
            auto after = declTokenRanges[pair.second + 1];
            if (!tailWithIndex.empty() && pair.second + 1 == tailWithIndex[0].second) {
                after.first = after.second;
            }
            headTokenRanges.push_back(after);
        }
        // The last range is the one before the first printed import. The imports are printed in
        // name order but the comments above them are the comments above the first one in the file:
        // taking the range from the first import of the printed order dropped the comment of the
        // first import whenever the two were not the same declaration.
        size_t firstInFile = headWithIndex[0].second;
        for (const auto &pair : headWithIndex) {
            firstInFile = std::min(firstInFile, pair.second);
        }
        headTokenRanges.push_back(declTokenRanges[firstInFile]);
    }
    if (!tailWithIndex.empty()) {
        for (const auto &pair : tailWithIndex) {
            tail.push_back(pair.first);
            tailTokenRanges.push_back(declTokenRanges[pair.second + 1]);
        }
        tailTokenRanges.push_back(declTokenRanges[tailWithIndex[0].second]);
    }

    string headStr, tailStr;
    // The comments above the first declaration (the description of the file) stay above it; they
    // used to be dropped. A blank line between two of them is kept: it is what separates the
    // licence header from the file's own description, and without it the two runs of comments
    // become one, which changes how the parser reads the file.
    {
        const size_t limit = decls.empty() ? tokens.size() - 1 : decls.front().firstToken().index();
        size_t previousEnd = 0;
        for (size_t i = 0; i < limit; i++) {
            if (tokens[i].channel > 1) {
                const size_t line = tokens[i].line();
                if (previousEnd != 0 && line > previousEnd + 1) {
                    headStr += lineEnd();
                }
                headStr += tokens[i].text() + lineEnd();
                // Written here, and marked as written: the node below the comments asks which of
                // them are still unwritten, and a comment it reads again is written twice.
                markCommentWritten(i);
                previousEnd = line + std::count(tokens[i].text().begin(), tokens[i].text().end(), '\n');
            }
        }
        // The blank line the file leaves between its comments and the first declaration is kept: a
        // doc comment standing against a declaration belongs to it, and one separated from it does
        // not, so writing them together would change the tree.
        if (previousEnd != 0 && limit < tokens.size() && tokens[limit].line() > previousEnd + 1) {
            headStr += lineEnd();
        }
    }
    // pass nullptr to avoid auto comment processing for empty head
    headStr += formatList(
        head,
        syn::Node(),
        "; ",
        (this->preferSemis ? ";" : ""),
        TrailingC | Multiline | PaddingNL | PRightOnly,
        1,
        headTokenRanges);
    // A file of imports only: the comments have been written with the imports. An empty list
    // with a context writes every comment of the context, which wrote them a second time.
    tailStr = formatList(
        tail,
        tail.empty() && !head.empty() ? syn::Node() : syn::Node(context),
        "; ",
        (this->preferSemis ? ";" : ""),
        TrailingC | Multiline | PaddingNL | PRightOnly,
        2,
        tailTokenRanges);
    string result = headStr + (head.empty() || tail.empty() ? "" : lineEnd()) + tailStr;

    // remove tailing spaces in empty lines
    std::regex emptyLineRegex("^\\s*$");
    result = std::regex_replace(result, emptyLineRegex, "");

    return result;
}

/*
stmtList : stmt (SEP? stmt)* SEP? ;
*/
any Formatter::visitStmtList(cst::StmtList context) {
    return formatList(vec(context->nodes()), context, "; ", "", PaddingNL | PushScope | Multiline);
}

/*
moduleName : ('.' | '..' | '...')? IDENTIFIER ('.' IDENTIFIER)* ;
*/
any Formatter::visitModuleName(cst::ModuleName context) { return context->text(); }

/*
importDecl : IMPORT (moduleName | (identDef | bracedIdents) FROM moduleName) ;
*/
any Formatter::visitImportDecl(cst::ImportDecl context) {
    string result        = "import ";
    const string path    = any_cast<string>(visitModuleName(context->get<cst::ModuleName>()));
    const auto &identDef = context->get<cst::IdentDef>();
    const auto braced    = context->get<cst::BracedImports>();
    const auto single    = context->get<cst::ImportItem>();
    if (context->tok(TK::KwFrom)) {
        if (single) {
            // `import a from m`, `import a as b from m`: one name.
            result += any_cast<string>(visit(single));
        } else if (braced->all<cst::ImportItem>().empty()) {
            result += "{}";
        } else {
            result += "{" +
                      formatList(
                          vec(braced->all<cst::ImportItem>()),
                          braced,
                          ", ",
                          ",",
                          PaddingSP | PaddingNL | PushScope) +
                      "}";
        }
        result += " from " + path;
    } else {
        result += path;
        if (identDef) {
            // `import m as x`: the module under another name.
            result += " as " + any_cast<string>(visitIdentDef(identDef));
        }
    }
    return result;
}

/*
exportDecl : EXPORT (dataDecl | typeDecl | bracedIdents) ;
*/
any Formatter::visitExportDecl(cst::ExportDecl context) {
    string result            = "export ";
    const auto &dataDecl     = context->get<cst::DataDecl>();
    const auto &typeDecl     = context->get<cst::TypeDecl>();
    const auto &bracedIdents = context->get<cst::BracedIdents>();
    if (dataDecl) {
        return result + any_cast<string>(visitDataDecl(dataDecl));
    } else if (typeDecl) {
        return result + any_cast<string>(visitTypeDecl(typeDecl));
    } else {
        return result + any_cast<string>(visitBracedIdents(bracedIdents));
    }
}

/*
usingNamespaceDecl : USING NAMESPACE identRef ;
*/
any Formatter::visitUsingNamespaceDecl(cst::UsingNamespaceDecl context) {
    return "using namespace " + any_cast<string>(visitIdentRef(context->get<cst::IdentRef>()));
}

/*
blockStmt  : stmtBlock | funcAnno+ stmtBlock ;
*/
any Formatter::visitBlockStmt(cst::BlockStmt context) {
    string result;
    // The tags written before the block (`#tag { ... }`) and `@numeric(...)`: each on a line of
    // its own, at the start of a line (design principles 4.1, 6.2).
    for (const auto &tagUse : context->all<cst::TagUse>()) {
        // The comments written between the tag and the declaration it belongs to are written here,
        // where the reader put them (commentsInFrontMatter).
        result += any_cast<string>(visitTagUse(tagUse)) +
                  commentsInFrontMatter(tagUse->lastToken()->index());
    }
    for (const auto &funcAnno : context->all<cst::FuncAnno>()) {
        result += any_cast<string>(visitFuncAnno(funcAnno)) +
                  commentsInFrontMatter(funcAnno->lastToken()->index());
    }
    result += any_cast<string>(visitStmtBlock(context->get<cst::StmtBlock>()));
    return result;
}

/*
stmtBlock  : SYNC? '{' stmtList? '}' ;
*/
any Formatter::visitStmtBlock(cst::StmtBlock context) {
    const auto &stmtList = context->get<cst::StmtList>();
    string result        = context->tok(TK::KwSync) ? "sync " : "";
    if (stmtList) {
        // The comments between the last statement of the block and its closing brace belong to the
        // block: the statement list writes comments up to its last statement and no further, and
        // they used to be dropped - `// mark:` after the last statement of a body, say. They are
        // written where they stood: on the statement's line if they were on it, which is where
        // every statement but the last one keeps such a comment. The brace keeps its own line.
        const size_t lastStmt = stmtList->lastToken()->index();
        const size_t brace    = context->lastToken()->index();
        std::string inner     = any_cast<string>(visitStmtList(stmtList));
        std::string comments;
        pushIndent();
        size_t lastStopLine =
            tokens[lastStmt].line() + countLines(tokens[lastStmt].text());
        for (size_t i = lastStmt + 1; i < brace; i++) {
            if (tokens[i].channel <= 1 || commentWritten(i)) {
                continue;
            }
            const long skips = static_cast<long>(tokens[i].line()) - static_cast<long>(lastStopLine);
            comments += skips <= 0 ? " " : lineEnd();
            writeCommentText(comments, i);
            lastStopLine = tokens[i].line() + countLines(tokens[i].text());
        }
        popIndent();
        if (comments.empty()) {
            return result + "{" + inner + "}";
        }
        const std::string listEnd = lineEnd();
        if (inner.size() >= listEnd.size() &&
            inner.compare(inner.size() - listEnd.size(), listEnd.size(), listEnd) == 0) {
            inner.resize(inner.size() - listEnd.size());
        }
        return result + "{" + inner + comments + listEnd + "}";
    } else {
        result += "{";
        const size_t firstTokIdx = context->firstToken()->index();
        const size_t lastTokIdx  = context->lastToken()->index();
        bool foundComment        = false;
        pushIndent();
        for (size_t i = firstTokIdx + 1; i < lastTokIdx; i++) {
            if (tokens[i].channel > 1) {
                result += lineEnd();
                insertComment(i, result);
                foundComment = true;
            }
        }
        popIndent();
        if (foundComment) {
            result += lineEnd();
        }
        return result + "}";
    }
}

/*
blockExpr  : stmtBlock | dataExpr ;
*/
any Formatter::visitBlockExpr(syn::Node context) { return visit(context); }

/*
funcData   : modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr ;
*/
any Formatter::visitFuncData(cst::FuncData context) {
    string result;
    const auto &modifiers    = context->get<cst::Modifiers>();
    const auto &angledParams = context->get<cst::AngledParams>();
    const auto &parentParams = context->get<cst::ParentParams>();
    const auto &blockExpr    = context->lastChild().node();

    if (modifiers) {
        result += any_cast<string>(visitModifiers(modifiers)) + " ";
    }

    if (angledParams) {
        result += any_cast<string>(visitAngledParams(angledParams));
    }
    result += any_cast<string>(visitParentParams(parentParams));

    const auto &typeExpr = context->type();
    if (typeExpr) {
        result += ": " + any_cast<string>(visitType(typeExpr));
    }

    result += " => ";

    return result + any_cast<string>(visitBlockExpr(blockExpr));
}

/*
funcAnno   : '@' identRef (angledValues | parentIdents)? ;
*/
any Formatter::visitFuncAnno(cst::FuncAnno context) {
    string result = "@";
    result += any_cast<string>(visitIdentRef(context->get<cst::IdentRef>()));
    if (context->get<cst::AngledValues>()) {
        result += any_cast<string>(visitAngledValues(context->get<cst::AngledValues>()));
    } else if (context->get<cst::ParentIdents>()) {
        result += any_cast<string>(visitParentIdents(context->get<cst::ParentIdents>()));
    }
    return result;
}

/*
tagDecl    : 'export'? 'tag' identDef typeParams? parentParams? 'on' tagPositions
                ('read_by' tagReaders)? whenClause?
           | 'export'? 'tag' identDef '=' bracketIdents
*/
any Formatter::visitTagDecl(cst::TagDecl context) {
    string result = context->tok(TK::KwExport) ? "export tag " : "tag ";
    // The head is measured once and written once, as a function's is; `on` and `read_by` belong to the signature and
    // the `when` clause is the one that moves.
    const auto buildHead = [&](bool splitParams) {
        string head;
        head += any_cast<string>(visitIdentDef(context->get<cst::IdentDef>()));
        if (context->get<cst::TypeParams>()) {
            head += any_cast<string>(visitTypeParams(context->get<cst::TypeParams>()));
        }
        paramsOnePerLine = splitParams;
        if (context->get<cst::ParentParams>()) {
            head += any_cast<string>(visitParentParams(context->get<cst::ParentParams>()));
        }
        paramsOnePerLine = false;
        if (const auto group = context->get<cst::BracketIdents>()) {
            return std::make_pair(head + " = " + any_cast<string>(visitBracketIdents(group)),
                                  std::vector<string>{});
        }
        head += " on " + any_cast<string>(visitTagPositions(context->get<cst::TagPositions>()));
        if (context->get<cst::TagReaders>()) {
            head += " read_by " + any_cast<string>(visitTagReaders(context->get<cst::TagReaders>()));
        }
        std::vector<string> clauses;
        if (context->get<cst::WhenClause>()) {
            clauses.push_back(any_cast<string>(visitWhenClause(context->get<cst::WhenClause>())));
        }
        return std::make_pair(head, clauses);
    };
    measureOnly = true;
    const auto measured = buildHead(false);
    measureOnly = false;
    const auto head = buildHead(headExceedsWidth(measured.first.size()));
    return result + joinDeclClauses(head.first, head.second);
}

/*
tagPositions : tagPositionName (',' tagPositionName)* ','?
*/
any Formatter::visitTagPositions(cst::TagPositions context) {
    string result;
    for (const auto position : context->all<cst::IdentDef>()) {
        if (!result.empty()) {
            result += ", ";
        }
        result += any_cast<string>(visitIdentDef(position));
    }
    return result;
}

/*
tagReaders : identRef (',' identRef)* ','?
*/
any Formatter::visitTagReaders(cst::TagReaders context) {
    string result;
    for (const auto reader : context->all<cst::IdentRef>()) {
        if (!result.empty()) {
            result += ", ";
        }
        result += any_cast<string>(visitIdentRef(reader));
    }
    return result;
}

/*
tagUse     : '#' (tagRef | '[' (tagRef (',' tagRef)* ','?)? ']')
*/
any Formatter::visitTagUse(cst::TagUse context) {
    const auto refs = context->all<cst::TagRef>();
    if (refs.size() == 1 && !context->tok(TK::LBracket)) {
        return "#" + any_cast<string>(visitTagRef(refs.front()));
    }
    string result = "#[";
    for (size_t i = 0; i < refs.size(); ++i) {
        result += (i > 0 ? ", " : "") + any_cast<string>(visitTagRef(refs[i]));
    }
    return result + "]";
}

/*
tagRef     : identRef parentArgues?
*/
any Formatter::visitTagRef(cst::TagRef context) {
    string result = any_cast<string>(visitIdentRef(context->get<cst::IdentRef>()));
    if (const auto arguments = context->get<cst::ParentArgues>()) {
        result += any_cast<string>(visitParentArgues(arguments));
    }
    return result;
}

/// The head of a declaration: the signature, then its clauses. A head wider than `wrapWidth`
/// puts every clause on a line of its own, indented one level, and leaves the opening brace of
/// the body at the end of the last clause's line; a head that fits keeps them on one line, one
/// space after the signature.
std::string Formatter::joinDeclClauses(
    const std::string &signature, const std::vector<std::string> &clauses) {
    if (clauses.empty()) {
        return signature;
    }
    size_t width = signature.size();
    for (const auto &clause : clauses) {
        width += 1 + clause.size();
    }
    std::string result = signature;
    if (!headExceedsWidth(width)) {
        for (const auto &clause : clauses) {
            result += " " + clause;
        }
        return result;
    }
    pushIndent();
    for (const auto &clause : clauses) {
        result += lineEnd() + clause;
    }
    popIndent();
    return result;
}

/*
funcDecl   :
        funcAnno*
        (WITH angledParams)?
        EXPORT? modifiers?

FUNC identRef parentParams (':' typeExpr)? stmtBlock? ;
*/
any Formatter::visitFuncDecl(cst::FuncDecl context) {
    string result;
    const auto funcAnnos     = context->all<cst::FuncAnno>();
    const auto &angledParams = context->get<cst::AngledParams>();
    const auto &modifiers    = context->get<cst::Modifiers>();
    const auto &identRef     = context->get<cst::IdentRef>();
    const auto &parentParams = context->get<cst::ParentParams>();
    const auto &typeExpr     = context->type();
    const auto &stmtBlock    = context->get<cst::StmtBlock>();

    for (const auto &tagUse : context->all<cst::TagUse>()) {
        // The comments written between the tag and the declaration it belongs to are written here,
        // where the reader put them (commentsInFrontMatter).
        result += any_cast<string>(visitTagUse(tagUse)) +
                  commentsInFrontMatter(tagUse->lastToken()->index());
    }

    for (const auto funcAnno : funcAnnos) {
        result += any_cast<string>(visitFuncAnno(funcAnno)) +
                  commentsInFrontMatter(funcAnno->lastToken()->index());
    }

    if (angledParams) {
        result += "with " + any_cast<string>(visitAngledParams(angledParams)) +
                  commentsInFrontMatter(angledParams->lastToken()->index());
    }

    if (context->tok(TK::KwExport)) {
        result += "export ";
    }

    if (modifiers) {
        result += any_cast<string>(visitModifiers(modifiers)) + " ";
    }

    // The head of the declaration - `func name::<T>(params): Ret` and its clauses - is built
    // twice: once to measure it, with no comment written or marked, and once for real, once the
    // two wrap decisions are fixed (see joinDeclClauses and paramsOnePerLine). The comments of
    // the source stay in the token stream for the real visit.
    const auto buildHead = [&](bool splitParams) {
        string prefix;
        // An operator overload is written `func operator +(...)`: the word, one space, the symbol
        // and the parameters right after it (design principles 4.3; the notation is provisional).
        if (const auto operatorName = context->get<cst::OperatorName>()) {
            prefix +=
                "func operator " + operatorName->text().substr(std::string_view("operator").size());
        } else {
            prefix += "func " + any_cast<string>(visitIdentRef(identRef));
        }
        if (const auto typeParams = context->get<cst::TypeParams>()) {
            prefix += any_cast<string>(visitTypeParams(typeParams));
        }
        paramsOnePerLine = splitParams;
        string signature = prefix + any_cast<string>(visitParentParams(parentParams));
        paramsOnePerLine = false;
        if (typeExpr) {
            signature += ": " + any_cast<string>(visitType(typeExpr));
        }
        std::vector<string> clauses;
        if (const auto clause = context->get<cst::ResourceClause>()) {
            // `uses s, fs` / `pure` after the result type (design effects plan 3.1), treated as a
            // `when` clause is by the wrap rule.
            clauses.push_back(any_cast<string>(visitResourceClause(clause)));
        }
        if (const auto whenClause = context->get<cst::WhenClause>()) {
            // The declaration's applicability condition, after the signature and before the body
            // (design principles 4.4): it is part of the declaration, so leaving it out changes
            // the AST the text parses to and, with it, the names synthesized from positions.
            clauses.push_back(any_cast<string>(visitWhenClause(whenClause)));
        }
        return std::make_pair(signature, clauses);
    };

    measureOnly = true;
    const auto measured = buildHead(false);
    measureOnly = false;
    // Still too wide once the clauses have moved to their own lines: the parameters go one per
    // line as well. A head that fits is under the width already, so this cannot fire for one.
    const bool splitParams = headExceedsWidth(measured.first.size());
    const auto head = buildHead(splitParams);
    result += joinDeclClauses(head.first, head.second);

    if (!stmtBlock) {
        // A body-less declaration of a C++ operator: the signature is the whole of it (design
        // principles 6.4).
        return result;
    }
    return result + " " + any_cast<string>(visitStmtBlock(stmtBlock));
}

/*
whenClause : IDENTIFIER logicalOrExpr
*/
any Formatter::visitWhenClause(cst::WhenClause context) {
    // The applicability condition of a declaration (design principles 4.4): `when` and the
    // expression, as written, after the signature and before the body.
    return "when " + any_cast<string>(visitExpr(context->expr()));
}

/*
ruleDecl : ('with' angledParams)? EXPORT? IDENTIFIER identDef ruleContext? parentParams whenClause? '{' ruleBody '}' ;
*/
any Formatter::visitRuleDecl(cst::RuleDecl context) {
    string result;
    // The tags written before the rule (`#requires(...)`, design principles 6.2, 7.2): each on a
    // line of its own, as before a function or a type declaration.
    for (const auto &tagUse : context->all<cst::TagUse>()) {
        // The comments written between the tag and the declaration it belongs to are written here,
        // where the reader put them (commentsInFrontMatter).
        result += any_cast<string>(visitTagUse(tagUse)) +
                  commentsInFrontMatter(tagUse->lastToken()->index());
    }
    // The `with` parameters come first, before `export`, as they do in a function declaration
    // (design principles 4.4, 5.5): they are the rule's own context parameters.
    if (const auto angledParams = context->get<cst::AngledParams>()) {
        result += "with " + any_cast<string>(visitAngledParams(angledParams)) +
                  commentsInFrontMatter(angledParams->lastToken()->index());
    }
    result += context->has(TK::KwExport) ? "export rule " : "rule ";
    // The head - `rule name::<T>(params) when cond` - is measured once and written once, as a
    // function's is. The rule body
    // is one line, so the opening brace stays at the end of the `when` line.
    const auto buildHead = [&](bool splitParams) {
        string head;
        head += any_cast<string>(visitIdentDef(context->get<cst::IdentDef>()));
        if (const auto typeParams = context->get<cst::TypeParams>()) {
            head += any_cast<string>(visitTypeParams(typeParams));
        }
        paramsOnePerLine = splitParams;
        head += any_cast<string>(visitParentParams(context->get<cst::ParentParams>()));
        paramsOnePerLine = false;
        std::vector<string> clauses;
        // The rule's condition is part of its signature, so it is written between the parameters
        // and the body, as a function writes it (design principles 4.4).
        if (const auto whenClause = context->get<cst::WhenClause>()) {
            clauses.push_back(any_cast<string>(visitWhenClause(whenClause)));
        }
        return std::make_pair(head, clauses);
    };
    measureOnly = true;
    const auto measured = buildHead(false);
    measureOnly = false;
    const auto head = buildHead(headExceedsWidth(measured.first.size()));
    result += joinDeclClauses(head.first, head.second);
    result += " { " + any_cast<string>(visitRuleBody(context->get<cst::RuleBody>())) + " }";
    return result;
}

/*
factDecl : ('with' angledParams)? 'export'? 'fact' identDef typeParams? parentParams? ':' typeExpr
           factBody factClause*
*/
any Formatter::visitFactDecl(cst::FactDecl context) {
    string result;
    for (const auto &tagUse : context->all<cst::TagUse>()) {
        // The comments written between the tag and the declaration it belongs to are written here,
        // where the reader put them (commentsInFrontMatter).
        result += any_cast<string>(visitTagUse(tagUse)) +
                  commentsInFrontMatter(tagUse->lastToken()->index());
    }
    // The arguments the fact is queried with come first, as they do for a function
    // (visitFuncDecl), and before `export`, which belongs to the declaration's name.
    if (const auto angledParams = context->get<cst::AngledParams>()) {
        result += "with " + any_cast<string>(visitAngledParams(angledParams)) +
                  commentsInFrontMatter(angledParams->lastToken()->index());
    }
    result += context->tok(TK::KwExport) ? "export fact " : "fact ";
    // The head is `fact name(params): Type`; a fact has no `when`/`uses`/`pure` clause, so the
    // half of the wrap rule that applies to it is the parameters going one per line. It is measured once and written once, as a function's is.
    const auto buildHead = [&](bool splitParams) {
        string head = any_cast<string>(visitIdentDef(context->get<cst::IdentDef>()));
        if (const auto typeParams = context->get<cst::TypeParams>()) {
            head += any_cast<string>(visitTypeParams(typeParams));
        }
        paramsOnePerLine = splitParams;
        if (const auto parentParams = context->get<cst::ParentParams>()) {
            head += any_cast<string>(visitParentParams(parentParams));
        }
        paramsOnePerLine = false;
        return head + ": " + any_cast<string>(visitType(context->type()));
    };
    measureOnly = true;
    const string measured = buildHead(false);
    measureOnly = false;
    result += buildHead(headExceedsWidth(measured.size()));
    result += " " + any_cast<string>(visitFactBody(context->get<cst::FactBody>()));
    // The clauses that say how two facts are merged, one per line, as written.
    for (const auto clause : context->all<cst::FactClause>()) {
        result += lineEnd() + any_cast<string>(visitFactClause(clause));
    }
    return result;
}

/*
factBody : 'match' logicalOrExpr '{' factCase* '}'
*/
any Formatter::visitFactBody(cst::FactBody context) {
    return "match " + any_cast<string>(visitExpr(context->expr())) + " {" +
           formatList(
               vec(context->all<cst::FactCase>()),
               context,
               " ",
               "",
               PaddingNL | Multiline | PushScope) +
           "}";
}

/*
factCase : 'case' factPattern '=>' dataExpr
*/
any Formatter::visitFactCase(cst::FactCase context) {
    return "case " + any_cast<string>(visitFactPattern(context->get<cst::FactPattern>())) + " => " +
           any_cast<string>(visitExpr(context->expr()));
}

/*
factPattern : 'const' identDef (':' typeExpr)? | dataExpr
*/
any Formatter::visitFactPattern(cst::FactPattern context) {
    if (const auto name = context->get<cst::IdentDef>()) {
        string result = "const " + any_cast<string>(visitIdentDef(name));
        if (const auto type = context->type()) {
            result += ": " + any_cast<string>(visitType(type));
        }
        return result;
    }
    // `_` is no expression: it is written as it stands.
    if (context->has(TK::Underscore)) {
        return string("_");
    }
    return any_cast<string>(visitExpr(context->expr()));
}

/*
factClause : IDENTIFIER dataExpr
*/
any Formatter::visitFactClause(cst::FactClause context) {
    return string(context->tok(TK::Identifier).text()) + " " +
           any_cast<string>(visitExpr(context->expr()));
}

/*
ruleBody : dataExpr ('=>' | '<=>') dataExpr ;
*/
any Formatter::visitRuleBody(cst::RuleBody context) {
    const auto sides = context->exprs();
    return any_cast<string>(visitExpr(sides[0])) + (context->has(TK::Equiv) ? " <=> " : " => ") +
           any_cast<string>(visitExpr(sides[1]));
}

/*
namespaceDecl : NAMESPACE identRef '{' SEP? (namespaceItem SEP?)* '}' ;
*/
any Formatter::visitNamespaceDecl(cst::NamespaceDecl context) {
    string result =
        "namespace " + any_cast<string>(visitIdentRef(context->get<cst::IdentRef>())) + " {";
    result += formatList(itemsOf(context), context, "; ", "", PaddingNL | PushScope | Multiline);
    return result + "}";
}

/*
parentIdents  : '(' identList? ','? ')' ;
*/
any Formatter::visitParentIdents(cst::ParentIdents context) {
    const auto &identList = context->get<cst::IdentList>();
    return "(" + (identList ? any_cast<string>(visitIdentList(identList)) : "") + ")";
}

/*
bracedIdents  : '{' identList? ','? '}' ;
*/
any Formatter::visitBracedIdents(cst::BracedIdents context) {
    const auto &identList = context->get<cst::IdentList>();
    if (identList) {
        return "{" +
               formatList(
                   vec(identList->all<cst::IdentDef>()),
                   identList,
                   ", ",
                   ",",
                   PaddingSP | PaddingNL | PushScope) +
               "}";
    } else {
        return string("{}");
    }
}

/*
bracketIdents : '[' identList? ','? ']' ;
*/
any Formatter::visitBracketIdents(cst::BracketIdents context) {
    return "[" +
           (context->get<cst::IdentList>()
                ? any_cast<string>(visitIdentList(context->get<cst::IdentList>()))
                : "") +
           "]";
}

/*
dataDecl   : (LET | VAR) carrier (':' typeList)? '=' dataList ;
*/
any Formatter::visitDataDecl(cst::DataDecl context) {
    string result;
    const string &letOrVar  = context->child(0)->text();
    const syn::Node carrier = context->child(1).node();
    const auto &typeList    = context->get<cst::TypeList>();
    const auto &dataList    = context->get<cst::DataList>();
    result += letOrVar + " " + any_cast<string>(visit(carrier));
    if (typeList) {
        result += ": " + any_cast<string>(visitTypeList(typeList));
    }
    if (dataList) {
        result += " = " + any_cast<string>(visitDataList(dataList));
    }
    return result;
}

/*
resourceDecl : 'export'? 'resource' identDef ('=' bracketIdents | 'unordered')?
*/
any Formatter::visitResourceDecl(cst::ResourceDecl context) {
    string result = context->tok(TK::KwExport) ? "export " : "";
    result += "resource " + any_cast<string>(visitIdentDef(context->get<cst::IdentDef>()));
    if (const auto members = context->get<cst::BracketIdents>()) {
        result += " = " + any_cast<string>(visitBracketIdents(members));
    } else if (context->lastToken() && context->lastToken()->text() == "unordered") {
        // The contextual `unordered` (design effects plan 3.1): any order of the accesses is
        // allowed, so the resource has no order to keep.
        result += " unordered";
    }
    return result;
}

/*
resourceClause : ('uses' identRef (',' identRef)* | 'pure')+
*/
any Formatter::visitResourceClause(cst::ResourceClause context) {
    // `pure` has no name to print; `uses` prints the names as written. A clause that says both
    // is refused by the semantic check (PureAndUses), and formatting keeps what was written.
    const auto refs = context->all<cst::IdentRef>();
    const bool pure = context->firstToken() && context->firstToken()->text() == "pure";
    string result   = pure ? "pure" : "";
    if (!refs.empty()) {
        if (!result.empty()) {
            result += " ";
        }
        result += "uses ";
        for (size_t i = 0; i < refs.size(); ++i) {
            result += (i > 0 ? ", " : "") + any_cast<string>(visitIdentRef(refs[i]));
        }
    }
    return result;
}

/*
typeDecl
    : TYPE identRef ('<' identDef (',' identDef)* ','? ('>' | '>='))?
      ('=' (typeExpr | STRING))?
    ;
*/
any Formatter::visitTypeDecl(cst::TypeDecl context) {
    string result;
    for (const auto &tagUse : context->all<cst::TagUse>()) {
        // The comments written between the tag and the declaration it belongs to are written here,
        // where the reader put them (commentsInFrontMatter).
        result += any_cast<string>(visitTagUse(tagUse)) +
                  commentsInFrontMatter(tagUse->lastToken()->index());
    }
    result += "type " + any_cast<string>(visitIdentRef(context->get<cst::IdentRef>()));
    if (const auto params = context->all<cst::IdentDef>(); !params.empty()) {
        result += "<";
        for (size_t i = 0; i < params.size(); ++i) {
            result += (i > 0 ? ", " : "") + any_cast<string>(visitIdentDef(params[i]));
        }
        result += ">";
    }
    // An opaque type has no right side: `export type Graph`.
    if (context->type()) {
        result += " = " + any_cast<string>(visitType(context->type()));
    } else if (context->tokCount(TK::String) > 0) {
        result += " = " + formatStringLiteral(context->tok(TK::String)->text());
    }
    return result;
}

/*
retStmt    : (RETURN | YIELD) dataList ;
*/
any Formatter::visitRetStmt(cst::RetStmt context) {
    return context->child(0)->text() + " " +
           any_cast<string>(visitDataList(context->get<cst::DataList>()));
}

/*
modifiers   : SYNC+ ;
*/
any Formatter::visitModifiers(cst::Modifiers context) {
    string result;
    context->tokCount(TK::KwSync) ? result += "sync " : result;
    // remove trailing space
    if (!result.empty()) {
        result.pop_back();
    }
    return result;
}

/*
indexValue   : '..' dataExpr ;
The rest argument (`..(a, b)`, `..ts`), expanded at the call.
*/
any Formatter::visitRestValue(cst::RestValue context) {
    return ".." + any_cast<string>(visitExpr(context->expr()));
}

/*
keyTypePair  : identDef ':' typeExpr ;
*/
any Formatter::visitKeyTypePair(cst::KeyTypePair context) {
    string result;
    for (const auto &tagUse : context->all<cst::TagUse>()) {
        result += any_cast<string>(visitTagUse(tagUse)) + " ";
    }
    return result +
           any_cast<string>(visitIdentDef(context->get<cst::IdentDef>())) + ": " +
           any_cast<string>(visitType(context->type()));
}

/*
keyValuePair : identDef ':' dataExpr | '...' dataExpr ;
*/
any Formatter::visitKeyValuePair(cst::KeyValuePair context) {
    if (context->get<cst::IdentDef>()) {
        return any_cast<string>(visitIdentDef(context->get<cst::IdentDef>())) + ": " +
               any_cast<string>(visitExpr(context->expr()));
    } else {
        return "..." + any_cast<string>(visitExpr(context->expr()));
    }
}

/*
keyParamPair : ('var' | 'const' | 'constas')? identDef (':' typeExpr ('=' dataExpr)?)?
             | '..' identDef (':' typeExpr)? ;
*/
any Formatter::visitKeyParamPair(cst::KeyParamPair context) {
    string result = context->tok(TK::DotDot)      ? ".."
                    : context->tok(TK::KwConstas) ? "constas "
                    : context->tok(TK::KwVar)     ? "var "
                    : context->tok(TK::KwConst)   ? "const "
                                                  : "";
    result += any_cast<string>(visitIdentDef(context->get<cst::IdentDef>()));
    const auto &typeExpr = context->type();
    const auto &dataExpr = context->expr();
    // A lambda's parameter may leave its type out (design principles 5.9).
    if (typeExpr) {
        result += ": " + any_cast<string>(visitType(typeExpr));
        if (dataExpr) {
            result += " = " + any_cast<string>(visitExpr(dataExpr));
        }
    }
    return result;
}

/*
dataList     : dataExpr (',' dataExpr)* ;
*/
any Formatter::visitDataList(cst::DataList context) {
    return formatList(vec(context->exprs()), context, ", ", ",", PaddingNL | PushScope);
}

/*
identList    : identDef (',' identDef)* ;
*/
any Formatter::visitIdentList(cst::IdentList context) {
    return formatList(
        vec(context->all<cst::IdentDef>()),
        context,
        ", ",
        ",",
        PaddingNL | PushScope);
}

/*
indexValues  : indexValue (',' indexValue)* ;
*/
any Formatter::visitIndexValues(cst::IndexValues context) {
    return formatList(vec(context->exprs()), context, ", ", ",", PaddingNL | PushScope);
}

/*
pairedValues : keyValuePair (',' keyValuePair)* ;
*/
any Formatter::visitPairedValues(cst::PairedValues context) {
    return formatList(
        vec(context->all<cst::KeyValuePair>()),
        context,
        ", ",
        ",",
        PaddingNL | PushScope);
}

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
any Formatter::visitTupleParam(cst::TupleParam context) {
    string result = "(";
    bool first    = true;
    for (const auto &identDef : context->all<cst::IdentDef>()) {
        if (!first) {
            result += ", ";
        }
        first   = false;
        result += any_cast<string>(visitIdentDef(identDef));
    }
    return result + ")";
}

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
any Formatter::visitPairedParams(cst::PairedParams context) {
    // Consumed on entry: only the parameter list the declaration set the flag for splits, not the
    // parameter lists nested inside it (a function type's own parameters, say).
    const bool onePerLine = std::exchange(paramsOnePerLine, false);
    std::vector<syn::Node> params;
    for (const syn::Node child : context->nodes()) {
        if (child.is(NK::KeyParamPair) || child.is(NK::TupleParam)) {
            params.push_back(child);
        }
    }
    // A declaration head too wide to fit writes its parameters one per line; the declaration sets the flag before it builds its
    // head, and it is false for every other parameter list (a lambda's, a call's).
    const int flags = PaddingNL | PushScope | (onePerLine ? Multiline : 0);
    return formatList(params, context, ", ", ",", flags);
}

/*
argumentList : indexValues (',' pairedValues)? | pairedValues ;
*/
any Formatter::visitArgumentList(cst::ArgumentList context) {
    const auto &indexValues  = context->get<cst::IndexValues>();
    const auto &pairedValues = context->get<cst::PairedValues>();
    string result;
    if (indexValues) {
        result += any_cast<string>(visitIndexValues(indexValues));
    }
    if (pairedValues) {
        if (!result.empty()) {
            result += indexValues->lastToken()->line() < pairedValues->firstToken()->line()
                          ? "," + lineEnd()
                          : ", ";
        }
        result += any_cast<string>(visitPairedValues(pairedValues));
    }
    return result;
}

/*
indices : '[' (sliceIndices | dataExpr) (',' (sliceIndices | dataExpr))* ']' ;
*/
any Formatter::visitIndices(cst::Indices context) {
    string result = "[";
    bool first    = true;
    for (const syn::Node entry : context->nodes()) {
        if (!first) {
            result += ", ";
        }
        first = false;
        if (entry.is(NK::SliceIndices)) {
            result += any_cast<string>(visitSliceIndices(cst::SliceIndices(entry)));
        } else {
            result += any_cast<string>(visitExpr(entry));
        }
    }
    return result + "]";
}

/*
sliceIndices : dataExpr? ':' dataExpr? (':' dataExpr?)?, a part not written left out ;
*/
any Formatter::visitSliceIndices(cst::SliceIndices context) {
    // The tree keeps the colons it consumed, so the text is the parts and the colons in the
    // order they were written. Nothing separates a colon from the part that follows it: a
    // slice is written `xs[a:b:s]`, `xs[:n]`, `xs[n:]` and `xs[::-1]`.
    string result;
    for (size_t i = 0; i < context->childCount(); ++i) {
        const auto child = context->child(i);
        if (child.isToken()) {
            result += child.token().kind() == TK::ColonColon ? "::" : ":";
            continue;
        }
        result += any_cast<string>(visitExpr(child.node()));
    }
    return result;
}

/*
parentParams : '(' pairedParams? ','? ')' ;
*/
any Formatter::visitParentParams(cst::ParentParams context) {
    return "(" +
           (context->get<cst::PairedParams>()
                ? any_cast<string>(visitPairedParams(context->get<cst::PairedParams>()))
                : "") +
           ")";
}

/*
parentArgues : '(' argumentList? ','? ')' ;
*/
any Formatter::visitParentArgues(cst::ParentArgues context) {
    return "(" +
           (context->get<cst::ArgumentList>()
                ? any_cast<string>(visitArgumentList(context->get<cst::ArgumentList>()))
                : "") +
           ")";
}

/*
angledParams : '<' pairedParams? ','? '>' ;
*/
any Formatter::visitAngledParams(cst::AngledParams context) {
    return "<" +
           (context->get<cst::PairedParams>()
                ? any_cast<string>(visitPairedParams(context->get<cst::PairedParams>()))
                : "") +
           ">";
}

/*
angledValues : '<' argumentList? ','? '>' ;
*/
any Formatter::visitAngledValues(cst::AngledValues context) {
    return "<" +
           (context->get<cst::ArgumentList>()
                ? any_cast<string>(visitArgumentList(context->get<cst::ArgumentList>()))
                : "") +
           ">";
}

/*
pattern
    : identRef identDef?
    | literal
    | '(' (dataList | identList)? ','? ')'
    | '{' (pairedValues | identList)? ','? '}'
    | '_' // wildcard
    ;
*/
any Formatter::visitPattern(cst::Pattern context) {
    if (context->get<cst::IdentRef>()) {
        // `error e`: the bound name is part of the pattern. A variant path may carry the
        // field patterns of the variant: `Shape::Circle(r)`, `Shape::Rect { w, h }`.
        string result = any_cast<string>(visitIdentRef(context->get<cst::IdentRef>()));
        if (const auto tuple = context->get<cst::EnumTuplePattern>()) {
            return result + any_cast<string>(visitEnumTuplePattern(tuple));
        }
        if (const auto record = context->get<cst::EnumStructPattern>()) {
            return result + any_cast<string>(visitEnumStructPattern(record));
        }
        if (context->get<cst::IdentDef>()) {
            result += " " + any_cast<string>(visitIdentDef(context->get<cst::IdentDef>()));
        }
        return result;
    }
    if (context->get<cst::Literal>()) {
        return visitLiteral(context->get<cst::Literal>());
    }
    const string open = context->firstToken()->text();
    if (open == "(") {
        string body;
        if (context->get<cst::DataList>()) {
            body = any_cast<string>(visitDataList(context->get<cst::DataList>()));
        } else if (context->get<cst::IdentList>()) {
            body = any_cast<string>(visitIdentList(context->get<cst::IdentList>()));
        }
        return "(" + body + (hasTrailingComma(context) ? "," : "") + ")";
    }
    if (open == "{") {
        string body;
        if (context->get<cst::PairedValues>()) {
            body = any_cast<string>(visitPairedValues(context->get<cst::PairedValues>()));
        } else if (context->get<cst::IdentList>()) {
            body = any_cast<string>(visitIdentList(context->get<cst::IdentList>()));
        }
        return "{" + body + (hasTrailingComma(context) ? "," : "") + "}";
    }
    return string("_");
}

/*
matchCase
    : CASE pattern ('|' pattern)* '=>' blockExpr
    ;
*/
any Formatter::visitMatchCase(cst::MatchCase context) {
    // `case p when c => body`: the guard stands between the patterns and the arm, as it does
    // in the source the parser reads (node_kinds.def). Leaving it out changes the AST the text
    // parses to, so it is written where it stood.
    string result =
        "case " + formatList(vec(context->all<cst::Pattern>()), context, " | ", " |");
    if (const auto guard = context->get<cst::WhenClause>()) {
        result += " " + any_cast<string>(visitWhenClause(guard));
    }
    return result + " => " + any_cast<string>(visitBlockExpr(context->lastChild().node()));
}

/*
matchTypeExpr
    : MATCH TYPE typeExpr '{' typeCase* '}'
    ;
*/
any Formatter::visitMatchTypeExpr(cst::MatchTypeExpr context) {
    return "match type " + formatTypeInTypePosition(context->type()) + " {" +
           formatList(
               vec(context->all<cst::TypeCase>()),
               context,
               " ",
               "",
               PaddingNL | Multiline | PushScope) +
           "}";
}

/*
typeCase
    : CASE typeCasePattern ('|' typeCasePattern)* '=>' blockExpr
    ;
*/
any Formatter::visitTypeCase(cst::TypeCase context) {
    return "case " +
           formatList(vec(context->all<cst::TypeCasePattern>()), context, " | ", " |") + " => " +
           any_cast<string>(visitBlockExpr(context->lastChild().node()));
}

/*
matchTypeType
    : 'match' 'type' typeExpr '{' typeArm* '}'
    ;
*/
any Formatter::visitMatchTypeType(cst::MatchTypeType context) {
    return "match type " + formatTypeInTypePosition(context->type()) + " {" +
           formatList(
               vec(context->all<cst::TypeArm>()),
               context,
               " ",
               "",
               PaddingNL | Multiline | PushScope) +
           "}";
}

/*
typeArm
    : CASE typeCasePattern ('|' typeCasePattern)* '=>' typeExpr
    ;
*/
any Formatter::visitTypeArm(cst::TypeArm context) {
    return "case " +
           formatList(vec(context->all<cst::TypeCasePattern>()), context, " | ", " |") + " => " +
           any_cast<string>(visit(context->type()));
}

/*
typeCasePattern
    : 'struct' | 'tuple' | '#' 'leaf' | '_'
    ;
*/
any Formatter::visitTypeCasePattern(cst::TypeCasePattern context) {
    return context->text();
}

/*
staticForExpr
    : STATIC staticForHeader '=>' blockExpr
    ;
staticForHeader
    : FOR (identDef | '_') (',' (identDef | '_'))* IN
      ('fields' typeExpr | 'indices' dataExpr)
    ;
The head writes no parentheses, exactly as `staticLoopExpr`'s does: the two heads spell one
`for ... in ...`. It writes as many names as the element it walks has parts: `fields` gives the
triple (name, type, tags), `indices` gives one thing.
*/
any Formatter::visitStaticForExpr(cst::StaticForExpr context) {
    const auto mode = context->tok(TK::Identifier);
    if (mode && mode.text() == "indices") {
        return "static for " + formatStaticHeadNames(context) + " in indices " +
               any_cast<string>(visit(context->expr())) + " => " +
               any_cast<string>(visitBlockExpr(context->lastChild().node()));
    }
    return "static for " + formatStaticHeadNames(context) + " in fields " +
           formatTypeInTypePosition(context->type()) + " => " +
           any_cast<string>(visitBlockExpr(context->lastChild().node()));
}

string Formatter::formatStaticHeadNames(syn::Node head) {
    string names;
    bool first = true;
    for (const auto name : head.all<cst::IdentDef>()) {
        names += (first ? "" : ", ") + any_cast<string>(visitIdentDef(name));
        first = false;
    }
    return names;
}

/// A `(` at the head of a type after `fields` / `match type` opens a tuple or a parenthesised
/// type, not a parameter list, so a function type written there needs one more
/// pair of parentheses: `fields ((x: int) => int)`. Every other type is printed as it is.
string Formatter::formatTypeInTypePosition(syn::Node type) {
    const string text = any_cast<string>(visitType(type));
    syn::Node head    = type;
    while (head && head.is(NK::TypeExpr)) {
        head = head.type(0);
    }
    return head && head.is(NK::FuncType) && text.starts_with("(") ? "(" + text + ")" : text;
}

/*
staticLoopExpr
    : STATIC LOOP loopState (',' loopState)* staticLoopHeader DO blockExpr
    ;
staticLoopHeader
    : FOR (identDef | '_') (',' (identDef | '_'))* IN
      ('fields' typeExpr | 'indices' dataExpr | dataExpr)
    ;
The head is written without parentheses, exactly as `static for` writes its own (one head, one
spelling): `for j in indices ts`, `for i in 0..5`, `for n in fields T`, or
`for n, F, tags in fields T` (the names follow the element, so a head may write one, two or
three of them).
*/
any Formatter::visitStaticLoopExpr(cst::StaticLoopExpr context) {
    string text = "static loop ";
    bool first  = true;
    for (const auto state : context->all<cst::LoopState>()) {
        text += (first ? "" : ", ") +
                any_cast<string>(visitIdentDef(state.get<cst::IdentDef>())) + " = " +
                any_cast<string>(visit(state.expr()));
        first = false;
    }
    const auto mode   = context->tok(TK::Identifier);
    const string body = any_cast<string>(visitBlockExpr(context->lastChild().node()));
    const string names = formatStaticHeadNames(context);
    if (mode && mode.text() == "indices") {
        return text + " for " + names + " in indices " +
               any_cast<string>(visit(context->expr())) + " do " + body;
    }
    if (mode && mode.text() == "fields") {
        return text + " for " + names + " in fields " +
               any_cast<string>(visitType(context->type())) + " do " + body;
    }
    return text + " for " + names + " in " + any_cast<string>(visit(context->expr())) + " do " +
           body;
}

/*
ctrlExpr
    : IF logicalOrExpr THEN blockExpr (ELSE blockExpr)?
    | MATCH logicalOrExpr '{' matchCase+ '}'
    ;
*/
any Formatter::visitCtrlExpr(syn::Node context) {
    if (context.is(NK::MatchTypeExpr)) {
        return visitMatchTypeExpr(cst::MatchTypeExpr(context));
    }
    switch (context.is(NK::IfExpr) ? 1 : 2) {
    case 1: // IF logicalOrExpr THEN blockExpr (ELSE blockExpr)?
    {
        string condition = any_cast<string>(visitExpr(context->expr()));
        string thenBlock = any_cast<string>(visitBlockExpr(context->child(3).node()));
        string result    = "if " + condition;
        // A comment between the condition and `then`, or between the branch and `else`, stands in
        // no gap a list looks at: the branch of a block-less `if` is an expression, not a list,
        // and the keyword after it belongs to this visitor alone. A `//` comment ends the line it
        // stands on, so `then` must not follow it on that line: written after the comment, the
        // keyword is read as part of it and the conditional loses it, which is the formatted text
        // failing to parse (`SyntaxDiag::MissingThen`). The `else` below already ends the line on
        // this answer; here it was dropped.
        const auto &thenTok = context->tok(TK::KwThen);
        const bool conditionLineEnds = writeInlineComments(
            result,
            context->expr()->lastToken()->index(),
            thenTok.index(),
            context->expr()->lastToken()->endLine());
        result += conditionLineEnds ? lineEnd() + "then " : " then ";
        result += thenBlock;
        if (context->tok(TK::KwElse)) {
            const auto &elseTok       = context->tok(TK::KwElse);
            const size_t branchEnd    = context->child(3).node()->lastToken()->index();
            const bool branchLineEnds = writeInlineComments(
                result,
                branchEnd,
                elseTok.index(),
                context->child(3).node()->lastToken()->endLine());
            string elseBlock = any_cast<string>(visitBlockExpr(context->child(5).node()));
            if (branchLineEnds || (elseBlock.starts_with("if ") && !thenBlock.ends_with("}"))) {
                // else if
                result += lineEnd() + "else ";
            } else {
                result += " else ";
            }
            result += elseBlock;
        }
        return result;
        break;
    }
    case 2: // MATCH logicalOrExpr '{' matchCase+ '}'
        return "match " + any_cast<string>(visitExpr(context->expr())) + " {" +
               formatList(
                   vec(context->all<cst::MatchCase>()),
                   context,
                   " ",
                   "",
                   PaddingNL | Multiline | PushScope) +
               "}";
        break;

    default:
        // `try`/`catch`/`finally` are not part of the language (design principles 3.9) and are not
        // tokens any more, so the parser never builds a control expression for them.
        throw runtime_error("Invalid structExpr context");
    }
}

/*
assignExpr
    : logicalOrExpr (
        ('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&=' | '|=')
        logicalOrExpr
    )?
    ;
*/
any Formatter::visitAssignExpr(cst::AssignExpr context) {
    string result = any_cast<string>(visitExpr(context->expr(0)));
    if (context->childCount() > 2) {
        result +=
            " " + context->child(1)->text() + " " + any_cast<string>(visitExpr(context->expr(1)));
    }
    return result;
}

/*
logicalOrExpr
    : logicalAndExpr ('||' logicalAndExpr)*
    ;
*/
any Formatter::visitLogicalOrExpr(cst::LogicalOrExpr context) {
    return formatBiOpsList(context, true);
}

/*
logicalAndExpr
    : equalityExpr ('&&' equalityExpr)*
    ;
*/
any Formatter::visitLogicalAndExpr(cst::LogicalAndExpr context) {
    return formatBiOpsList(context, true);
}

/*
equalityExpr
    : relationalExpr (('===' | '!==' | '==' | '!=') relationalExpr)*
    ;
*/
any Formatter::visitEqualityExpr(cst::EqualityExpr context) {
    return formatBiOpsList(context, true);
}

/*
relationalExpr
    : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
    ;
*/
any Formatter::visitRelationalExpr(cst::RelationalExpr context) {
    return formatBiOpsList(context, true);
}

/*
additiveExpr
    : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
    ;
*/
any Formatter::visitAdditiveExpr(cst::AdditiveExpr context) {
    return formatBiOpsList(context, true);
}

/*
multiplicativeExpr
    : nullableExpr (('*' | '/' | '^' | '@' | '%') nullableExpr)*
    ;
*/
any Formatter::visitMultiplicativeExpr(cst::MultiplicativeExpr context) {
    return formatBiOpsList(context, true);
}

/*
nullableExpr
    : unaryExpr (('??' | '!!') dataExpr)?
    ;
*/
any Formatter::visitNullableExpr(cst::NullableExpr context) {
    string result = any_cast<string>(visitExpr(context->expr()));
    if (context->childCount() > 1) {
        return result + " " + context->child(1)->text() + " " +
               any_cast<string>(visitExpr(context->expr(1)));
    } else {
        return result;
    }
}

/*
unaryExpr
    : linkExpr ((AS | IS) typeExpr)?
    | ('!' | '-' | '~') linkExpr
    ;
*/
any Formatter::visitUnaryExpr(syn::Node context) {
    string operand = any_cast<string>(visitExpr(context->expr()));
    if (context.is(NK::CastExpr)) {
        return operand + " " + context->child(1)->text() + " " +
               any_cast<string>(visitType(context->type()));
    }
    const string op = context->child(0)->text();
    // A space where the operator and the operand's first character would read as one token:
    // `! !x` is two negations, `!!x` the error branch; likewise `- -x`, `- >`, `! =`.
    const char next  = operand.empty() ? '\0' : operand[0];
    const bool joins = (op == "!" && (next == '!' || next == '=')) ||
                       (op == "-" && (next == '-' || next == '>' || next == '='));
    return joins ? op + " " + operand : op + operand;
}

/*
linkExpr
    : compExpr ('->' compExpr)*
    ;
*/
any Formatter::visitLinkExpr(cst::LinkExpr context) { return formatBiOpsList(context, false); }

/*
compExpr
    : annoExpr ('..' annoExpr)*
    ;
*/
any Formatter::visitCompExpr(cst::CompExpr context) { return formatBiOpsList(context, false); }

/*
annoExpr
    : accessExpr (({isAdjacent()}? (indices | parentArgues | angledValues | '!')) | memberAccess)*
    ;
*/
any Formatter::visitAnnoExpr(cst::AnnoExpr context) {
    string result = any_cast<string>(visitExpr(context->expr()));
    for (size_t i = 1; i < context->childCount(); i++) {
        const auto &child = context->child(i);
        if (child.isToken()) {
            result += child.text();
        } else {
            result += any_cast<string>(visit(child.node()));
        }
    }
    return result;
}

/*
accessExpr
    : primaryData ('.' (IDENTIFIER | INTEGER | REAL))*
    ;
*/
any Formatter::visitAccessExpr(cst::AccessExpr context) {
    string result = any_cast<string>(visitExpr(context->expr()));
    for (size_t i = 1; i < context->childCount(); i++) {
        const auto &child = context->child(i);
        if (child.isToken()) {
            result += child.text();
        } else {
            result += any_cast<string>(visit(child.node()));
        }
    }
    return result;
}

/*
memberAccess
    : '.' (IDENTIFIER | INTEGER | REAL | '[' dataExpr ']')
    ;
*/
any Formatter::visitMemberAccess(cst::MemberAccess context) {
    if (context->child(1).isToken() && context->child(1).text() == "[") {
        return ".[" + any_cast<string>(visit(context->child(2).node())) + "]";
    }
    return "." + context->child(1)->text();
}

/*
structData
    : '{' (pairedValues ','?)? '}' // no list comprehension because the struct is immutable
    | '{' staticForExpr '}'        // a struct comprehension
    ;
*/
any Formatter::visitStructData(cst::StructData context) {
    // A comprehension holds its generator and nothing else.
    if (const auto generator = context->get<cst::StaticForExpr>()) {
        return "{" + any_cast<string>(visitStaticForExpr(generator)) + "}";
    }
    const auto &pairedValues = context->get<cst::PairedValues>();
    if (pairedValues) {
        return "{" +
               formatList(
                   vec(pairedValues->all<cst::KeyValuePair>()),
                   pairedValues,
                   ", ",
                   ",",
                   PaddingSP | PaddingNL | PushScope) +
               "}";
    } else {
        return string("{}");
    }
}

/*
arrayData
    : '[' ((indexValues ','?) | dataExpr FOR identRef IN dataExpr (IF dataExpr)?)? ']'
    ;
*/
any Formatter::visitArrayData(cst::ArrayData context) {
    if (context->childCount() == 2) {
        return string("[]");
    } else {
        const auto &indexValues = context->get<cst::IndexValues>();
        if (indexValues) {
            return "[" + any_cast<string>(visitIndexValues(indexValues)) + "]";
        } else {
            const auto dataExprs = context->exprs();
            string result        = "[" + any_cast<string>(visitExpr(dataExprs[0]));
            result += " for " + any_cast<string>(visitIdentRef(context->get<cst::IdentRef>())) +
                      " in " + any_cast<string>(visitExpr(dataExprs[1]));
            if (dataExprs.size() > 2) {
                result += " if " + any_cast<string>(visitExpr(dataExprs[2]));
            }
            result += "]";
            return result;
        }
    }
}

/*
tupleData
    : '(' dataList? ','? ')'
    | '(' staticForExpr ')'        // a tuple comprehension
    ;
*/
any Formatter::visitTupleData(cst::TupleData context) {
    if (const auto generator = context->get<cst::StaticForExpr>()) {
        return "(" + any_cast<string>(visitStaticForExpr(generator)) + ")";
    }
    if (context->get<cst::DataList>()) {
        return "(" + any_cast<string>(visitDataList(context->get<cst::DataList>())) +
               (hasTrailingComma(context) ? "," : "") + ")";
    } else {
        return string("()");
    }
}

/*
literal
    : ('+' | '-')? INTEGER
    | ('+' | '-')? REAL
    | STRING
    | MULTI_STR
    | FSTRING
    | TRUE
    | FALSE
    | NULL
    ;
*/
any Formatter::visitLiteral(cst::Literal context) {
    if (context->tok(TK::String)) {
        return formatStringLiteral(context->tok(TK::String)->text());
    } else if (context->tok(TK::MultiStr)) {
        return formatStringLiteral(context->tok(TK::MultiStr)->text(), true);
    } else if (context->tok(TK::FString)) {
        return context->text();
    } else {
        return context->text();
    }
}

/*
typeExpr
    : arrayType ('?' | '!' | '..')*
    ;
*/
any Formatter::visitTypeExpr(cst::TypeExpr context) {
    string result;
    result += any_cast<string>(visitType(context->type(0)));
    // Preserve distinct postfix tokens: adjacent equal marks would lex as
    // the expression operators `??` or `!!` instead of two type suffixes.
    for (size_t i = 1; i < context->childCount(); ++i) {
        const auto mark = context->child(i)->text();
        if (!result.empty() && !mark.empty() && result.back() == mark.front()) {
            result += ' ';
        }
        result += mark;
    }
    return result;
}

/*
arrayType
    : specType arrayDim+
    ;
*/
any Formatter::visitArrayType(cst::ArrayType context) {
    string result = any_cast<string>(visitType(context->type()));
    for (const auto dim : context->all<cst::ArrayDim>()) {
        result += any_cast<string>(visitArrayDim(dim));
    }
    return result;
}

/*
arrayDim
    : '[' (']' | arrayDimItem (',' arrayDimItem)* ','?)? ']'
    ;
arrayDimItem : '?' | '..' identDef? | dataExpr
*/
any Formatter::visitArrayDim(cst::ArrayDim context) {
    string result;
    bool first    = true;
    bool packName = false;
    for (size_t i = 0; i < context->childCount(); ++i) {
        const auto child = context->child(i);
        if (child.isToken()) {
            const TK kind = child.token()->kind();
            if (kind == TK::Question) {
                if (!first) {
                    result += ", ";
                }
                first = false;
                result += "?";
            } else if (kind == TK::DotDot) {
                if (!first) {
                    result += ", ";
                }
                first    = false;
                result += "..";
                packName = true;
            }
            continue;
        }
        const auto node = child.node();
        if (packName && node.is(NK::IdentDef)) {
            result += node.text();
            packName = false;
        } else if (node.isExpr()) {
            if (!first) {
                result += ", ";
            }
            first = false;
            result += any_cast<string>(visitExpr(node));
        }
    }
    return "[" + result + "]";
}

/*
typeOrData : typeExpr | CONST dataExpr ;
*/
any Formatter::visitConstData(cst::ConstData context) {
    return "const " + any_cast<string>(visitExpr(context->expr()));
}

/*
specType
    : primaryType ('<' typeOrData (',' typeOrData)* '>')?
    ;
*/
any Formatter::visitSpecType(cst::SpecType context) {
    vector<syn::Node> arguments = vec(context->types());
    string result               = any_cast<string>(visitType(arguments[0]));
    arguments.erase(arguments.begin());
    result += "<";
    result += formatList(arguments, context, ", ", ",", PushScope);
    result += ">";
    return result;
}

/*
structType
    : '{' (keyTypePair (',' keyTypePair)*)? ','? '}'
    ;
*/
any Formatter::visitStructType(cst::StructType context) {
    const auto keyTypePairs = vec(context->all<cst::KeyTypePair>());
    return "{" + formatList(keyTypePairs, context, ", ", ",", PaddingNL | PaddingSP | PushScope) +
           "}";
}

/*
enumType
    : 'enum' '{' (enumVariant (',' enumVariant)*)? ','? '}'
    ;
*/
any Formatter::visitEnumType(cst::EnumType context) {
    const auto variants = vec(context->all<cst::EnumVariant>());
    return "enum {" + formatList(variants, context, ", ", ",", PaddingNL | PaddingSP | PushScope) +
           "}";
}

/*
enumVariant
    : identDef ('(' typeList? ','? ')' | '{' (enumField (',' enumField)*)? ','? '}')?
    ;
*/
any Formatter::visitEnumVariant(cst::EnumVariant context) {
    string result = any_cast<string>(visitIdentDef(context->get<cst::IdentDef>()));
    if (const auto types = context->get<cst::TypeList>()) {
        result += "(" + any_cast<string>(visitTypeList(types)) + ")";
    } else {
        const auto fields = vec(context->all<cst::EnumField>());
        if (!fields.empty()) {
            result += " {" +
                      formatList(fields, context, ", ", ",", PaddingNL | PaddingSP | PushScope) +
                      "}";
        }
    }
    return result;
}

/*
enumField
    : identDef ':' typeExpr
    ;
*/
any Formatter::visitEnumField(cst::EnumField context) {
    return any_cast<string>(visitIdentDef(context->get<cst::IdentDef>())) + ": " +
           any_cast<string>(visitType(context->type()));
}

/*
enumTuplePattern
    : '(' (pattern (',' pattern)*)? ','? ')'
    ;
*/
any Formatter::visitEnumTuplePattern(cst::EnumTuplePattern context) {
    return "(" + formatList(vec(context->all<cst::Pattern>()), context, ", ", ",", 0) +
           (hasTrailingComma(context) ? "," : "") + ")";
}

/*
enumStructPattern
    : '{' (IDENTIFIER (':' pattern)? ','?)* '}'
    ;
*/
any Formatter::visitEnumStructPattern(cst::EnumStructPattern context) {
    string body;
    bool first = true;
    for (size_t i = 0; i < context->childCount(); ++i) {
        const auto child = context->child(i);
        if (!child.isToken() || child.token()->kind() != TK::Identifier) {
            continue;
        }
        if (!first) {
            body += ", ";
        }
        first = false;
        body += child.text();
        // The pattern after the colon renames or takes the field apart.
        if (i + 1 < context->childCount() && context->child(i + 1).isNode()) {
            body += ": " + any_cast<string>(visit(context->child(i + 1).node()));
            ++i;
        }
    }
    if (body.empty()) {
        return " {}";
    }
    return " { " + body + (hasTrailingComma(context) ? "," : "") + " }";
}

/*
typeList
    : typeExpr (',' typeExpr)*
    ;
*/
any Formatter::visitTypeList(cst::TypeList context) {
    return formatList(vec(context->types()), context, ", ", ",", PaddingNL | PushScope);
}

/*
typeParams
    : '::' '<' identDef (',' identDef)* ','? '>'
    ;
*/
any Formatter::visitTypeParams(cst::TypeParams context) {
    const auto names = vec(context->all<cst::IdentDef>());
    return "::<" + formatList(names, context, ", ", ",", PaddingNL | PushScope) + ">";
}

/*
typeArguments
    : '::' '<' (typeList | namedTypeArgument (',' namedTypeArgument)*) ','? '>'
    ;
*/
any Formatter::visitTypeArguments(cst::TypeArguments context) {
    string result = "::<";
    if (const auto types = context->get<cst::TypeList>()) {
        result += any_cast<string>(visitTypeList(types));
    } else {
        const auto named = vec(context->all<cst::NamedTypeArgument>());
        result += formatList(named, context, ", ", ",", PaddingNL | PushScope);
    }
    result += hasTrailingComma(context) ? "," : "";
    return result + ">";
}

/*
namedTypeArgument
    : IDENTIFIER '=' typeExpr
    ;
*/
any Formatter::visitNamedTypeArgument(cst::NamedTypeArgument context) {
    return context->tok(TK::Identifier)->text() + " = " +
           any_cast<string>(visitType(context->type()));
}

/*
tupleType
    : '(' typeList? ','? ')'
    ;
*/
any Formatter::visitTupleType(cst::TupleType context) {
    return "(" +
           (context->get<cst::TypeList>()
                ? any_cast<string>(visitTypeList(context->get<cst::TypeList>()))
                : "") +
           (hasTrailingComma(context) ? "," : "") + ")";
}

/*
funcType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;
*/
any Formatter::visitFuncType(cst::FuncType context) {
    string result;
    const auto &modifiers    = context->get<cst::Modifiers>();
    const auto &angledParams = context->get<cst::AngledParams>();
    const auto &parentParams = context->get<cst::ParentParams>();
    if (modifiers) {
        result += any_cast<string>(visitModifiers(modifiers)) + " ";
    }
    if (angledParams) {
        result += any_cast<string>(visitAngledParams(angledParams));
    }
    result += any_cast<string>(visitParentParams(parentParams));
    result += " => " + any_cast<string>(visitType(context->type()));
    if (const auto clause = context->get<cst::ResourceClause>()) {
        result += " " + any_cast<string>(visitResourceClause(clause));
    }
    return result;
}

/*
identDef : IDENTIFIER ;
*/
any Formatter::visitIdentDef(cst::IdentDef context) {
    // The text of the node, not of an Identifier token: the `_` of `let _ = e` is a token of
    // its own kind.
    return context->text();
}

/*
identRef : (IDENTIFIER '::')* IDENTIFIER ;
*/
any Formatter::visitIdentRef(cst::IdentRef context) { return context->text(); }

// ---- Dispatch on the kind of a node ----

any Formatter::visit(syn::Node context) {
    // A comment between two tokens the node writes itself - a parameter and its type, say - is in
    // no gap any list looks at, and the node writes those tokens without it. Write it here, at the
    // token it stands before: the next pass finds it in the same gap and writes it in the same
    // place, so the text settles instead of moving on every pass.
    std::string above = measureOnly ? std::string() : commentsBefore(context);
    std::any body     = dispatch(context);
    if (above.empty()) {
        return body;
    }
    return above + any_cast<string>(body);
}

any Formatter::dispatch(syn::Node context) {
    switch (context.kind()) {
#define CAMEL_FORMAT(name)                                                                         \
    case NK::name:                                                                                 \
        return visit##name(cst::name(context));
        CAMEL_FORMAT(StmtList)
        CAMEL_FORMAT(ModuleName)
        CAMEL_FORMAT(ImportDecl)
        CAMEL_FORMAT(ExportDecl)
        CAMEL_FORMAT(UsingNamespaceDecl)
        CAMEL_FORMAT(NamespaceDecl)
        CAMEL_FORMAT(BlockStmt)
        CAMEL_FORMAT(StmtBlock)
        CAMEL_FORMAT(FuncAnno)
        CAMEL_FORMAT(TagDecl)
        CAMEL_FORMAT(TagPositions)
        CAMEL_FORMAT(TagReaders)
        CAMEL_FORMAT(TagUse)
        CAMEL_FORMAT(TagRef)
        CAMEL_FORMAT(FuncDecl)
        CAMEL_FORMAT(RuleDecl)
        CAMEL_FORMAT(WhenClause)
        CAMEL_FORMAT(FactDecl)
        CAMEL_FORMAT(FactBody)
        CAMEL_FORMAT(FactCase)
        CAMEL_FORMAT(FactPattern)
        CAMEL_FORMAT(FactClause)
        CAMEL_FORMAT(TypeParams)
        CAMEL_FORMAT(RuleBody)
        CAMEL_FORMAT(DataDecl)
        CAMEL_FORMAT(ResourceDecl)
        CAMEL_FORMAT(ResourceClause)
        CAMEL_FORMAT(TypeDecl)
        CAMEL_FORMAT(RetStmt)
        CAMEL_FORMAT(Modifiers)
        CAMEL_FORMAT(ParentIdents)
        CAMEL_FORMAT(BracedIdents)
        CAMEL_FORMAT(BracketIdents)
        CAMEL_FORMAT(KeyTypePair)
        CAMEL_FORMAT(KeyValuePair)
        CAMEL_FORMAT(KeyParamPair)
        CAMEL_FORMAT(TupleParam)
        CAMEL_FORMAT(DataList)
        CAMEL_FORMAT(IdentList)
        CAMEL_FORMAT(IndexValues)
        CAMEL_FORMAT(PairedValues)
        CAMEL_FORMAT(PairedParams)
        CAMEL_FORMAT(ArgumentList)
        CAMEL_FORMAT(Indices)
        CAMEL_FORMAT(ParentParams)
        CAMEL_FORMAT(ParentArgues)
        CAMEL_FORMAT(AngledParams)
        CAMEL_FORMAT(AngledValues)
        CAMEL_FORMAT(Pattern)
        CAMEL_FORMAT(MatchCase)
        CAMEL_FORMAT(MatchTypeExpr)
        CAMEL_FORMAT(StaticForExpr)
        CAMEL_FORMAT(StaticLoopExpr)
        CAMEL_FORMAT(TypeCase)
        CAMEL_FORMAT(TypeCasePattern)
        CAMEL_FORMAT(MemberAccess)
        CAMEL_FORMAT(TypeList)
        CAMEL_FORMAT(IdentDef)
        CAMEL_FORMAT(IdentRef)
        CAMEL_FORMAT(RestValue)
        CAMEL_FORMAT(AssignExpr)
        CAMEL_FORMAT(LogicalOrExpr)
        CAMEL_FORMAT(LogicalAndExpr)
        CAMEL_FORMAT(EqualityExpr)
        CAMEL_FORMAT(RelationalExpr)
        CAMEL_FORMAT(AdditiveExpr)
        CAMEL_FORMAT(MultiplicativeExpr)
    case NK::LoopExpr: {
        // loop s = init, t = init (for x in xs)? (while c)? do body
        string text = "loop ";
        bool first  = true;
        for (const auto state : context.all<cst::LoopState>()) {
            text += (first ? "" : ", ") +
                    any_cast<string>(visitIdentDef(state.get<cst::IdentDef>())) + " = " +
                    any_cast<string>(visit(state.expr()));
            first = false;
        }
        if (const auto clause = context.get<cst::LoopFor>()) {
            text += " for " + any_cast<string>(visitIdentDef(clause.get<cst::IdentDef>())) +
                    " in " + any_cast<string>(visit(clause.expr()));
        }
        if (const auto clause = context.get<cst::LoopWhile>()) {
            text += " while " + any_cast<string>(visit(clause.expr()));
        }
        return text + " do " + any_cast<string>(visitBlockExpr(context.lastChild().node()));
    }
    case NK::ImportItem: {
        // name, or name as local
        string item = any_cast<string>(visitIdentDef(context.get<cst::IdentDef>(0)));
        if (context.get<cst::IdentDef>(1)) {
            item += " as " + any_cast<string>(visitIdentDef(context.get<cst::IdentDef>(1)));
        }
        return item;
    }
    case NK::OrExpr:
    case NK::AndExpr:
        return formatBiOpsList(context, true);
    case NK::PowerExpr:
        return formatBiOpsList(context, true);
        CAMEL_FORMAT(NullableExpr)
        CAMEL_FORMAT(LinkExpr)
        CAMEL_FORMAT(CompExpr)
        CAMEL_FORMAT(AnnoExpr)
        CAMEL_FORMAT(AccessExpr)
        CAMEL_FORMAT(Literal)
        CAMEL_FORMAT(ArrayData)
        CAMEL_FORMAT(StructData)
        CAMEL_FORMAT(TupleData)
        CAMEL_FORMAT(FuncData)
        CAMEL_FORMAT(TypeExpr)
        CAMEL_FORMAT(ArrayType)
        CAMEL_FORMAT(ArrayDim)
        CAMEL_FORMAT(SpecType)
        CAMEL_FORMAT(MatchTypeType)
        CAMEL_FORMAT(TypeArm)
        CAMEL_FORMAT(ConstData)
        CAMEL_FORMAT(StructType)
        CAMEL_FORMAT(EnumType)
        CAMEL_FORMAT(EnumVariant)
        CAMEL_FORMAT(EnumField)
        CAMEL_FORMAT(EnumTuplePattern)
        CAMEL_FORMAT(EnumStructPattern)
        CAMEL_FORMAT(TupleType)
        CAMEL_FORMAT(FuncType)
        CAMEL_FORMAT(TypeArguments)
        CAMEL_FORMAT(NamedTypeArgument)
#undef CAMEL_FORMAT
    case NK::IfExpr:
    case NK::MatchExpr:
        return visitCtrlExpr(context);
    case NK::CastExpr:
    case NK::UnaryExpr:
        return visitUnaryExpr(context);
    case NK::RefData:
    case NK::RefType:
        return context.text();
    case NK::ParenType:
        return "(" + any_cast<string>(visit(context.type())) + ")";
    case NK::TypeOfType:
        return "typeof " + any_cast<string>(visit(context.expr()));
    case NK::ShapeComp: {
        // `T { static for n, F in fields U => e }`: the shape is written as a type, and the
        // generator prints as it does on its own; the braces are tokens, so the generator is
        // taken by kind and not by child index.
        const auto generator = context.get<cst::StaticForExpr>();
        return formatTypeInTypePosition(context.type()) + " { " +
               any_cast<string>(visitStaticForExpr(generator)) + " }";
    }
    case NK::ConstasData:
        return "constas " + any_cast<string>(visitIdentDef(context.get<cst::IdentDef>()));
    case NK::SliceIndices:
        return visitSliceIndices(cst::SliceIndices(context));
    default:
        throw std::runtime_error(
            "camel-format: no rule for " + std::string(syn::nodeKindName(context.kind())));
    }
}
