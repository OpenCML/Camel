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
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "fmt.h"
#include <algorithm>
#include <regex>

using namespace std;

inline std::string trim(const std::string &str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

inline std::vector<std::string> regex_split(const std::string &input, const std::string &pattern) {
    std::regex re(pattern);
    std::sregex_token_iterator begin(input.begin(), input.end(), re, -1);
    std::sregex_token_iterator end;
    return {begin, end};
}

Formatter::Formatter(const syn::Tree &tree) : Formatter(tree, Options{}) {}

Formatter::Formatter(const syn::Tree &tree, Options options) : tree(tree) {
    // The tokens with their lines; comments are what the formatter puts back between nodes.
    const syn::TokenBuffer &buffer = tree.tokens();
    tokens.reserve(buffer.size());
    for (const syn::Token &token : buffer.tokens()) {
        int channel = 0;
        if (token.kind == TK::Command) {
            channel = 2;
        } else if (token.kind == TK::LineComment) {
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
    char quoteChar = (quotePrefer == QuotePreference::Single) ? '\'' : '"';
    const string slicedStr =
        multiLine ? input.substr(3, input.size() - 6) : input.substr(1, input.size() - 2);

    string procStr;

    if (multiLine) {
        string trimmedStr = trim(slicedStr);
        vector<string> lines;
        for (const string &line : regex_split(trimmedStr, "\\r?\\n\\s*")) {
            lines.push_back(trim(line));
        }
        for (size_t i = 0; i < lines.size(); i++) {
            procStr += lines[i];
            if (i != lines.size() - 1) {
                procStr += lineEnd();
            }
        }
    } else {
        procStr = slicedStr;
    }

    bool hasSingleQuote = (slicedStr.find('\'') != string::npos);
    bool hasDoubleQuote = (slicedStr.find('"') != string::npos);

    if (hasSingleQuote && hasDoubleQuote && !multiLine) {
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

    if (multiLine) {
        string multiQuote = string(3, quoteChar);
        return multiQuote + lineEnd() + procStr + lineEnd() + multiQuote;
    } else {
        return quoteChar + procStr + quoteChar;
    }
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

    size_t lastIndex = 0;
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
    tailStr = formatList(
        tail,
        context,
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
any Formatter::visitModuleName(cst::ModuleName context) {
    return context->text();
}

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
    string result            = "import ";
    const string path        = any_cast<string>(visitModuleName(context->get<cst::ModuleName>()));
    const auto &identDef     = context->get<cst::IdentDef>();
    const auto &bracedIdents = context->get<cst::BracedIdents>();
    if (context->tok(TK::KwFrom)) {
        if (identDef) {
            result += any_cast<string>(visitIdentDef(identDef));
        } else {
            result += any_cast<string>(visitBracedIdents(bracedIdents));
        }
        result += " from " + path;
    } else {
        result += path;
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
blockStmt  : WAIT? stmtBlock ;
*/
any Formatter::visitBlockStmt(cst::BlockStmt context) {
    string result = context->tok(TK::KwWait) ? "wait " : "";
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
funcAnno   : '@' identRef angledValues? ;
*/
any Formatter::visitFuncAnno(cst::FuncAnno context) {
    string result = "@";
    result += any_cast<string>(visitIdentRef(context->get<cst::IdentRef>()));
    if (context->get<cst::AngledValues>()) {
        result += any_cast<string>(visitAngledValues(context->get<cst::AngledValues>()));
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

    result += "func " + any_cast<string>(visitIdentRef(identRef)) +
              any_cast<string>(visitParentParams(parentParams));

    if (typeExpr) {
        result += ": " + any_cast<string>(visitType(typeExpr));
    }

    return result + " " + any_cast<string>(visitStmtBlock(stmtBlock));
}

/*
namespaceDecl : NAMESPACE identRef '{' SEP? (namespaceItem SEP?)* '}' ;
*/
any Formatter::visitNamespaceDecl(cst::NamespaceDecl context) {
    string result = "namespace " + any_cast<string>(visitIdentRef(context->get<cst::IdentRef>())) + " {";
    result +=
        formatList(itemsOf(context), context, "; ", "", PaddingNL | PushScope | Multiline);
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
               formatList(vec(identList->all<cst::IdentDef>()),
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
           (context->get<cst::IdentList>() ? any_cast<string>(visitIdentList(context->get<cst::IdentList>())) : "") +
           "]";
}


/*
dataDecl   : (LET | VAR) carrier (':' typeList)? '=' dataList ;
*/
any Formatter::visitDataDecl(cst::DataDecl context) {
    string result;
    const string &letOrVar = context->child(0)->text();
    const syn::Node carrier = context->child(1).node();
    const auto &typeList   = context->get<cst::TypeList>();
    const auto &dataList   = context->get<cst::DataList>();
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
useDecl    : USE (identDef '=')? identRef ;
*/
any Formatter::visitUseDecl(cst::UseDecl context) {
    string result        = "use ";
    const auto &identDef = context->get<cst::IdentDef>();
    if (identDef) {
        result += any_cast<string>(visitIdentDef(identDef)) + " = ";
    }
    return result + any_cast<string>(visitIdentRef(context->get<cst::IdentRef>()));
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
    return any_cast<string>(visitIdentDef(context->get<cst::IdentDef>())) + ": " +
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
    string result = context->tok(TK::KwVar) ? "var " : "";
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
    return formatList(vec(context->all<cst::IdentDef>()), context, ", ", ",", PaddingNL | PushScope);
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
    return formatList(vec(context->all<cst::KeyValuePair>()), context, ", ", ",", PaddingNL | PushScope);
}

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
any Formatter::visitPairedParams(cst::PairedParams context) {
    return formatList(vec(context->all<cst::KeyParamPair>()), context, ", ", ",", PaddingNL | PushScope);
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
           (context->get<cst::PairedParams>() ? any_cast<string>(visitPairedParams(context->get<cst::PairedParams>()))
                                    : "") +
           ")";
}

/*
parentArgues : '(' argumentList? ','? ')' ;
*/
any Formatter::visitParentArgues(cst::ParentArgues context) {
    return "(" +
           (context->get<cst::ArgumentList>() ? any_cast<string>(visitArgumentList(context->get<cst::ArgumentList>()))
                                    : "") +
           ")";
}

/*
angledParams : '<' pairedParams? ','? '>' ;
*/
any Formatter::visitAngledParams(cst::AngledParams context) {
    return "<" +
           (context->get<cst::PairedParams>() ? any_cast<string>(visitPairedParams(context->get<cst::PairedParams>()))
                                    : "") +
           ">";
}

/*
angledValues : '<' argumentList? ','? '>' ;
*/
any Formatter::visitAngledValues(cst::AngledValues context) {
    return "<" +
           (context->get<cst::ArgumentList>() ? any_cast<string>(visitArgumentList(context->get<cst::ArgumentList>()))
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
        // `error e`: the bound name is part of the pattern.
        string result = any_cast<string>(visitIdentRef(context->get<cst::IdentRef>()));
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
    | MATCH identRef '{' matchCase+ '}'
    | TRY stmtBlock catchClause+ (FINALLY stmtBlock)?
    ;
*/
any Formatter::visitCtrlExpr(syn::Node context) {
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
    case 2: // MATCH identRef '{' matchCase+ '}'
        return "match " + any_cast<string>(visitIdentRef(context->get<cst::IdentRef>())) + " {" +
               formatList(vec(context->all<cst::MatchCase>()),
                   context,
                   " ",
                   "",
                   PaddingNL | Multiline | PushScope) +
               "}";
        break;
    case 3: // TRY stmtBlock (CATCH identDef ':' typeExpr stmtBlock)+ (FINALLY stmtBlock)?
        return "try " + any_cast<string>(visitStmtBlock(context->get<cst::StmtBlock>(0))) +
               formatList(vec(context->all<cst::CatchClause>()), context, " ", "", PaddingSP | InOneLine) +
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
    return (context->tok(TK::KwWait) ? "wait " : "") +
           any_cast<string>(visitExpr(context->expr()));
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
        result += " " + context->child(1)->text() + " " +
                  any_cast<string>(visitExpr(context->expr(1)));
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
    // `- 1` negates the number 1; written together it would be the literal -1.
    if (op == "-" && !operand.empty() && std::isdigit(static_cast<unsigned char>(operand[0]))) {
        return op + " " + operand;
    }
    return op + operand;
}

/*
linkExpr
    : compExpr (('->' | '?->') compExpr)*
    ;
*/
any Formatter::visitLinkExpr(cst::LinkExpr context) {
    return formatBiOpsList(context, false);
}

/*
compExpr
    : annoExpr (('..' | '?..') annoExpr)*
    ;
*/
any Formatter::visitCompExpr(cst::CompExpr context) {
    return formatBiOpsList(context, false);
}

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
               formatList(vec(pairedValues->all<cst::KeyValuePair>()),
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
            string result         = "[" + any_cast<string>(visitExpr(dataExprs[0]));
            result += " for " + any_cast<string>(visitIdentRef(context->get<cst::IdentRef>())) + " in " +
                      any_cast<string>(visitExpr(dataExprs[1]));
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
    : unionType (('?' | '!')+ | '?' unionType)?
    ;
*/
any Formatter::visitTypeExpr(cst::TypeExpr context) {
    string result;
    result += any_cast<string>(visitType(context->type(0)));
    if (context->types().size() > 1) {
        result += " ? " + any_cast<string>(visitType(context->type(1)));
    } else {
        // The marks as written: `T?`, `T!`, `T?!`.
        for (size_t i = 1; i < context->childCount(); ++i) {
            result += context->child(i)->text();
        }
    }
    return result;
}

/*
unionType
    : interType ('|' interType)*
    ;
*/
any Formatter::visitUnionType(cst::UnionType context) {
    return formatBiOpsList(context, true);
}

/*
interType
    : diffType ('&' diffType)*
    ;
*/
any Formatter::visitInterType(cst::InterType context) {
    return formatBiOpsList(context, true);
}

/*
diffType
    : keyUnionDiffType ('\\' keyUnionDiffType)*
    ;
*/
any Formatter::visitDiffType(cst::DiffType context) {
    return formatBiOpsList(context, true);
}

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
typeList
    : typeExpr (',' typeExpr)*
    ;
*/
any Formatter::visitTypeList(cst::TypeList context) {
    return formatList(vec(context->types()), context, ", ", ",", PaddingNL | PushScope);
}

/*
tupleType
    : '(' typeList? ','? ')'
    ;
*/
any Formatter::visitTupleType(cst::TupleType context) {
    return "(" + (context->get<cst::TypeList>() ? any_cast<string>(visitTypeList(context->get<cst::TypeList>())) : "") +
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
    return context->tok(TK::Identifier)->text();
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
        CAMEL_FORMAT(FuncDecl)
        CAMEL_FORMAT(DataDecl)
        CAMEL_FORMAT(TypeDecl)
        CAMEL_FORMAT(UseDecl)
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
        CAMEL_FORMAT(UnionType)
        CAMEL_FORMAT(InterType)
        CAMEL_FORMAT(DiffType)
        CAMEL_FORMAT(KeyUnionDiffType)
        CAMEL_FORMAT(KeyInterType)
        CAMEL_FORMAT(TypeUnit)
        CAMEL_FORMAT(ArrayType)
        CAMEL_FORMAT(SpecType)
        CAMEL_FORMAT(ConstData)
        CAMEL_FORMAT(StructType)
        CAMEL_FORMAT(TupleType)
        CAMEL_FORMAT(FuncType)
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
    default:
        throw std::runtime_error(
            "camel-format: no rule for " + std::string(syn::nodeKindName(context.kind())));
    }
}
