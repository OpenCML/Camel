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
 * Updated: May. 01, 2025
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

inline bool isMultiLine(const antlr4::ParserRuleContext *context) {
    if (context->children.size() < 2) {
        return false;
    }
    const size_t firstTokenLine = context->getStart()->getLine();
    size_t secondTokenLine = 0;
    // get second token
    const auto secondToken = context->children[1];
    if (antlr4::ParserRuleContext::is(secondToken)) {
        const antlr4::ParserRuleContext *secondTokenContext =
            dynamic_cast<antlr4::ParserRuleContext *>(secondToken);
        secondTokenLine = secondTokenContext->getStart()->getLine();
    } else {
        secondTokenLine = context->getStop()->getLine();
    }
    return firstTokenLine != secondTokenLine;
}

void Formatter::insertComment(antlr4::Token *comment, string &result) {
    size_t tokChannel = comment->getChannel();
    if (tokChannel == 0 || tokChannel > 4) {
        return;
    }
    const auto &commentText = comment->getText();
    result += commentText;
    if (tokChannel == 4) {
        // block comment
        result += " ";
    }
};

/*
program : SEP? (decl SEP?)* EOF;
*/
any Formatter::visitProgram(OpenCMLParser::ProgramContext *context) {
    OpenCMLParser::DeclContext *moduleDecl = nullptr;
    vector<OpenCMLParser::DeclContext *> head, tail;
    vector<pair<OpenCMLParser::DeclContext *, size_t>> headWithIndex, tailWithIndex;
    vector<pair<size_t, size_t>> declTokenRanges, headTokenRanges, tailTokenRanges;

    size_t lastIndex = 0;
    for (size_t i = 0; i < context->decl().size(); i++) {
        auto child = context->decl(i);
        if (child->moduleDecl()) {
            moduleDecl = child;
        } else if (child->importDecl()) {
            headWithIndex.push_back({child, i});
        } else {
            tailWithIndex.push_back({child, i});
        }
        declTokenRanges.push_back({lastIndex, child->getStart()->getTokenIndex()});
        lastIndex = child->getStop()->getTokenIndex() + 1;
    }
    // here we set the last index to the end - 1 of the token stream
    // to exclude the EOF token
    declTokenRanges.push_back({lastIndex, tokens.size() - 1});

    sort(
        headWithIndex.begin(),
        headWithIndex.end(),
        [](pair<OpenCMLParser::DeclContext *, size_t> a,
           pair<OpenCMLParser::DeclContext *, size_t>
               b) {
            return a.first->importDecl()->STRING()->getText() <
                   b.first->importDecl()->STRING()->getText();
        });

    if (!headWithIndex.empty()) {
        for (const auto &pair : headWithIndex) {
            head.push_back(pair.first);
            headTokenRanges.push_back(declTokenRanges[pair.second + 1]);
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
        headStr = any_cast<string>(visit(moduleDecl));
        if (this->preferSemis) {
            headStr += ";";
        }
        headStr += lineEnd(2);
    }
    // pass nullptr to avoid auto comment processing for empty head
    headStr += formatList(
        head,
        nullptr,
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
decl
    : moduleDecl
    | importDecl
    | exportDecl
    | dataDecl
    | funcDecl
    | typeDecl
    | useDecl
    ;
*/
any Formatter::visitDecl(OpenCMLParser::DeclContext *context) {
    return visit(context->children[0]);
}

/*
stmt
    : dataDecl
    | funcDecl
    | typeDecl
    | dataExpr
    | useDecl
    | retStmt
    | blockStmt
    ;
*/
any Formatter::visitStmt(OpenCMLParser::StmtContext *context) {
    return visit(context->children[0]);
}

/*
stmtList : stmt (SEP? stmt)* SEP? ;
*/
any Formatter::visitStmtList(OpenCMLParser::StmtListContext *context) {
    return formatList(context->stmt(), context, "; ", "", PaddingNL | PushScope | Multiline);
}

/*
moduleDecl : MODULE identDef ;
*/
any Formatter::visitModuleDecl(OpenCMLParser::ModuleDeclContext *context) {
    return "module " + any_cast<string>(visitIdentDef(context->identDef()));
}

/*
importDecl : IMPORT (STRING | (identDef | bracedIdents) FROM STRING) ;
*/
any Formatter::visitImportDecl(OpenCMLParser::ImportDeclContext *context) {
    string result = "import ";
    const string path = context->STRING()->getText();
    const auto &identDef = context->identDef();
    const auto &bracedIdents = context->bracedIdents();
    if (context->FROM()) {
        if (identDef) {
            result += any_cast<string>(visitIdentDef(identDef));
        } else {
            result += any_cast<string>(visitBracedIdents(bracedIdents));
        }
        result += " from " + formatStringLiteral(path);
    } else {
        result += formatStringLiteral(path);
    }
    return result;
}

/*
exportDecl : EXPORT (dataDecl | typeDecl | bracedIdents) ;
*/
any Formatter::visitExportDecl(OpenCMLParser::ExportDeclContext *context) {
    string result = "export ";
    const auto &dataDecl = context->dataDecl();
    const auto &typeDecl = context->typeDecl();
    const auto &bracedIdents = context->bracedIdents();
    if (dataDecl) {
        return result + any_cast<string>(visitDataDecl(dataDecl));
    } else if (typeDecl) {
        return result + any_cast<string>(visitTypeDecl(typeDecl));
    } else {
        return result + any_cast<string>(visitBracedIdents(bracedIdents));
    }
}

/*
blockStmt  : WAIT? stmtBlock ;
*/
any Formatter::visitBlockStmt(OpenCMLParser::BlockStmtContext *context) {
    string result = context->WAIT() ? "wait " : "";
    result += any_cast<string>(visitStmtBlock(context->stmtBlock()));
    return result;
}

/*
stmtBlock  : SYNC? '{' stmtList? '}' ;
*/
any Formatter::visitStmtBlock(OpenCMLParser::StmtBlockContext *context) {
    const auto &stmtList = context->stmtList();
    string result = context->SYNC() ? "sync " : "";
    if (stmtList) {
        return result + "{" + any_cast<string>(visitStmtList(stmtList)) + "}";
    } else {
        result += "{";
        const size_t firstTokIdx = context->getStart()->getTokenIndex();
        const size_t lastTokIdx = context->getStop()->getTokenIndex();
        bool foundComment = false;
        pushIndent();
        for (size_t i = firstTokIdx + 1; i < lastTokIdx; i++) {
            if (tokens[i]->getChannel() > 1) {
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
any Formatter::visitBlockExpr(OpenCMLParser::BlockExprContext *context) {
    return context->stmtBlock() ? any_cast<string>(visitStmtBlock(context->stmtBlock()))
                                : any_cast<string>(visitDataExpr(context->dataExpr()));
}

/*
funcData   : modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr ;
*/
any Formatter::visitFuncData(OpenCMLParser::FuncDataContext *context) {
    string result;
    const auto &modifiers = context->modifiers();
    const auto &angledParams = context->angledParams();
    const auto &parentParams = context->parentParams();
    const auto &blockExpr = context->blockExpr();

    if (modifiers) {
        result += any_cast<string>(visitModifiers(modifiers)) + " ";
    }

    if (angledParams) {
        result += any_cast<string>(visitAngledParams(angledParams));
    }
    result += any_cast<string>(visitParentParams(parentParams));

    const auto &typeExpr = context->typeExpr();
    if (typeExpr) {
        result += ": " + any_cast<string>(visitTypeExpr(typeExpr));
    }

    result += " => ";

    return result + any_cast<string>(visitBlockExpr(blockExpr));
}

/*
funcDecl   :
        (WITH angledParams)?
        EXPORT? implMark? modifiers?
        FUNC identDef parentParams (':' typeExpr)? stmtBlock ;
*/
any Formatter::visitFuncDecl(OpenCMLParser::FuncDeclContext *context) {
    string result;
    const auto &angledParams = context->angledParams();
    const auto &implMark = context->implMark();
    const auto &modifiers = context->modifiers();
    const auto &identDef = context->identDef();
    const auto &parentParams = context->parentParams();
    const auto &typeExpr = context->typeExpr();
    const auto &stmtBlock = context->stmtBlock();

    if (angledParams) {
        result += "with " + any_cast<string>(visitAngledParams(angledParams)) + lineEnd();
    }

    if (context->EXPORT()) {
        result += "export ";
    }

    if (implMark) {
        result += any_cast<string>(visitImplMark(implMark)) + " ";
    }

    if (modifiers) {
        result += any_cast<string>(visitModifiers(modifiers)) + " ";
    }

    result += "func " + any_cast<string>(visitIdentDef(identDef)) +
              any_cast<string>(visitParentParams(parentParams));

    if (typeExpr) {
        result += ": " + any_cast<string>(visitTypeExpr(typeExpr));
    }

    return result + " " + any_cast<string>(visitStmtBlock(stmtBlock));
}

/*
parentIdents  : '(' identList? ','? ')' ;
*/
any Formatter::visitParentIdents(OpenCMLParser::ParentIdentsContext *context) {
    const auto &identList = context->identList();
    return "(" + (identList ? any_cast<string>(visitIdentList(identList)) : "") + ")";
}

/*
bracedIdents  : '{' identList? ','? '}' ;
*/
any Formatter::visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) {
    const auto &identList = context->identList();
    if (identList) {
        return "{" +
               formatList(
                   identList->identDef(),
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
any Formatter::visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) {
    return "[" +
           (context->identList() ? any_cast<string>(visitIdentList(context->identList())) : "") +
           "]";
}

/*
carrier       : identList | parentIdents | bracedIdents | bracketIdents ;
*/
any Formatter::visitCarrier(OpenCMLParser::CarrierContext *context) {
    return visit(context->children[0]);
}

/*
dataDecl   : (LET | VAR) carrier (':' typeList)? '=' dataList ;
*/
any Formatter::visitDataDecl(OpenCMLParser::DataDeclContext *context) {
    string result;
    const string &letOrVar = context->children[0]->getText();
    const auto &carrier = context->carrier();
    const auto &typeList = context->typeList();
    const auto &dataList = context->dataList();
    result += letOrVar + " " + any_cast<string>(visitCarrier(carrier));
    if (typeList) {
        result += ": " + any_cast<string>(visitTypeList(typeList));
    }
    if (dataList) {
        result += " = " + any_cast<string>(visitDataList(dataList));
    }
    return result;
}

/*
typeDecl   : implMark? TYPE identDef '=' (typeExpr | STRING) ;
*/
any Formatter::visitTypeDecl(OpenCMLParser::TypeDeclContext *context) {
    const auto &implMark = context->implMark();
    string result;
    if (implMark) {
        result += any_cast<string>(visitImplMark(implMark)) + " ";
    }
    result += "type " + any_cast<string>(visitIdentDef(context->identDef())) + " = ";
    if (context->typeExpr()) {
        result += any_cast<string>(visitTypeExpr(context->typeExpr()));
    } else {
        result += formatStringLiteral(context->STRING()->getText());
    }
    return result;
}

/*
useDecl    : USE (identDef '=')? identRef ;
*/
any Formatter::visitUseDecl(OpenCMLParser::UseDeclContext *context) {
    string result = "use ";
    const auto &identDef = context->identDef();
    if (identDef) {
        result += any_cast<string>(visitIdentDef(identDef)) + " = ";
    }
    return result + any_cast<string>(visitIdentRef(context->identRef()));
}

/*
retStmt    : (RETURN | RAISE | THROW) dataList ;
*/
any Formatter::visitRetStmt(OpenCMLParser::RetStmtContext *context) {
    return context->children[0]->getText() + " " +
           any_cast<string>(visitDataList(context->dataList()));
}

/*
implMark    : INNER | OUTER ;
*/
any Formatter::visitImplMark(OpenCMLParser::ImplMarkContext *context) { return context->getText(); }

/*
modifiers   : (ATOMIC | SHARED | SYNC | MACRO)+ ;
*/
any Formatter::visitModifiers(OpenCMLParser::ModifiersContext *context) {
    string result;
    (context->MACRO()).size() ? result += "macro " : result;
    (context->ATOMIC()).size() ? result += "atomic " : result;
    (context->SHARED()).size() ? result += "shared " : result;
    (context->SYNC()).size() ? result += "sync " : result;
    // remove trailing space
    if (!result.empty()) {
        result.pop_back();
    }
    return result;
}

/*
indexValue   : '...'? dataExpr ;
*/
any Formatter::visitIndexValue(OpenCMLParser::IndexValueContext *context) {
    if (context->children.size() == 2) {
        return "..." + any_cast<string>(visitDataExpr(context->dataExpr()));
    } else {
        return any_cast<string>(visitDataExpr(context->dataExpr()));
    }
}

/*
keyTypePair  : identDef ':' typeExpr ;
*/
any Formatter::visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) {
    return any_cast<string>(visitIdentDef(context->identDef())) + ": " +
           any_cast<string>(visitTypeExpr(context->typeExpr()));
}

/*
keyValuePair : identDef ':' dataExpr | '...' dataExpr ;
*/
any Formatter::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) {
    if (context->identDef()) {
        return any_cast<string>(visitIdentDef(context->identDef())) + ": " +
               any_cast<string>(visitDataExpr(context->dataExpr()));
    } else {
        return "..." + any_cast<string>(visitDataExpr(context->dataExpr()));
    }
}

/*
keyParamPair : VAR? identDef ':' typeExpr ('=' dataExpr)? ;
*/
any Formatter::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) {
    string result = context->VAR() ? "var " : "";
    result += any_cast<string>(visitIdentDef(context->identDef()));
    const auto &typeExpr = context->typeExpr();
    const auto &dataExpr = context->dataExpr();
    result += ": " + any_cast<string>(visitTypeExpr(typeExpr));
    if (dataExpr) {
        result += " = " + any_cast<string>(visitDataExpr(dataExpr));
    }
    return result;
}

/*
dataList     : dataExpr (',' dataExpr)* ;
*/
any Formatter::visitDataList(OpenCMLParser::DataListContext *context) {
    return formatList(context->dataExpr(), context, ", ", ",", PaddingNL | PushScope);
}

/*
identList    : identDef (',' identDef)* ;
*/
any Formatter::visitIdentList(OpenCMLParser::IdentListContext *context) {
    return formatList(context->identDef(), context, ", ", ",", PaddingNL | PushScope);
}

/*
indexValues  : indexValue (',' indexValue)* ;
*/
any Formatter::visitIndexValues(OpenCMLParser::IndexValuesContext *context) {
    return formatList(context->indexValue(), context, ", ", ",", PaddingNL | PushScope);
}

/*
pairedValues : keyValuePair (',' keyValuePair)* ;
*/
any Formatter::visitPairedValues(OpenCMLParser::PairedValuesContext *context) {
    return formatList(context->keyValuePair(), context, ", ", ",", PaddingNL | PushScope);
}

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
any Formatter::visitPairedParams(OpenCMLParser::PairedParamsContext *context) {
    return formatList(context->keyParamPair(), context, ", ", ",", PaddingNL | PushScope);
}

/*
argumentList : indexValues (',' pairedValues)? | pairedValues ;
*/
any Formatter::visitArgumentList(OpenCMLParser::ArgumentListContext *context) {
    const auto &indexValues = context->indexValues();
    const auto &pairedValues = context->pairedValues();
    string result;
    if (indexValues) {
        result += formatList(indexValues->indexValue(), context, ", ", ",", PaddingNL | PushScope);
    }
    if (pairedValues) {
        result +=
            formatList(pairedValues->keyValuePair(), context, ", ", ",", PaddingNL | PushScope);
    }
    return result;
}

/*
indices : '[' dataExpr (':' dataExpr (':' dataExpr)?)? ']' ;
*/
any Formatter::visitIndices(OpenCMLParser::IndicesContext *context) {
    string result = "[" + any_cast<string>(visitDataExpr(context->dataExpr(0)));
    if (context->children.size() > 3) {
        result += ": " + any_cast<string>(visitDataExpr(context->dataExpr(1)));
        if (context->children.size() > 6) {
            result += ": " + any_cast<string>(visitDataExpr(context->dataExpr(2)));
        }
    }
    return result + "]";
}

/*
parentParams : '(' pairedParams? ','? ')' ;
*/
any Formatter::visitParentParams(OpenCMLParser::ParentParamsContext *context) {
    return "(" +
           (context->pairedParams() ? any_cast<string>(visitPairedParams(context->pairedParams()))
                                    : "") +
           ")";
}

/*
parentArgues : '(' argumentList? ','? ')' ;
*/
any Formatter::visitParentArgues(OpenCMLParser::ParentArguesContext *context) {
    return "(" +
           (context->argumentList() ? any_cast<string>(visitArgumentList(context->argumentList()))
                                    : "") +
           ")";
}

/*
angledParams : '<' pairedParams? ','? '>' ;
*/
any Formatter::visitAngledParams(OpenCMLParser::AngledParamsContext *context) {
    return "<" +
           (context->pairedParams() ? any_cast<string>(visitPairedParams(context->pairedParams()))
                                    : "") +
           ">";
}

/*
angledValues : '<' argumentList? ','? '>' ;
*/
any Formatter::visitAngledValues(OpenCMLParser::AngledValuesContext *context) {
    return "<" +
           (context->argumentList() ? any_cast<string>(visitArgumentList(context->argumentList()))
                                    : "") +
           ">";
}

/*
pattern
    : identRef
    | literal
    | '(' (dataList | identList)? ','? ')'
    | '{' (pairedValues | identList)? ','? '}'
    | '_' // wildcard
    ;
*/
any Formatter::visitPattern(OpenCMLParser::PatternContext *context) {
    switch (context->getAltNumber()) {
    case 1: // identRef
        return visitIdentRef(context->identRef());
        break;
    case 2: // literal
        return visitLiteral(context->literal());
        break;
    case 3: // '(' (valueList | identList)? ','? ')'
        return "(" +
               (context->dataList() ? any_cast<string>(visitDataList(context->dataList()))
                                    : any_cast<string>(visitIdentList(context->identList()))) +
               ")";
        break;
    case 4: // '{' (pairedValues | identList)? ','? '}'
        return "{" +
               (context->pairedValues()
                    ? any_cast<string>(visitPairedValues(context->pairedValues()))
                    : any_cast<string>(visitIdentList(context->identList()))) +
               "}";
        break;
    case 5: // '_'
        return string("_");
        break;

    default:
        throw runtime_error("Invalid pattern context");
    }
}

/*
matchCase
    : CASE pattern ('|' pattern)* '=>' blockExpr
    ;
*/
any Formatter::visitMatchCase(OpenCMLParser::MatchCaseContext *context) {
    return "case " + formatList(context->pattern(), context, " | ", " |") + " => " +
           any_cast<string>(visitBlockExpr(context->blockExpr()));
}

/*
catchClause
    : CATCH identDef ':' typeExpr stmtBlock
    ;
*/
any Formatter::visitCatchClause(OpenCMLParser::CatchClauseContext *context) {
    return "catch " + any_cast<string>(visitIdentDef(context->identDef())) + ": " +
           any_cast<string>(visitTypeExpr(context->typeExpr())) + " " +
           any_cast<string>(visitStmtBlock(context->stmtBlock()));
}

/*
ctrlExpr
    : IF logicalOrExpr THEN blockExpr (ELSE blockExpr)?
    | MATCH identRef '{' matchCase+ '}'
    | TRY stmtBlock catchClause+ (FINALLY stmtBlock)?
    ;
*/
any Formatter::visitCtrlExpr(OpenCMLParser::CtrlExprContext *context) {
    switch (context->getAltNumber()) {
    case 1: // IF logicalOrExpr THEN blockExpr (ELSE blockExpr)?
    {
        string result = "if " + any_cast<string>(visitLogicalOrExpr(context->logicalOrExpr())) +
                        " then " + any_cast<string>(visitBlockExpr(context->blockExpr(0)));
        if (context->ELSE()) {
            result += " else " + any_cast<string>(visitBlockExpr(context->blockExpr(1)));
        }
        return result;
        break;
    }
    case 2: // MATCH identRef '{' matchCase+ '}'
        return "match " + any_cast<string>(visitIdentRef(context->identRef())) + " {" +
               formatList(
                   context->matchCase(),
                   context,
                   " ",
                   "",
                   PaddingNL | Multiline | PushScope) +
               "}";
        break;
    case 3: // TRY stmtBlock (CATCH identDef ':' typeExpr stmtBlock)+ (FINALLY stmtBlock)?
        return "try " + any_cast<string>(visitStmtBlock(context->stmtBlock(0))) +
               formatList(context->catchClause(), context, " ", "", PaddingSP | InOneLine) +
               (context->FINALLY()
                    ? "finally " + any_cast<string>(visitStmtBlock(context->stmtBlock(1)))
                    : "");
        break;

    default:
        throw runtime_error("Invalid structExpr context");
    }
}

/*
dataExpr
    : waitExpr
    | ctrlExpr
    ;
*/
any Formatter::visitDataExpr(OpenCMLParser::DataExprContext *context) {
    return visit(context->children[0]);
}

/*
waitExpr : WAIT? assignExpr ;
*/
any Formatter::visitWaitExpr(OpenCMLParser::WaitExprContext *context) {
    return (context->WAIT() ? "wait " : "") +
           any_cast<string>(visitAssignExpr(context->assignExpr()));
}

/*
assignExpr
    : logicalOrExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&=' | '|=')
logicalOrExpr)?
    ;
*/
any Formatter::visitAssignExpr(OpenCMLParser::AssignExprContext *context) {
    string result = any_cast<string>(visitLogicalOrExpr(context->logicalOrExpr(0)));
    if (context->children.size() > 2) {
        result += " " + context->children[1]->getText() + " " +
                  any_cast<string>(visitLogicalOrExpr(context->logicalOrExpr(1)));
    }
    return result;
}

/*
logicalOrExpr
    : logicalAndExpr ('||' logicalAndExpr)*
    ;
*/
any Formatter::visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) {
    return formatBiOpsList(context->logicalAndExpr(), context->children, true);
}

/*
logicalAndExpr
    : equalityExpr ('&&' equalityExpr)*
    ;
*/
any Formatter::visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) {
    return formatBiOpsList(context->equalityExpr(), context->children, true);
}

/*
equalityExpr
    : relationalExpr (('===' | '!==' | '==' | '!=') relationalExpr)*
    ;
*/
any Formatter::visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) {
    return formatBiOpsList(context->relationalExpr(), context->children, true);
}

/*
relationalExpr
    : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
    ;
*/
any Formatter::visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) {
    return formatBiOpsList(context->additiveExpr(), context->children, true);
}

/*
additiveExpr
    : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
    ;
*/
any Formatter::visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) {
    return formatBiOpsList(context->multiplicativeExpr(), context->children, true);
}

/*
multiplicativeExpr
    : nullableExpr (('*' | '/' | '^' | '@' | '%') nullableExpr)*
    ;
*/
any Formatter::visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) {
    return formatBiOpsList(context->nullableExpr(), context->children, true);
}

/*
nullableExpr
    : unaryExpr (('??' | '!!') dataExpr)?
    ;
*/
any Formatter::visitNullableExpr(OpenCMLParser::NullableExprContext *context) {
    string result = any_cast<string>(visitUnaryExpr(context->unaryExpr()));
    if (context->children.size() > 1) {
        return result + " " + context->children[1]->getText() + " " +
               any_cast<string>(visitDataExpr(context->dataExpr()));
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
any Formatter::visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) {
    string result = any_cast<string>(visitLinkExpr(context->linkExpr()));
    if (context->getAltNumber() == 1) {
        if (context->typeExpr()) {
            return result + " " + context->children[1]->getText() + " " +
                   any_cast<string>(visitTypeExpr(context->typeExpr()));
        } else {
            return result;
        }
    } else {
        return context->children[0]->getText() + result;
    }
}

/*
linkExpr
    : bindExpr (('->' | '?->') bindExpr)*
    ;
*/
any Formatter::visitLinkExpr(OpenCMLParser::LinkExprContext *context) {
    return formatBiOpsList(context->bindExpr(), context->children, false);
}

/*
bindExpr
    : annoExpr (('..' | '?..') annoExpr)*
    ;
*/
any Formatter::visitBindExpr(OpenCMLParser::BindExprContext *context) {
    return formatBiOpsList(context->annoExpr(), context->children, false);
}

/*
annoExpr
    : withExpr ({isAdjacent()}? (indices | parentArgues | angledValues | '!'))*
    ;
*/
any Formatter::visitAnnoExpr(OpenCMLParser::AnnoExprContext *context) {
    string result = any_cast<string>(visitWithExpr(context->withExpr()));
    for (size_t i = 1; i < context->children.size(); i++) {
        const auto &child = context->children[i];
        if (antlr4::tree::TerminalNode::is(child)) {
            antlr4::tree::TerminalNode *terminalNode =
                dynamic_cast<antlr4::tree::TerminalNode *>(child);
            result += terminalNode->getText();
        } else {
            result += any_cast<string>(visit(child));
        }
    }
    return result;
}

/*
withExpr
    : accessExpr (('.' | '?.') accessExpr)*
    ;
*/
any Formatter::visitWithExpr(OpenCMLParser::WithExprContext *context) {
    return formatBiOpsList(context->accessExpr(), context->children, false);
}

/*
accessExpr
    : primaryData ('.$' (IDENTIFIER | INTEGER))*
    ;
*/
any Formatter::visitAccessExpr(OpenCMLParser::AccessExprContext *context) {
    string result = any_cast<string>(visitPrimaryData(context->primaryData()));
    for (size_t i = 1; i < context->children.size(); i++) {
        const auto &child = context->children[i];
        if (antlr4::tree::TerminalNode::is(child)) {
            antlr4::tree::TerminalNode *terminalNode =
                dynamic_cast<antlr4::tree::TerminalNode *>(child);
            result += terminalNode->getText();
        } else {
            result += any_cast<string>(visit(child));
        }
    }
    return result;
}

/*
dictData
    : '{' (pairedValues ','?)? '}' // no list comprehension because the struct of dict is immutable
    ;
*/
any Formatter::visitDictData(OpenCMLParser::DictDataContext *context) {
    const auto &pairedValues = context->pairedValues();
    if (pairedValues) {
        return "{" +
               formatList(
                   pairedValues->keyValuePair(),
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
listData
    : '[' ((indexValues ','?) | dataExpr FOR identRef IN dataExpr (IF dataExpr)?)? ']'
    ;
*/
any Formatter::visitListData(OpenCMLParser::ListDataContext *context) {
    const auto &children = context->children;
    if (children.size() == 2) {
        return string("[]");
    } else {
        const auto &indexValues = context->indexValues();
        if (indexValues) {
            return "[" + any_cast<string>(visitIndexValues(indexValues)) + "]";
        } else {
            const auto &dataExprs = context->dataExpr();
            string result = "[" + any_cast<string>(visitDataExpr(dataExprs[0]));
            result += " for " + any_cast<string>(visitIdentRef(context->identRef())) + " in " +
                      any_cast<string>(visitDataExpr(dataExprs[1]));
            if (dataExprs.size() > 2) {
                result += " if " + any_cast<string>(visitDataExpr(dataExprs[2]));
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
any Formatter::visitTupleData(OpenCMLParser::TupleDataContext *context) {
    if (context->dataList()) {
        return "(" + any_cast<string>(visitDataList(context->dataList())) +
               (context->children.size() > 3 ? ", " : "") + ")";
    } else {
        return string("()");
    }
}

/*
primaryData
    : identRef
    | literal
    | listData
    | dictData
    | '(' dataExpr ')'
    | tupleData
    | funcData
    ;
*/
any Formatter::visitPrimaryData(OpenCMLParser::PrimaryDataContext *context) {
    const auto &children = context->children;
    if (children.size() == 1) {
        return visit(children[0]);
    } else {
        return "(" + any_cast<string>(visit(children[1])) + ")";
    }
}

/*
literal
    : INTEGER
    | REAL
    | STRING
    | MULTI_STR
    | FSTRING
    | TRUE
    | FALSE
    | NULL
    ;
*/
any Formatter::visitLiteral(OpenCMLParser::LiteralContext *context) {
    if (context->STRING()) {
        return formatStringLiteral(context->STRING()->getText());
    } else if (context->MULTI_STR()) {
        return formatStringLiteral(context->MULTI_STR()->getText(), true);
    } else if (context->FSTRING()) {
        // TODO: format f-string
        return context->getText();
    } else {
        return context->getText();
    }
}

/*
typeExpr
    : unionType ('?' unionType?)?
    ;
*/
any Formatter::visitTypeExpr(OpenCMLParser::TypeExprContext *context) {
    string result;
    result += any_cast<string>(visitUnionType(context->unionType(0)));
    if (context->children.size() > 1) {
        if (context->unionType().size() > 1) {
            result += " ? " + any_cast<string>(visitUnionType(context->unionType(1)));
        } else {
            result += "?";
        }
    }
    return result;
}

/*
unionType
    : interType ('|' interType)*
    ;
*/
any Formatter::visitUnionType(OpenCMLParser::UnionTypeContext *context) {
    return formatBiOpsList(context->interType(), context->children, true);
}

/*
interType
    : diffType ('&' diffType)*
    ;
*/
any Formatter::visitInterType(OpenCMLParser::InterTypeContext *context) {
    return formatBiOpsList(context->diffType(), context->children, true);
}

/*
diffType
    : keyUnionDiffType ('\\' keyUnionDiffType)*
    ;
*/
any Formatter::visitDiffType(OpenCMLParser::DiffTypeContext *context) {
    return formatBiOpsList(context->keyUnionDiffType(), context->children, true);
}

/*
keyUnionDiffType
    : keyInterType (('+' | '-') keyInterType)*
    ;
*/
any Formatter::visitKeyUnionDiffType(OpenCMLParser::KeyUnionDiffTypeContext *context) {
    return formatBiOpsList(context->keyInterType(), context->children, true);
}

/*
keyInterType
    : typeUnit ('^' typeUnit)*
    ;
*/
any Formatter::visitKeyInterType(OpenCMLParser::KeyInterTypeContext *context) {
    return formatBiOpsList(context->typeUnit(), context->children, true);
}

/*
typeUnit : (identDef OF)? listType ;
*/
any Formatter::visitTypeUnit(OpenCMLParser::TypeUnitContext *context) {
    string result;
    const auto &identDef = context->identDef();
    if (identDef) {
        result += any_cast<string>(visitIdentDef(identDef)) + " of ";
    }
    result += any_cast<string>(visitListType(context->listType()));
    return result;
}

/*
listType
    : specType ('[' ']')*
    ;
*/
any Formatter::visitListType(OpenCMLParser::ListTypeContext *context) {
    string result = any_cast<string>(visitSpecType(context->specType()));
    const size_t size = (context->children.size() - 1) / 2;
    for (size_t i = 0; i < size; i++) {
        result += "[]";
    }
    return result;
}

/*
typeOrData : typeExpr | CONST dataExpr ;
*/
any Formatter::visitTypeOrData(OpenCMLParser::TypeOrDataContext *context) {
    if (context->dataExpr()) {
        return "const " + any_cast<string>(visitDataExpr(context->dataExpr()));
    } else {
        return any_cast<string>(visitTypeExpr(context->typeExpr()));
    }
}

/*
specType
    : primaryType ('<' typeOrData (',' typeOrData)* '>')?
    ;
*/
any Formatter::visitSpecType(OpenCMLParser::SpecTypeContext *context) {
    string result;
    result += any_cast<string>(visitPrimaryType(context->primaryType()));
    const auto &children = context->children;
    if (children.size() > 2) {
        result += "<";
        result += formatList(context->typeOrData(), context, ", ", ",", PushScope);
        result += ">";
    }
    return result;
}

/*
primaryType
    : INNER_ATOM_TYPE
    | dictType
    | identRef
    | '(' typeExpr ')'
    | tupleType
    | funcType
    | TYPEOF dataExpr
    | TYPEAS identDef
    ;
*/
any Formatter::visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) {
    switch (context->getAltNumber()) {
    case 1: // INNER_ATOM_TYPE
        return context->INNER_ATOM_TYPE()->getText();
        break;
    case 2: // dictExprType
        return any_cast<string>(visitDictType(context->dictType()));
        break;
    case 3: // identRef
        return any_cast<string>(visitIdentRef(context->identRef()));
        break;
    case 4: // '(' typeExpr ')'
        return "(" + any_cast<string>(visitTypeExpr(context->typeExpr())) + ")";
        break;
    case 5: // tupleType
        return any_cast<string>(visitTupleType(context->tupleType()));
        break;
    case 6: // lambdaType
        return any_cast<string>(visitFuncType(context->funcType()));
        break;
    case 7: // TYPEOF waitExpr
        return "typeof " + any_cast<string>(visitDataExpr(context->dataExpr()));
        break;
    case 8: // TYPEAS identDef
        return "typeas " + any_cast<string>(visitIdentDef(context->identDef()));
        break;

    default:
        throw std::runtime_error("Invalid primaryType context");
    }
}

/*
dictType
    : '{' (keyTypePair (',' keyTypePair)*)? ','? '}'
    ;
*/
any Formatter::visitDictType(OpenCMLParser::DictTypeContext *context) {
    const auto &keyTypePairs = context->keyTypePair();
    return "{" + formatList(keyTypePairs, context, ", ", ",", PaddingNL | PaddingSP | PushScope) +
           "}";
}

/*
typeList
    : typeExpr (',' typeExpr)*
    ;
*/
any Formatter::visitTypeList(OpenCMLParser::TypeListContext *context) {
    return formatList(context->typeExpr(), context, ", ", ",", PaddingNL | PushScope);
}

/*
tupleType
    : '(' typeList? ','? ')'
    ;
*/
any Formatter::visitTupleType(OpenCMLParser::TupleTypeContext *context) {
    return "(" + (context->typeList() ? any_cast<string>(visitTypeList(context->typeList())) : "") +
           ")";
}

/*
funcType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;
*/
any Formatter::visitFuncType(OpenCMLParser::FuncTypeContext *context) {
    string result;
    const auto &modifiers = context->modifiers();
    const auto &angledParams = context->angledParams();
    const auto &parentParams = context->parentParams();
    if (modifiers) {
        result += any_cast<string>(visitModifiers(modifiers)) + " ";
    }
    if (angledParams) {
        result += any_cast<string>(visitAngledParams(angledParams));
    }
    result += any_cast<string>(visitParentParams(parentParams));
    return result + " => " + any_cast<string>(visitTypeExpr(context->typeExpr()));
}

/*
identDef : IDENTIFIER ;
*/
any Formatter::visitIdentDef(OpenCMLParser::IdentDefContext *context) {
    return context->IDENTIFIER()->getText();
}

/*
identRef : (IDENTIFIER '::')* IDENTIFIER ;
*/
any Formatter::visitIdentRef(OpenCMLParser::IdentRefContext *context) {
    string result;
    const auto &idents = context->IDENTIFIER();
    for (const auto &ident : idents) {
        result += ident->getText() + "::";
    }
    if (idents.size() > 0) {
        result.pop_back();
        result.pop_back();
    }
    return result;
}
