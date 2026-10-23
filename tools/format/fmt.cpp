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
    if (tokChannel == 0 || tokChannel > 4) {
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
    syn::Node moduleDecl;
    vector<syn::Node> head, tail;
    vector<pair<syn::Node, size_t>> headWithIndex, tailWithIndex;
    vector<pair<size_t, size_t>> declTokenRanges, headTokenRanges, tailTokenRanges;

    size_t lastIndex              = 0;
    const vector<syn::Node> decls = vec(context->nodes());
    for (size_t i = 0; i < decls.size(); i++) {
        auto child = decls[i];
        if (child.is(NK::ModuleDecl)) {
            moduleDecl = child;
        } else if (child.is(NK::ImportDecl)) {
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
    if (moduleDecl) {
        // The comments above `module` (the description of the file) stay above it; they used
        // to be dropped. A blank line between two of them is kept: it is what separates the
        // licence header from the file's own description, and without it the two runs of
        // comments become one, which changes how the parser reads the file.
        size_t previousEnd = 0;
        for (size_t i = 0; i < moduleDecl.firstToken().index(); i++) {
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
        headStr += any_cast<string>(visit(moduleDecl));
        if (this->preferSemis) {
            headStr += ";";
        }
        headStr += lineEnd(2);
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
moduleDecl : MODULE identDef ;
*/
any Formatter::visitModuleDecl(cst::ModuleDecl context) {
    return "module " + any_cast<string>(visitIdentDef(context->get<cst::IdentDef>()));
}

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
blockStmt  : WAIT? stmtBlock | funcAnno+ stmtBlock ;
*/
any Formatter::visitBlockStmt(cst::BlockStmt context) {
    string result;
    // The tags written before the block (`#tag { ... }`) and `@numeric(...)`: each on a line of
    // its own, at the start of a line (design principles 4.1, 6.2).
    for (const auto &tagUse : context->all<cst::TagUse>()) {
        result += any_cast<string>(visitTagUse(tagUse)) + lineEnd();
    }
    for (const auto &funcAnno : context->all<cst::FuncAnno>()) {
        result += any_cast<string>(visitFuncAnno(funcAnno)) + lineEnd();
    }
    result += context->tok(TK::KwWait) ? "wait " : "";
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
tagDecl    : 'export'? 'tag' identDef angledParams? 'on' tagPositions ('read_by' tagReaders)?
           | 'export'? 'tag' identDef '=' bracketIdents
*/
any Formatter::visitTagDecl(cst::TagDecl context) {
    string result = context->tok(TK::KwExport) ? "export tag " : "tag ";
    result += any_cast<string>(visitIdentDef(context->get<cst::IdentDef>()));
    if (context->get<cst::AngledParams>()) {
        result += any_cast<string>(visitAngledParams(context->get<cst::AngledParams>()));
    }
    if (const auto group = context->get<cst::BracketIdents>()) {
        return result + " = " + any_cast<string>(visitBracketIdents(group));
    }
    result += " on " + any_cast<string>(visitTagPositions(context->get<cst::TagPositions>()));
    if (context->get<cst::TagReaders>()) {
        result += " read_by " + any_cast<string>(visitTagReaders(context->get<cst::TagReaders>()));
    }
    return result;
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
tagRef     : identRef angledValues?
*/
any Formatter::visitTagRef(cst::TagRef context) {
    string result = any_cast<string>(visitIdentRef(context->get<cst::IdentRef>()));
    if (const auto values = context->get<cst::AngledValues>()) {
        result += any_cast<string>(visitAngledValues(values));
    }
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
        result += any_cast<string>(visitTagUse(tagUse)) + lineEnd();
    }

    for (const auto funcAnno : funcAnnos) {
        result += any_cast<string>(visitFuncAnno(funcAnno)) + lineEnd();
    }

    if (angledParams) {
        result += "with " + any_cast<string>(visitAngledParams(angledParams)) + lineEnd();
    }

    if (context->tok(TK::KwExport)) {
        result += "export ";
    }

    if (modifiers) {
        result += any_cast<string>(visitModifiers(modifiers)) + " ";
    }

    // An operator overload is written `func operator +(...)`: the word, one space, the symbol
    // and the parameters right after it (design principles 4.3; the notation is provisional).
    if (const auto operatorName = context->get<cst::OperatorName>()) {
        result +=
            "func operator " + operatorName->text().substr(std::string_view("operator").size());
    } else {
        result += "func " + any_cast<string>(visitIdentRef(identRef));
    }
    if (const auto typeParams = context->get<cst::TypeParams>()) {
        result += any_cast<string>(visitTypeParams(typeParams));
    }
    result += any_cast<string>(visitParentParams(parentParams));

    if (typeExpr) {
        result += ": " + any_cast<string>(visitType(typeExpr));
    }

    if (const auto clause = context->get<cst::ResourceClause>()) {
        // `uses s, fs` / `pure` after the result type (design effects plan 3.1).
        result += " " + any_cast<string>(visitResourceClause(clause));
    }

    if (const auto whenClause = context->get<cst::WhenClause>()) {
        // The declaration's applicability condition, after the signature and before the body
        // (design principles 5.13): it is part of the declaration, so leaving it out changes
        // the AST the text parses to and, with it, the names synthesized from positions.
        result += " " + any_cast<string>(visitWhenClause(whenClause));
    }

    if (!stmtBlock) {
        // A body-less declaration of a C++ operator: the signature is the whole of it (design
        // principles 6.4; handover H19, step 1).
        return result;
    }
    return result + " " + any_cast<string>(visitStmtBlock(stmtBlock));
}

/*
whenClause : IDENTIFIER logicalOrExpr
*/
any Formatter::visitWhenClause(cst::WhenClause context) {
    // The applicability condition of a declaration (design principles 5.13): `when` and the
    // expression, as written, after the signature and before the body.
    return "when " + any_cast<string>(visitExpr(context->expr()));
}

/*
ruleDecl : EXPORT? IDENTIFIER identDef ruleContext? parentParams '{' ruleBody '}'
           (IDENTIFIER dataExpr)? ;
*/
any Formatter::visitRuleDecl(cst::RuleDecl context) {
    string result;
    // The tags written before the rule (`#requires<...>`, design principles 6.2, 7.2): each on a
    // line of its own, as before a function or a type declaration.
    for (const auto &tagUse : context->all<cst::TagUse>()) {
        result += any_cast<string>(visitTagUse(tagUse)) + lineEnd();
    }
    result += context->has(TK::KwExport) ? "export rule " : "rule ";
    result += any_cast<string>(visitIdentDef(context->get<cst::IdentDef>()));
    if (const auto typeParams = context->get<cst::TypeParams>()) {
        result += any_cast<string>(visitTypeParams(typeParams));
    }
    if (const auto ruleContext = context->get<cst::RuleContext>()) {
        result += any_cast<string>(visitRuleContext(ruleContext));
    }
    result += any_cast<string>(visitParentParams(context->get<cst::ParentParams>()));
    result += " { " + any_cast<string>(visitRuleBody(context->get<cst::RuleBody>())) + " }";
    if (const auto condition = context->expr()) {
        result += " when " + any_cast<string>(visitExpr(condition));
    }
    return result;
}

/*
analysisDecl : frontMatter? 'export'? IDENTIFIER identDef parentParams? ':' typeExpr
               '{' analysisClause* '}'
*/
any Formatter::visitAnalysisDecl(cst::AnalysisDecl context) {
    string result;
    for (const auto &tagUse : context->all<cst::TagUse>()) {
        result += any_cast<string>(visitTagUse(tagUse)) + lineEnd();
    }
    result += context->tok(TK::KwExport) ? "export analysis " : "analysis ";
    result += any_cast<string>(visitIdentDef(context->get<cst::IdentDef>()));
    if (const auto params = context->get<cst::ParentParams>()) {
        result += any_cast<string>(visitParentParams(params));
    }
    result += ": " + any_cast<string>(visitType(context->type()));
    result += " {";
    pushIndent();
    for (const auto clause : context->all<cst::AnalysisClause>()) {
        result += lineEnd() + any_cast<string>(visitAnalysisClause(clause));
    }
    popIndent();
    result += lineEnd() + "}";
    return result;
}

/*
analysisClause : IDENTIFIER '=>' dataExpr
               | IDENTIFIER '(' identList? ')' '=>' dataExpr
               | IDENTIFIER '(' ('const' identDef ':' typeExpr | dataExpr) ')' '=>' dataExpr
*/
any Formatter::visitAnalysisClause(cst::AnalysisClause context) {
    const string keyword = context->tok(TK::Identifier).text();
    const auto exprs     = context->exprs();
    if (const auto name = context->get<cst::IdentDef>()) {
        return "on (const " + any_cast<string>(visitIdentDef(name)) + ": " +
               any_cast<string>(visitType(context->type())) + ") => " +
               any_cast<string>(visitExpr(exprs[0]));
    }
    if (const auto params = context->get<cst::IdentList>()) {
        return keyword + "(" + any_cast<string>(visitIdentList(params)) + ") => " +
               any_cast<string>(visitExpr(exprs[0]));
    }
    if (keyword == "on") {
        return "on (" + any_cast<string>(visitExpr(exprs[0])) + ") => " +
               any_cast<string>(visitExpr(exprs[1]));
    }
    return keyword + " => " + any_cast<string>(visitExpr(exprs[0]));
}

/*
ruleContext : '<' identDef (',' identDef)* ','? '>' ;
*/
any Formatter::visitRuleContext(cst::RuleContext context) {
    string result = "<";
    bool first    = true;
    for (const auto name : context->all<cst::IdentDef>()) {
        result += (first ? "" : ", ") + any_cast<string>(visitIdentDef(name));
        first = false;
    }
    return result + ">";
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
    : TYPE identRef ('=' | '<' identDef (',' identDef)* ','? ('>' '=' | '>='))
      (typeExpr | STRING)
    ;
*/
any Formatter::visitTypeDecl(cst::TypeDecl context) {
    string result;
    for (const auto &tagUse : context->all<cst::TagUse>()) {
        result += any_cast<string>(visitTagUse(tagUse)) + lineEnd();
    }
    result += "type " + any_cast<string>(visitIdentRef(context->get<cst::IdentRef>()));
    if (const auto params = context->all<cst::IdentDef>(); !params.empty()) {
        result += "<";
        for (size_t i = 0; i < params.size(); ++i) {
            result += (i > 0 ? ", " : "") + any_cast<string>(visitIdentDef(params[i]));
        }
        result += ">";
    }
    result += " = ";
    if (context->type()) {
        result += any_cast<string>(visitType(context->type()));
    } else {
        result += formatStringLiteral(context->tok(TK::String)->text());
    }
    return result;
}

/*
retStmt    : (RETURN | RAISE | THROW) dataList ;
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
indexValue   : '...'? dataExpr ;
The tree has a node for it only with the marker.
*/
any Formatter::visitSpreadValue(cst::SpreadValue context) {
    return "..." + any_cast<string>(visitExpr(context->expr()));
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
    std::vector<syn::Node> params;
    for (const syn::Node child : context->nodes()) {
        if (child.is(NK::KeyParamPair) || child.is(NK::TupleParam)) {
            params.push_back(child);
        }
    }
    return formatList(params, context, ", ", ",", PaddingNL | PushScope);
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
    return "case " + formatList(vec(context->all<cst::Pattern>()), context, " | ", " |") + " => " +
           any_cast<string>(visitBlockExpr(context->lastChild().node()));
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
    : FOR '(' identDef (',' identDef)? ')' IN ('fields' typeExpr | 'indices' dataExpr)
    ;
*/
any Formatter::visitStaticForExpr(cst::StaticForExpr context) {
    const auto valueName = context->get<cst::IdentDef>(0);
    const auto mode      = context->tok(TK::Identifier);
    if (mode && mode.text() == "indices") {
        return "static for (" + any_cast<string>(visitIdentDef(valueName)) + ") in indices " +
               any_cast<string>(visit(context->expr())) + " => " +
               any_cast<string>(visitBlockExpr(context->lastChild().node()));
    }
    const auto typeName = context->get<cst::IdentDef>(1);
    return "static for (" + any_cast<string>(visitIdentDef(valueName)) + ", " +
           any_cast<string>(visitIdentDef(typeName)) + ") in fields " +
           formatTypeInTypePosition(context->type()) + " => " +
           any_cast<string>(visitBlockExpr(context->lastChild().node()));
}

/// A `(` at the head of a type after `fields` / `match type` opens a tuple or a parenthesised
/// type, not a parameter list (H79 ruling 1), so a function type written there needs one more
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
    : STATIC LOOP loopState (',' loopState)* staticForHeader DO blockExpr
    ;
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
    const auto mode = context->tok(TK::Identifier);
    if (mode && mode.text() == "indices") {
        text += " for (" +
                any_cast<string>(visitIdentDef(context->get<cst::IdentDef>(0))) +
                ") in indices " + any_cast<string>(visit(context->expr())) + " do " +
                any_cast<string>(visitBlockExpr(context->lastChild().node()));
        return text;
    }
    text += " for (" +
            any_cast<string>(visitIdentDef(context->get<cst::IdentDef>(0))) + ", " +
            any_cast<string>(visitIdentDef(context->get<cst::IdentDef>(1))) + ") in fields " +
            any_cast<string>(visitType(context->type())) + " do " +
            any_cast<string>(visitBlockExpr(context->lastChild().node()));
    return text;
}

/*
catchClause
    : CATCH identDef ':' typeExpr stmtBlock
    ;
*/
any Formatter::visitCatchClause(cst::CatchClause context) {
    return "catch " + any_cast<string>(visitIdentDef(context->get<cst::IdentDef>())) + ": " +
           any_cast<string>(visitType(context->type())) + " " +
           any_cast<string>(visitStmtBlock(context->get<cst::StmtBlock>()));
}

/*
ctrlExpr
    : IF logicalOrExpr THEN blockExpr (ELSE blockExpr)?
    | MATCH logicalOrExpr '{' matchCase+ '}'
    | TRY stmtBlock catchClause+ (FINALLY stmtBlock)?
    ;
*/
any Formatter::visitCtrlExpr(syn::Node context) {
    if (context.is(NK::MatchTypeExpr)) {
        return visitMatchTypeExpr(cst::MatchTypeExpr(context));
    }
    switch (context.is(NK::IfExpr) ? 1 : context.is(NK::MatchExpr) ? 2 : 3) {
    case 1: // IF logicalOrExpr THEN blockExpr (ELSE blockExpr)?
    {
        string condition = any_cast<string>(visitExpr(context->expr()));
        string thenBlock = any_cast<string>(visitBlockExpr(context->child(3).node()));
        string result    = "if " + condition;
        // A comment between the condition and `then`, or between the branch and `else`, stands in
        // no gap a list looks at: the branch of a block-less `if` is an expression, not a list,
        // and the keyword after it belongs to this visitor alone.
        const auto &thenTok = context->tok(TK::KwThen);
        writeInlineComments(
            result,
            context->expr()->lastToken()->index(),
            thenTok.index(),
            context->expr()->lastToken()->endLine());
        result += " then " + thenBlock;
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
    case 3: // TRY stmtBlock (CATCH identDef ':' typeExpr stmtBlock)+ (FINALLY stmtBlock)?
        return "try " + any_cast<string>(visitStmtBlock(context->get<cst::StmtBlock>(0))) +
               formatList(
                   vec(context->all<cst::CatchClause>()),
                   context,
                   " ",
                   "",
                   PaddingSP | InOneLine) +
               (context->tok(TK::KwFinally)
                    ? "finally " + any_cast<string>(visitStmtBlock(context->get<cst::StmtBlock>(1)))
                    : "");
        break;

    default:
        throw runtime_error("Invalid structExpr context");
    }
}

/*
waitExpr : WAIT? assignExpr ;
*/
any Formatter::visitWaitExpr(cst::WaitExpr context) {
    return (context->tok(TK::KwWait) ? "wait " : "") + any_cast<string>(visitExpr(context->expr()));
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
    : compExpr (('->' | '?->') compExpr)*
    ;
*/
any Formatter::visitLinkExpr(cst::LinkExpr context) { return formatBiOpsList(context, false); }

/*
compExpr
    : annoExpr (('..' | '?..') annoExpr)*
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
    | '{' staticForExpr '}'        // a struct comprehension (H51 step 4)
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
    | '(' staticForExpr ')'        // a tuple comprehension (H51 step 4)
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
    : interType ('?' | '!')*
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
interType
    : diffType ('&' diffType)*
    ;
*/
any Formatter::visitInterType(cst::InterType context) { return formatBiOpsList(context, true); }

/*
diffType
    : keyUnionDiffType ('\\' keyUnionDiffType)*
    ;
*/
any Formatter::visitDiffType(cst::DiffType context) { return formatBiOpsList(context, true); }

/*
keyUnionDiffType
    : keyInterType (('+' | '-') keyInterType)*
    ;
*/
any Formatter::visitKeyUnionDiffType(cst::KeyUnionDiffType context) {
    return formatBiOpsList(context, true);
}

/*
keyInterType
    : typeUnit ('^' typeUnit)*
    ;
*/
any Formatter::visitKeyInterType(cst::KeyInterType context) {
    return formatBiOpsList(context, true);
}

/*
typeUnit : (identDef OF)? arrayType ;
*/
any Formatter::visitTypeUnit(cst::TypeUnit context) {
    string result;
    const auto &identDef = context->get<cst::IdentDef>();
    if (identDef) {
        result += any_cast<string>(visitIdentDef(identDef)) + " of ";
    }
    result += any_cast<string>(visitType(context->type()));
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
    std::string above = commentsBefore(context);
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
        CAMEL_FORMAT(ModuleDecl)
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
        CAMEL_FORMAT(AnalysisDecl)
        CAMEL_FORMAT(AnalysisClause)
        CAMEL_FORMAT(RuleContext)
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
        CAMEL_FORMAT(CatchClause)
        CAMEL_FORMAT(MemberAccess)
        CAMEL_FORMAT(TypeList)
        CAMEL_FORMAT(IdentDef)
        CAMEL_FORMAT(IdentRef)
        CAMEL_FORMAT(SpreadValue)
        CAMEL_FORMAT(RestValue)
        CAMEL_FORMAT(WaitExpr)
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
        CAMEL_FORMAT(InterType)
        CAMEL_FORMAT(DiffType)
        CAMEL_FORMAT(KeyUnionDiffType)
        CAMEL_FORMAT(KeyInterType)
        CAMEL_FORMAT(TypeUnit)
        CAMEL_FORMAT(ArrayType)
        CAMEL_FORMAT(ArrayDim)
        CAMEL_FORMAT(SpecType)
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
    case NK::TryExpr:
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
        // `T { static for (n, F) in fields U => e }`: the shape is written as a type, and the
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
