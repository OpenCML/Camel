/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Updated: Oct. 19, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "fmt.h"
#include <algorithm>

using namespace std;

string Formatter::formatStringLiteral(const string &input) {
    char quoteChar = (quotePrefer == QuotePreference::Single) ? '\'' : '"';
    const string slicedStr = input.substr(1, input.size() - 2);

    bool hasSingleQuote = (slicedStr.find('\'') != string::npos);
    bool hasDoubleQuote = (slicedStr.find('"') != string::npos);

    if (hasSingleQuote && !hasDoubleQuote) {
        quoteChar = '"';
    } else if (!hasSingleQuote && hasDoubleQuote) {
        quoteChar = '\'';
    } else {
        // escape all quotes
        string escapedStr;
        for (const char &c : slicedStr) {
            if (c == quoteChar) {
                escapedStr += '\\';
            }
            escapedStr += c;
        }
        return quoteChar + escapedStr + quoteChar;
    }

    return quoteChar + slicedStr + quoteChar;
}

inline bool isMultiLine(const antlr4::ParserRuleContext *context) {
    const size_t firstTokenLine = context->getStart()->getLine();
    size_t secondTokenLine = 0;
    // get second token
    const auto secondToken = context->children[1];
    if (antlr4::ParserRuleContext::is(secondToken)) {
        const antlr4::ParserRuleContext *secondTokenContext = dynamic_cast<antlr4::ParserRuleContext *>(secondToken);
        secondTokenLine = secondTokenContext->getStart()->getLine();
    } else {
        secondTokenLine = context->getStop()->getLine();
    }
    return firstTokenLine != secondTokenLine;
}

void Formatter::insertComment(antlr4::Token *comment, string &result) {
    const auto &commentText = comment->getText();
    switch (comment->getChannel()) {
    case 2: {
        if (cmtPrefer == CommentPreference::Slash)
            result += hash2slash(commentText);
        else
            result += commentText;
    } break;
    case 3: {
        if (cmtPrefer == CommentPreference::Hash)
            result += slash2hash(commentText);
        else
            result += commentText;
    } break;
    case 4:
        result += commentText + " ";
        break;

    default:
        throw runtime_error("Trying to insert a comment that is not from a comment channel");
    }
};

/*
program : stmtList? EOF;
*/
any Formatter::visitProgram(OpenCMLParser::ProgramContext *context) {
    const auto &stmtList = context->stmtList();
    const string code = stmtList ? any_cast<string>(visitStmtList(stmtList, false, true, true)) : "";
    // remove first newline character
    return code.substr(newline.size());
};

/*
stmtList : stmt (SEP? stmt)* SEP? ;
*/
any Formatter::visitStmtList(OpenCMLParser::StmtListContext *context, bool padding, bool forceMultiLine,
                             bool trailingComma) {
    vector<OpenCMLParser::StmtContext *> head, tail;
    for (const auto &stmt : context->stmt()) {
        if (stmt->useStmt()) {
            head.push_back(stmt);
        } else {
            tail.push_back(stmt);
        }
    }
    sort(head.begin(), head.end(), [](OpenCMLParser::StmtContext *a, OpenCMLParser::StmtContext *b) {
        return a->useStmt()->STRING()->getText() < b->useStmt()->STRING()->getText();
    });
    string headStr = formatList(head, "; ", (preferSemis ? ";" : ""), trailingComma, padding, forceMultiLine, 1);
    string tailStr = formatList(tail, "; ", (preferSemis ? ";" : ""), trailingComma, padding, forceMultiLine);
    return headStr + (head.empty() || tail.empty() ? "" : lineEnd()) + tailStr;
};

/*
stmt
    : letStmt
    | useStmt
    | typeStmt
    | exprStmt
    | waitStmt
    | funcDef
    | retStmt
    ;
*/
any Formatter::visitStmt(OpenCMLParser::StmtContext *context) { return visit(context->children[0]); };

/*
letStmt    : (LET | VAR) carrier (':' typeExpr)? '=' entityExpr ;
*/
any Formatter::visitLetStmt(OpenCMLParser::LetStmtContext *context) {
    string result;
    const string &letOrVar = context->children[0]->getText();
    const auto &carrier = context->carrier();
    const auto &typeExpr = context->typeExpr();
    const auto &entityExpr = context->entityExpr();
    result += letOrVar + " " + any_cast<string>(visitCarrier(carrier));
    if (typeExpr) {
        result += ": " + any_cast<string>(visitTypeExpr(typeExpr));
    }
    if (entityExpr) {
        result += " = " + any_cast<string>(visitEntityExpr(entityExpr));
    }
    return result;
};

/*
useStmt    : USE (identRef | bracedIdents | '*') FROM STRING ;
*/
any Formatter::visitUseStmt(OpenCMLParser::UseStmtContext *context) {
    string result = "use ";
    const auto &identRef = context->identRef();
    if (identRef) {
        result += any_cast<string>(visitIdentRef(identRef));
    } else {
        const auto &bracedIdents = context->bracedIdents();
        if (bracedIdents) {
            result += any_cast<string>(visitBracedIdents(bracedIdents));
        } else {
            result += "*";
        }
    }
    result += " from " + formatStringLiteral(context->STRING()->getText());
    return result;
};

/*
typeStmt : TYPE identRef '=' typeExpr ;
*/
any Formatter::visitTypeStmt(OpenCMLParser::TypeStmtContext *context) {
    string result;
    const auto &identRef = context->identRef();
    const auto &typeExpr = context->typeExpr();
    result += "type " + any_cast<string>(visitIdentRef(identRef));
    if (typeExpr) {
        if (context->children.size() == 4) {
            result += " =";
        }
        result += " " + any_cast<string>(visitTypeExpr(typeExpr));
    }
    return result;
};

/*
exprStmt : annotations? entityExpr ;
*/
any Formatter::visitExprStmt(OpenCMLParser::ExprStmtContext *context) {
    string result;
    const auto &annotations = context->annotations();
    if (annotations) {
        result += any_cast<string>(visitAnnotations(annotations, true)) + lineEnd();
    }
    return result + any_cast<string>(visitEntityExpr(context->entityExpr()));
};

/*
waitStmt   : WAIT entityExpr ;
*/
any Formatter::visitWaitStmt(OpenCMLParser::WaitStmtContext *context) {
    return "wait " + any_cast<string>(visitEntityExpr(context->entityExpr()));
};

/*
withDef : WITH angledParams ;
*/
any Formatter::visitWithDef(OpenCMLParser::WithDefContext *context) {
    return "with " + any_cast<string>(visitAngledParams(context->angledParams()));
};

/*
funcDecl   : annotations? withDef? modifiers? FUNC identRef parentParams (':' typeExpr)? ;
*/
std::any Formatter::visitFuncDecl(OpenCMLParser::FuncDeclContext *context) {
    string result;
    const auto &annotations = context->annotations();
    const auto &withDef = context->withDef();
    const auto &modifiers = context->modifiers();
    const auto &identRef = context->identRef();
    const auto &parentParams = context->parentParams();
    const auto &typeExpr = context->typeExpr();
    if (annotations) {
        result += any_cast<string>(visitAnnotations(annotations, true)) + lineEnd();
    }
    if (withDef) {
        result += any_cast<string>(visitWithDef(withDef)) + lineEnd();
    }
    if (modifiers) {
        result += any_cast<string>(visitModifiers(modifiers)) + " ";
    }
    result += "func " + any_cast<string>(visitIdentRef(identRef)) + any_cast<string>(visitParentParams(parentParams));
    if (typeExpr) {
        result += ": " + any_cast<string>(visitTypeExpr(typeExpr));
    }
    return result;
}

/*
funcDef    : funcDecl bracedStmts ;
*/
any Formatter::visitFuncDef(OpenCMLParser::FuncDefContext *context) {
    string result;
    const auto &funcDecl = context->funcDecl();
    const auto &bracedStmts = context->bracedStmts();
    return result + any_cast<string>(visitFuncDecl(funcDecl)) + " " + any_cast<string>(visitBracedStmts(bracedStmts));
};

/*
retStmt : RETURN entityExpr? ;
*/
any Formatter::visitRetStmt(OpenCMLParser::RetStmtContext *context) {
    string result = "return";
    const auto &entityExpr = context->entityExpr();
    if (entityExpr) {
        result += " " + any_cast<string>(visitEntityExpr(entityExpr));
    }
    return result;
};

/*
lambdaExpr : modifiers? angledParams? parentParams (':' typeExpr)? '=>' (bracedStmts | entityExpr) ;
*/
any Formatter::visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) {
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

    const auto &typeExpr = context->typeExpr();
    if (typeExpr) {
        result += ": " + any_cast<string>(visitTypeExpr(typeExpr));
    }

    result += " => ";

    const auto &bracedStmts = context->bracedStmts();
    if (bracedStmts) {
        result += any_cast<string>(visitBracedStmts(bracedStmts));
    } else {
        const auto &entityExpr = context->entityExpr();
        result += any_cast<string>(visitEntityExpr(entityExpr));
    }

    return result;
};

/*
carrier    : identRef | bracedIdents | bracketIdents ;
*/
any Formatter::visitCarrier(OpenCMLParser::CarrierContext *context) { return visit(context->children[0]); };

/*
annotation  : '@' primaryExpr ;
*/
any Formatter::visitAnnotation(OpenCMLParser::AnnotationContext *context) {
    return "@" + any_cast<string>(visitPrimaryExpr(context->primaryExpr()));
};

/*
annotations : annotation+ ;
*/
any Formatter::visitAnnotations(OpenCMLParser::AnnotationsContext *context, bool multiLine) {
    string result;
    const auto &annotations = context->annotation();
    for (int i = 0; i < annotations.size(); i++) {
        if (i > 0 && multiLine) {
            result += lineEnd();
        }
        result += any_cast<string>(visitAnnotation(annotations[i]));
    }
    return result;
};

/*
modifiers   : (INNER | OUTER | ATOMIC | SHARED | SYNC)+ ;
*/
any Formatter::visitModifiers(OpenCMLParser::ModifiersContext *context) {
    string result;
    (context->INNER()).size() ? result += "inner " : result;
    (context->OUTER()).size() ? result += "outer " : result;
    (context->ATOMIC()).size() ? result += "atomic " : result;
    (context->SHARED()).size() ? result += "shared " : result;
    (context->SYNC()).size() ? result += "sync " : result;
    // remove trailing space
    if (!result.empty()) {
        result.pop_back();
    }
    return result;
};

/*
keyTypePair  : identRef ':' typeExpr ;
*/
any Formatter::visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) {
    return any_cast<string>(visitIdentRef(context->identRef())) + ": " +
           any_cast<string>(visitTypeExpr(context->typeExpr()));
};

/*
keyValuePair : identRef ':' entityExpr ;
*/
any Formatter::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) {
    return context->identRef()->getText() + ": " + any_cast<string>(visitEntityExpr(context->entityExpr()));
};

/*
keyParamPair : VAR? identRef annotation? ':' typeExpr ('=' entityExpr)? ;
*/
any Formatter::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) {
    string result = context->VAR() ? "var " : "";
    result += any_cast<string>(visitIdentRef(context->identRef()));
    const auto &annotation = context->annotation();
    const auto &typeExpr = context->typeExpr();
    const auto &entityExpr = context->entityExpr();
    if (annotation) {
        result += any_cast<string>(visitAnnotation(annotation));
    }
    result += ": " + any_cast<string>(visitTypeExpr(typeExpr));
    if (entityExpr) {
        result += " = " + any_cast<string>(visitEntityExpr(entityExpr));
    }
    return result;
};

/*
indexKTPair  : '[' typeExpr ']' ':' typeExpr ;
*/
any Formatter::visitIndexKTPair(OpenCMLParser::IndexKTPairContext *context) {
    const auto &typeExprs = context->typeExpr();
    return "[" + any_cast<string>(visitTypeExpr(typeExprs[0])) + "]: " + any_cast<string>(visitTypeExpr(typeExprs[1]));
};

/*
indexKVPair  : '[' entityExpr ']' ':' entityExpr ;
*/
any Formatter::visitIndexKVPair(OpenCMLParser::IndexKVPairContext *context) {
    return "[" + any_cast<string>(visitEntityExpr(context->entityExpr(0))) +
           "]: " + any_cast<string>(visitEntityExpr(context->entityExpr(1)));
};

/*
typeList     : typeExpr (',' typeExpr)* ;
*/
any Formatter::visitTypeList(OpenCMLParser::TypeListContext *context, bool trailingComma, bool padding,
                             bool forceMultiLine) {
    return formatList(context->typeExpr(), ", ", ",", trailingComma, padding, forceMultiLine);
};

/*
identList    : identRef (',' identRef)* ;
*/
any Formatter::visitIdentList(OpenCMLParser::IdentListContext *context, bool trailingComma, bool padding,
                              bool forceMultiLine) {
    return formatList(context->identRef(), ", ", ",", trailingComma, padding, forceMultiLine);
};

/*
valueList    : entityExpr (',' entityExpr)* ;
*/
any Formatter::visitValueList(OpenCMLParser::ValueListContext *context, bool trailingComma, bool padding,
                              bool forceMultiLine) {
    return formatList(context->entityExpr(), ", ", ",", trailingComma, padding, forceMultiLine);
};

/*
pairedTypes  : keyTypePair (',' keyTypePair)* ;
*/
any Formatter::visitPairedTypes(OpenCMLParser::PairedTypesContext *context, bool trailingComma, bool padding,
                                bool forceMultiLine) {
    return formatList(context->keyTypePair(), ", ", ",", trailingComma, padding, forceMultiLine);
};

/*
pairedValues : keyValuePair (',' keyValuePair)* ;
*/
any Formatter::visitPairedValues(OpenCMLParser::PairedValuesContext *context, bool trailingComma, bool padding,
                                 bool forceMultiLine) {
    return formatList(context->keyValuePair(), ", ", ",", trailingComma, padding, forceMultiLine);
};

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
any Formatter::visitPairedParams(OpenCMLParser::PairedParamsContext *context, bool trailingComma, bool padding,
                                 bool forceMultiLine) {
    return formatList(context->keyParamPair(), ", ", ",", trailingComma, padding, forceMultiLine);
};

/*
indexKVPairs : indexKVPair (',' indexKVPair)* ;
*/
any Formatter::visitIndexKVPairs(OpenCMLParser::IndexKVPairsContext *context, bool trailingComma, bool padding,
                                 bool forceMultiLine) {
    return formatList(context->indexKVPair(), ", ", ",", trailingComma, padding, forceMultiLine);
}

/*
argumentList : valueList (',' pairedValues)? | pairedValues ;
*/
any Formatter::visitArgumentList(OpenCMLParser::ArgumentListContext *context, bool trailingComma, bool padding,
                                 bool forceMultiLine) {
    const auto &valueList = context->valueList();
    const auto &pairedValues = context->pairedValues();
    if (valueList && pairedValues) {
        return any_cast<string>(visitValueList(valueList, true, false, forceMultiLine)) +
               any_cast<string>(visitPairedValues(pairedValues, trailingComma, false, forceMultiLine));
    } else if (valueList) {
        return any_cast<string>(visitValueList(valueList, trailingComma, false, forceMultiLine));
    } else {
        return any_cast<string>(visitPairedValues(pairedValues, trailingComma, false, forceMultiLine));
    }
};

/*
bracedPairedValues : '{' pairedValues? ','? '}' ;
*/
any Formatter::visitBracedPairedValues(OpenCMLParser::BracedPairedValuesContext *context) {
    const auto &pairedValues = context->pairedValues();
    const bool hasComma = context->children.size() > 2 + (pairedValues ? 1 : 0);
    return "{" +
           (pairedValues ? any_cast<string>(visitPairedValues(pairedValues, hasComma, true, isMultiLine(context)))
                         : "") +
           "}";
};

/*
bracedIdents       : '{' identList? ','? '}' ;
*/
any Formatter::visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) {
    const auto &identList = context->identList();
    const bool hasComma = context->children.size() > 2 + (identList ? 1 : 0);
    return "{" + (identList ? any_cast<string>(visitIdentList(identList, hasComma, true, isMultiLine(context))) : "") +
           "}";
};

/*
bracedStmts        : '{' stmtList? '}' ;
*/
any Formatter::visitBracedStmts(OpenCMLParser::BracedStmtsContext *context) {
    const auto &stmtList = context->stmtList();
    const bool hasComma = stmtList && stmtList->getStop()->getText() == ";";
    if (stmtList) {
        return "{" + any_cast<string>(visitStmtList(stmtList, true, isMultiLine(context), hasComma)) + "}";
    } else {
        // check if there are comments inside the braces where there are no statements
        string result = "{";
        const size_t firstTokIdx = context->getStart()->getTokenIndex();
        const size_t lastTokIdx = context->getStop()->getTokenIndex();
        bool foundComment = false;
        pushIndent();
        for (int i = firstTokIdx + 1; i < lastTokIdx; i++) {
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
};

/*
bracedValues       : '{' valueList? ','? '}' ;
*/
any Formatter::visitBracedValues(OpenCMLParser::BracedValuesContext *context) {
    const auto &valueList = context->valueList();
    const bool hasComma = context->children.size() > 2 + (valueList ? 1 : 0);
    return "{" + (valueList ? any_cast<string>(visitValueList(valueList, hasComma, true, isMultiLine(context))) : "") +
           "}";
};

/*
bracedIndexKVPairs : '{' indexKVPairs? ','? '}' ;
*/
any Formatter::visitBracedIndexKVPairs(OpenCMLParser::BracedIndexKVPairsContext *context) {
    const auto &indexKVPairs = context->indexKVPairs();
    const bool hasComma = context->children.size() > 2 + (indexKVPairs ? 1 : 0);
    return "{" +
           (indexKVPairs ? any_cast<string>(visitIndexKVPairs(indexKVPairs, hasComma, true, isMultiLine(context)))
                         : "") +
           "}";
};

/*
bracketIdents : '[' identList? ','? ']' ;
*/
any Formatter::visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) {
    const auto &identList = context->identList();
    const bool hasComma = context->children.size() > 2 + (identList ? 1 : 0);
    return "[" + (identList ? any_cast<string>(visitIdentList(identList, hasComma, false, isMultiLine(context))) : "") +
           "]";
};

/*
bracketValues : '[' valueList? ','? ']' ;
*/
any Formatter::visitBracketValues(OpenCMLParser::BracketValuesContext *context) {
    const auto &valueList = context->valueList();
    const bool hasComma = context->children.size() > 2 + (valueList ? 1 : 0);
    return "[" + (valueList ? any_cast<string>(visitValueList(valueList, hasComma, false, isMultiLine(context))) : "") +
           "]";
};

/*
memberAccess : '[' entityExpr ']' ;
*/
any Formatter::visitMemberAccess(OpenCMLParser::MemberAccessContext *context) {
    return "[" + any_cast<string>(visitEntityExpr(context->entityExpr())) + "]";
};

/*
parentParams : '(' pairedParams? ','? ')' ;
*/
any Formatter::visitParentParams(OpenCMLParser::ParentParamsContext *context) {
    const auto &pairedParams = context->pairedParams();
    const bool hasComma = context->children.size() > 2 + (pairedParams ? 1 : 0);
    return "(" +
           (pairedParams ? any_cast<string>(visitPairedParams(pairedParams, hasComma, false, isMultiLine(context)))
                         : "") +
           ")";
};

/*
parentArgues       : '(' argumentList? ','? ')' ;
*/
any Formatter::visitParentArgues(OpenCMLParser::ParentArguesContext *context) {
    const auto &argumentList = context->argumentList();
    const bool hasComma = context->children.size() > 2 + (argumentList ? 1 : 0);
    return "(" +
           (argumentList ? any_cast<string>(visitArgumentList(argumentList, hasComma, false, isMultiLine(context)))
                         : "") +
           ")";
};

/*
parentValues       : '(' valueList? ','? ')' ;
*/
any Formatter::visitParentValues(OpenCMLParser::ParentValuesContext *context) {
    const auto &valueList = context->valueList();
    const bool hasComma = context->children.size() > 2 + (valueList ? 1 : 0);
    return "(" + (valueList ? any_cast<string>(visitValueList(valueList, hasComma, false, isMultiLine(context))) : "") +
           ")";
};

/*
angledParams       : '<' pairedParams? ','? '>' ;
*/
any Formatter::visitAngledParams(OpenCMLParser::AngledParamsContext *context) {
    const auto &pairedParams = context->pairedParams();
    const bool hasComma = context->children.size() > 2 + (pairedParams ? 1 : 0);
    return "<" +
           (pairedParams ? any_cast<string>(visitPairedParams(pairedParams, hasComma, false, isMultiLine(context)))
                         : "") +
           ">";
};

/*
angledValues       : '<' argumentList? ','? '>' ;
*/
any Formatter::visitAngledValues(OpenCMLParser::AngledValuesContext *context) {
    const auto &argumentList = context->argumentList();
    const bool hasComma = context->children.size() > 2 + (argumentList ? 1 : 0);
    return "<" +
           (argumentList ? any_cast<string>(visitArgumentList(argumentList, hasComma, false, isMultiLine(context)))
                         : "") +
           ">";
};

/*
entityExpr
    : ternaryExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '&=' | '|=') ternaryExpr)?
    ;
*/
any Formatter::visitEntityExpr(OpenCMLParser::EntityExprContext *context) {
    string result = any_cast<string>(visitTernaryExpr(context->ternaryExpr(0)));
    if (context->children.size() > 1) {
        result +=
            " " + context->children[1]->getText() + " " + any_cast<string>(visitTernaryExpr(context->ternaryExpr(1)));
    }
    return result;
};

/*
ternaryExpr
    : logicalOrExpr ('?' ternaryExpr ':' ternaryExpr)?
    ;
*/
any Formatter::visitTernaryExpr(OpenCMLParser::TernaryExprContext *context) {
    string result = any_cast<string>(visitLogicalOrExpr(context->logicalOrExpr()));
    if (context->children.size() > 1) {
        result += " ? " + any_cast<string>(visitTernaryExpr(context->ternaryExpr(0))) + " : " +
                  any_cast<string>(visitTernaryExpr(context->ternaryExpr(1)));
    }
    return result;
};

/*
logicalOrExpr
    : logicalAndExpr ('||' logicalAndExpr)*
    ;
*/
any Formatter::visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) {
    return formatList(context->logicalAndExpr(), " || ", " ||", false, false, false);
};

/*
logicalAndExpr
    : equalityExpr ('&&' equalityExpr)*
    ;
*/
any Formatter::visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) {
    return formatList(context->equalityExpr(), " && ", " &&", false, false, false);
};

/*
equalityExpr
    : relationalExpr (('==' | '!=') relationalExpr)*
    ;
*/
any Formatter::visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) {
    string result = any_cast<string>(visitRelationalExpr(context->relationalExpr(0)));
    for (int i = 1; i < context->relationalExpr().size(); i++) {
        result += " " + context->children[i * 2 - 1]->getText() + " " +
                  any_cast<string>(visitRelationalExpr(context->relationalExpr(i)));
    }
    return result;
};

/*
relationalExpr
    : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
    ;
*/
any Formatter::visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) {
    string result = any_cast<string>(visitAdditiveExpr(context->additiveExpr(0)));
    for (int i = 1; i < context->additiveExpr().size(); i++) {
        result += " " + context->children[i * 2 - 1]->getText() + " " +
                  any_cast<string>(visitAdditiveExpr(context->additiveExpr(i)));
    }
    return result;
};

/*
additiveExpr
    : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
    ;
*/
any Formatter::visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) {
    string result = any_cast<string>(visitMultiplicativeExpr(context->multiplicativeExpr(0)));
    for (int i = 1; i < context->multiplicativeExpr().size(); i++) {
        result += " " + context->children[i * 2 - 1]->getText() + " " +
                  any_cast<string>(visitMultiplicativeExpr(context->multiplicativeExpr(i)));
    }
    return result;
};

/*
multiplicativeExpr
    : unaryExpr (('^' | '*' | '/' | '%' | AS | IS) unaryExpr)*
    ;
*/
any Formatter::visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) {
    string result = any_cast<string>(visitUnaryExpr(context->unaryExpr(0)));
    for (int i = 1; i < context->unaryExpr().size(); i++) {
        result += " " + context->children[i * 2 - 1]->getText() + " " +
                  any_cast<string>(visitUnaryExpr(context->unaryExpr(i)));
    }
    return result;
};

/*
unaryExpr
    : linkExpr
    | ('!' | '-' | '~') linkExpr
    ;
*/
any Formatter::visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) {
    if (context->children.size() == 1) {
        return visitLinkExpr(context->linkExpr());
    } else {
        return context->children[0]->getText() + any_cast<string>(visitLinkExpr(context->linkExpr()));
    }
};

/*
linkExpr
    : withExpr ('->' withExpr)*
    ;
*/
any Formatter::visitLinkExpr(OpenCMLParser::LinkExprContext *context) {
    string result = any_cast<string>(visitWithExpr(context->withExpr(0)));
    for (int i = 1; i < context->withExpr().size(); i++) {
        result += "->" + any_cast<string>(visitWithExpr(context->withExpr(i)));
    }
    return result;
};

/*
withExpr
    : annotatedExpr ('.' annotatedExpr)*
    ;
*/
any Formatter::visitWithExpr(OpenCMLParser::WithExprContext *context) {
    string result = any_cast<string>(visitAnnotatedExpr(context->annotatedExpr(0)));
    for (int i = 1; i < context->annotatedExpr().size(); i++) {
        result += "." + any_cast<string>(visitAnnotatedExpr(context->annotatedExpr(i)));
    }
    return result;
};

/*
annotatedExpr
    : primaryExpr (memberAccess | parentArgues | angledValues | annotation)*
    ;
*/
any Formatter::visitAnnotatedExpr(OpenCMLParser::AnnotatedExprContext *context) {
    string result = any_cast<string>(visitPrimaryExpr(context->primaryExpr()));
    for (int i = 1; i < context->children.size(); i++) {
        result += any_cast<string>(visit(context->children[i]));
    }
    return result;
};

/*
primaryExpr
    : identRef
    | literal
    | bracketValues         // for list
    | bracedPairedValues    // for dict
    | '(' entityExpr ')'    // if there is only one entity, it will be recognized as a primary expression rather than a
tuple | parentValues          // for tuple
    \\ for vector | array | tensor | set | map
    | '<' typeExpr (',' (typeExpr | INTEGER | '[' INTEGER (',' INTEGER)* ']'))? '>' (bracketValues | bracedValues |
bracedIndexKVPairs) | lambdaExpr ;
*/
any Formatter::visitPrimaryExpr(OpenCMLParser::PrimaryExprContext *context) {
    const auto &alt = context->getAltNumber();
    if (alt == 5) { // '(' entityExpr ')'
        return "(" + any_cast<string>(visitEntityExpr(context->entityExpr())) + ")";
    } else if (alt == 7) {
        string result;
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() > 0) {
            result += "<" + any_cast<string>(visitTypeExpr(typeExpr[0]));
            const auto &children = context->children;
            if (children.size() > 4) {
                result += ", ";
                const auto &ints = context->INTEGER();
                if (children[3]->getText() == "[") {
                    result += "[";
                    for (int i = 0; i < ints.size(); i++) {
                        if (i > 0) {
                            result += ", ";
                        }
                        result += ints[i]->getText();
                    }
                    result += "]";
                } else if (ints.size() > 0) {
                    result += ints[0]->getText();
                } else {
                    result += any_cast<string>(visitTypeExpr(context->typeExpr(1)));
                }
            }
            result += ">";
        }
        return result + any_cast<string>(visit(context->children.back()));
    } else {
        return visit(context->children[0]);
    }
};

/*
literal
    : INTEGER UNIT?
    | REAL UNIT?
    | STRING
    | MULTI_STR
    | FSTRING
    | TRUE
    | FALSE
    | NULL
    ;
*/
any Formatter::visitLiteral(OpenCMLParser::LiteralContext *context) {
    const string &text = context->getText();
    if (context->STRING()) {
        return formatStringLiteral(text);
    } else {
        return text;
    }
};

/*
typeExpr
    : arrayType (('&' | '|' | '^') arrayType)*
    ;
*/
any Formatter::visitTypeExpr(OpenCMLParser::TypeExprContext *context) {
    string result = any_cast<string>(visitArrayType(context->arrayType(0)));
    for (int i = 1; i < context->arrayType().size(); i++) {
        result += " " + context->children[i * 2 - 1]->getText() + " " +
                  any_cast<string>(visitArrayType(context->arrayType(i)));
    }
    return result;
};

/*
arrayType
    : atomType ('[' INTEGER? ']')*
    ;
*/
any Formatter::visitArrayType(OpenCMLParser::ArrayTypeContext *context) {
    string result = any_cast<string>(visitAtomType(context->atomType()));
    for (int i = 1; i < context->children.size(); i++) {
        result += context->children[i]->getText();
    }
    return result;
};

/*
atomType
    : primaryType
    | structType
    | specialType
    | identRef
    | '(' typeExpr ')'
    | lambdaType
    ;
*/
any Formatter::visitAtomType(OpenCMLParser::AtomTypeContext *context) {
    if (context->getAltNumber() == 5) {
        return "(" + any_cast<string>(visitTypeExpr(context->typeExpr())) + ")";
    } else {
        return visit(context->children[0]);
    }
};

/*
lambdaType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;
*/
any Formatter::visitLambdaType(OpenCMLParser::LambdaTypeContext *context) {
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
};

/*
primaryType
    : INTEGER_TYPE
    | INTEGER32_TYPE
    | INTEGER64_TYPE
    | REAL_TYPE
    | FLOAT_TYPE
    | DOUBLE_TYPE
    | NUMBER_TYPE
    | STRING_TYPE
    | BOOL_TYPE
    | CHAR_TYPE
    ;
*/
any Formatter::visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) { return context->getText(); };

/*
structType
    : SET_TYPE ('<' typeExpr '>')?
    | MAP_TYPE ('<' typeExpr ',' typeExpr '>')?
    | LIST_TYPE // variable length, heterogeneous
    | DICT_TYPE // universal dict type
    | ARRAY_TYPE ('<' typeExpr (',' INTEGER)? '>')? // fixed length, homogenous
    | TUPLE_TYPE ('<' typeList? ','? '>')? // fixed length, heterogeneous
    | UNION_TYPE ('<' typeList? ','? '>')?
    | VECTOR_TYPE ('<' typeExpr '>')? // variable length, homogenous
    | TENSOR_TYPE ('<' typeExpr (',' '[' INTEGER (',' INTEGER)* ']')? '>')?
    | '{' pairedTypes? ','? '}' // concrete dict type
    | '{' indexKTPair '}' // concrete map type
    ;
*/
any Formatter::visitStructType(OpenCMLParser::StructTypeContext *context) {
    // return formatted string
    switch (context->getAltNumber()) {
    case 1: // SET_TYPE ('<' typeExpr '>')?
    {
        const auto &typeExpr = context->typeExpr(0);
        return typeExpr ? "Set<" + any_cast<string>(visitTypeExpr(typeExpr)) + ">" : string("Set");
    } break;
    case 2: // MAP_TYPE ('<' typeExpr ',' typeExpr '>')?
    {
        const auto &typeExpr0 = context->typeExpr(0);
        const auto &typeExpr1 = context->typeExpr(1);
        return typeExpr0 && typeExpr1 ? "Map<" + any_cast<string>(visitTypeExpr(typeExpr0)) + ", " +
                                            any_cast<string>(visitTypeExpr(typeExpr1)) + ">"
                                      : string("Map");
    } break;
    case 3: // LIST_TYPE
        return string("List");
        break;
    case 4: // DICT_TYPE
        return string("Dict");
        break;
    case 5: // ARRAY_TYPE ('<' typeExpr (',' INTEGER)? '>')?
    {
        const auto &typeExpr = context->typeExpr(0);
        const auto &integer = context->INTEGER(0);
        return typeExpr ? integer
                              ? "Array<" + any_cast<string>(visitTypeExpr(typeExpr)) + ", " + integer->getText() + ">"
                              : "Array<" + any_cast<string>(visitTypeExpr(typeExpr)) + ">"
                        : string("Array");
    } break;
    case 6: // TUPLE_TYPE ('<' typeList? ','? '>')?
    {
        const auto &typeList = context->typeList();
        return typeList ? "Tuple<" + any_cast<string>(visitTypeList(typeList, false, false, isMultiLine(context))) + ">"
                        : "Tuple";
    } break;
    case 7: // UNION_TYPE ('<' typeList? ','? '>')?
    {
        const auto &typeList = context->typeList();
        return typeList ? "Union<" + any_cast<string>(visitTypeList(typeList, false, false, isMultiLine(context))) + ">"
                        : "Union";
    } break;
    case 8: // VECTOR_TYPE ('<' typeExpr '>')?
    {
        const auto &typeExpr = context->typeExpr(0);
        return typeExpr ? "Vector<" + any_cast<string>(visitTypeExpr(typeExpr)) + ">" : "Vector";
    } break;
    case 9: // TENSOR_TYPE ('<' typeExpr (',' '[' INTEGER (',' INTEGER)* ']')? '>')?
    {
        const auto &typeExpr = context->typeExpr(0);
        const auto &integers = context->INTEGER();
        if (typeExpr) {
            if (integers.size() == 1) {
                return "Tensor<" + any_cast<string>(visitTypeExpr(typeExpr)) + ", [" + integers[0]->getText() + "]>";
            } else {
                string result = "Tensor<" + any_cast<string>(visitTypeExpr(typeExpr)) + ", [";
                for (const auto &integer : integers) {
                    result += integer->getText() + ", ";
                }
                if (integers.size() > 1) {
                    result.pop_back();
                    result.pop_back();
                }
                result += "]>";
                return result;
            }
        } else {
            return string("Tensor");
        }
    } break;
    case 10: // '{' pairedTypes? ','? '}'
    {
        const auto &pairedTypes = context->pairedTypes();
        return pairedTypes ? "{" + any_cast<string>(visitPairedTypes(pairedTypes)) + "}" : "{}";
    } break;
    case 11: // '{' indexKTPair '}'
    {
        return "{ " + any_cast<string>(visitIndexKTPair(context->indexKTPair())) + " }";
    } break;

    default:
        return string("");
        break;
    }
};

/*
specialType
    : ANY_TYPE
    | VOID_TYPE
    | FUNCTOR_TYPE
    ;
*/
any Formatter::visitSpecialType(OpenCMLParser::SpecialTypeContext *context) { return context->getText(); };

/*
identRef : IDENTIFIER ;
*/
any Formatter::visitIdentRef(OpenCMLParser::IdentRefContext *context) { return context->getText(); };
