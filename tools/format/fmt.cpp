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
 * Created: May. 17, 2024
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
    configure(options);
}

std::string Formatter::format() {
    return any_cast<string>(visitProgram(cst::Program(tree.root())));
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

void Formatter::insertComment(const FmtToken &comment, string &result) {
    size_t tokChannel = comment.channel;
    if (tokChannel == 0 || tokChannel > 4) {
        return;
    }
    const auto &commentText = comment.text();
    result += commentText;
    if (tokChannel == 4) {
        // block comment
        result += " ";
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
        headTokenRanges.push_back(declTokenRanges[headWithIndex[0].second]);
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
        // to be dropped.
        for (size_t i = 0; i < moduleDecl.firstToken().index(); i++) {
            if (tokens[i].channel > 1) {
                headStr += tokens[i].text() + lineEnd();
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
        return result + "{" + any_cast<string>(visitStmtList(stmtList)) + "}";
    } else {
        result += "{";
        const size_t firstTokIdx = context->firstToken()->index();
        const size_t lastTokIdx  = context->lastToken()->index();
        bool foundComment        = false;
        pushIndent();
        for (size_t i = firstTokIdx + 1; i < lastTokIdx; i++) {
            if (tokens[i].channel > 1) {
                result += lineEnd();
                insertComment(tokens[i], result);
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
        EXPORT? implMark? modifiers?

FUNC identRef parentParams (':' typeExpr)? stmtBlock ;
*/
any Formatter::visitFuncDecl(cst::FuncDecl context) {
    string result;
    const auto funcAnnos     = context->all<cst::FuncAnno>();
    const auto &angledParams = context->get<cst::AngledParams>();
    const auto &implMark     = context->get<cst::ImplMark>();
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

    if (implMark) {
        result += any_cast<string>(visitImplMark(implMark)) + " ";
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
    result += any_cast<string>(visitParentParams(parentParams));

    if (typeExpr) {
        result += ": " + any_cast<string>(visitType(typeExpr));
    }

    return result + " " + any_cast<string>(visitStmtBlock(stmtBlock));
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
analysisDecl : frontMatter? 'export'? IDENTIFIER identDef ':' typeExpr '{' analysisClause* '}'
*/
any Formatter::visitAnalysisDecl(cst::AnalysisDecl context) {
    string result;
    for (const auto &tagUse : context->all<cst::TagUse>()) {
        result += any_cast<string>(visitTagUse(tagUse)) + lineEnd();
    }
    result += context->tok(TK::KwExport) ? "export analysis " : "analysis ";
    result += any_cast<string>(visitIdentDef(context->get<cst::IdentDef>()));
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
typeDecl
    : implMark? TYPE identRef ('=' | '<' identDef (',' identDef)* ','? ('>' '=' | '>='))
      (typeExpr | STRING)
    ;
*/
any Formatter::visitTypeDecl(cst::TypeDecl context) {
    const auto &implMark = context->get<cst::ImplMark>();
    string result;
    for (const auto &tagUse : context->all<cst::TagUse>()) {
        result += any_cast<string>(visitTagUse(tagUse)) + lineEnd();
    }
    if (implMark) {
        result += any_cast<string>(visitImplMark(implMark)) + " ";
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
implMark    : INNER | OUTER ;
*/
any Formatter::visitImplMark(cst::ImplMark context) { return context->text(); }

/*
modifiers   : (ATOMIC | SHARED | SYNC | MACRO)+ ;
*/
any Formatter::visitModifiers(cst::Modifiers context) {
    string result;
    context->tokCount(TK::KwMacro) ? result += "macro " : result;
    context->tokCount(TK::KwAtomic) ? result += "atomic " : result;
    context->tokCount(TK::KwShared) ? result += "shared " : result;
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
keyParamPair : VAR? identDef ':' typeExpr ('=' dataExpr)? ;
*/
any Formatter::visitKeyParamPair(cst::KeyParamPair context) {
    string result = context->tok(TK::KwVar) ? "var " : context->tok(TK::KwConst) ? "const " : "";
    result += any_cast<string>(visitIdentDef(context->get<cst::IdentDef>()));
    const auto &typeExpr = context->type();
    const auto &dataExpr = context->expr();
    result += ": " + any_cast<string>(visitType(typeExpr));
    if (dataExpr) {
        result += " = " + any_cast<string>(visitExpr(dataExpr));
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
any Formatter::visitPairedParams(cst::PairedParams context) {
    return formatList(
        vec(context->all<cst::KeyParamPair>()),
        context,
        ", ",
        ",",
        PaddingNL | PushScope);
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
indices : '[' dataExpr (':' dataExpr (':' dataExpr)?)? ']' ;
*/
any Formatter::visitIndices(cst::Indices context) {
    string result = "[" + any_cast<string>(visitExpr(context->expr(0)));
    if (context->childCount() > 3) {
        result += ": " + any_cast<string>(visitExpr(context->expr(1)));
        if (context->childCount() > 6) {
            result += ": " + any_cast<string>(visitExpr(context->expr(2)));
        }
    }
    return result + "]";
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
    return "match type " + any_cast<string>(visitType(context->type())) + " {" +
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
        string result    = "if " + condition + " then " + thenBlock;
        if (context->tok(TK::KwElse)) {
            string elseBlock = any_cast<string>(visitBlockExpr(context->child(5).node()));
            if (elseBlock.starts_with("if ") && !thenBlock.ends_with("}")) {
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
    : '.' (IDENTIFIER | INTEGER | REAL)
    ;
*/
any Formatter::visitMemberAccess(cst::MemberAccess context) {
    return "." + context->child(1)->text();
}

/*
structData
    : '{' (pairedValues ','?)? '}' // no list comprehension because the struct is immutable
    ;
*/
any Formatter::visitStructData(cst::StructData context) {
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
    ;
*/
any Formatter::visitTupleData(cst::TupleData context) {
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
    : specType ('[' ']')*
    ;
*/
any Formatter::visitArrayType(cst::ArrayType context) {
    string result     = any_cast<string>(visitType(context->type()));
    const size_t size = (context->childCount() - 1) / 2;
    for (size_t i = 0; i < size; i++) {
        result += "[]";
    }
    return result;
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
    return result + " => " + any_cast<string>(visitType(context->type()));
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
        CAMEL_FORMAT(AnalysisDecl)
        CAMEL_FORMAT(AnalysisClause)
        CAMEL_FORMAT(RuleContext)
        CAMEL_FORMAT(RuleBody)
        CAMEL_FORMAT(DataDecl)
        CAMEL_FORMAT(TypeDecl)
        CAMEL_FORMAT(RetStmt)
        CAMEL_FORMAT(ImplMark)
        CAMEL_FORMAT(Modifiers)
        CAMEL_FORMAT(ParentIdents)
        CAMEL_FORMAT(BracedIdents)
        CAMEL_FORMAT(BracketIdents)
        CAMEL_FORMAT(KeyTypePair)
        CAMEL_FORMAT(KeyValuePair)
        CAMEL_FORMAT(KeyParamPair)
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
        CAMEL_FORMAT(TypeCase)
        CAMEL_FORMAT(TypeCasePattern)
        CAMEL_FORMAT(CatchClause)
        CAMEL_FORMAT(MemberAccess)
        CAMEL_FORMAT(TypeList)
        CAMEL_FORMAT(IdentDef)
        CAMEL_FORMAT(IdentRef)
        CAMEL_FORMAT(SpreadValue)
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
    case NK::TypeAsType:
        return "typeas " + any_cast<string>(visitIdentDef(context.get<cst::IdentDef>()));
    case NK::ConstasData:
        return "constas " + any_cast<string>(visitIdentDef(context.get<cst::IdentDef>()));
    default:
        throw std::runtime_error(
            "camel-format: no rule for " + std::string(syn::nodeKindName(context.kind())));
    }
}
