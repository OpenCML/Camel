/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 17, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "fmt.h"

std::string Formatter::formatStringLiteral(const std::string &input) {
    char quoteChar = (quotePrefer == QuotePreference::Single) ? '\'' : '"';
    const std::string slicedStr = input.substr(1, input.size() - 2);

    bool hasSingleQuote = (slicedStr.find('\'') != std::string::npos);
    bool hasDoubleQuote = (slicedStr.find('"') != std::string::npos);

    if (hasSingleQuote && !hasDoubleQuote) {
        quoteChar = '"';
    } else if (!hasSingleQuote && hasDoubleQuote) {
        quoteChar = '\'';
    } else {
        // escape all quotes
        std::string escapedStr;
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

void Formatter::insertComment(antlr4::Token *comment, std::string &result) {
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
        break;
    }
};

/*
program : stmtList? EOF;
*/
std::any Formatter::visitProgram(OpenCMLParser::ProgramContext *context) {
    const auto &stmtList = context->stmtList();
    const std::string code = stmtList ? std::any_cast<std::string>(visitStmtList(stmtList, false, true, true)) : "";
    // remove first newline character
    return code.substr(newline.size());
};

/*
stmtList : stmt (SEP stmt)* SEP? ;
*/
std::any Formatter::visitStmtList(OpenCMLParser::StmtListContext *context, bool padding, bool forceMultiLine,
                                  bool trailingComma) {
    return formatList(context->stmt(), context, trailingComma, ";", padding, forceMultiLine);
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
std::any Formatter::visitStmt(OpenCMLParser::StmtContext *context) { return visit(context->children[0]); };

/*
letStmt    : (LET | VAR) carrier (':' typeExpr)? '=' entityExpr ;
*/
std::any Formatter::visitLetStmt(OpenCMLParser::LetStmtContext *context) {
    std::string result;
    const std::string &letOrVar = context->children[0]->getText();
    const auto &carrier = context->carrier();
    const auto &typeExpr = context->typeExpr();
    const auto &entityExpr = context->entityExpr();
    result += letOrVar + " " + std::any_cast<std::string>(visitCarrier(carrier));
    if (typeExpr) {
        result += ": " + std::any_cast<std::string>(visitTypeExpr(typeExpr));
    }
    if (entityExpr) {
        result += " = " + std::any_cast<std::string>(visitEntityExpr(entityExpr));
    }
    return result;
};

/*
useStmt    : USE (identRef | bracedIdents | '*') FROM STRING ;
*/
std::any Formatter::visitUseStmt(OpenCMLParser::UseStmtContext *context) {
    std::string result = "use ";
    const auto &identRef = context->identRef();
    if (identRef) {
        result += std::any_cast<std::string>(visitIdentRef(identRef));
    } else {
        const auto &bracedIdents = context->bracedIdents();
        if (bracedIdents) {
            result += std::any_cast<std::string>(visitBracedIdents(bracedIdents));
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
std::any Formatter::visitTypeStmt(OpenCMLParser::TypeStmtContext *context) {
    std::string result;
    const auto &identRef = context->identRef();
    const auto &typeExpr = context->typeExpr();
    result += "type " + std::any_cast<std::string>(visitIdentRef(identRef));
    if (typeExpr) {
        if (context->children.size() == 4) {
            result += " =";
        }
        result += " " + std::any_cast<std::string>(visitTypeExpr(typeExpr));
    }
    return result;
};

/*
exprStmt : annotations? entityExpr ;
*/
std::any Formatter::visitExprStmt(OpenCMLParser::ExprStmtContext *context) {
    std::string result;
    const auto &annotations = context->annotations();
    if (annotations) {
        result += std::any_cast<std::string>(visitAnnotations(annotations, true)) + lineEnd();
    }
    return result + std::any_cast<std::string>(visitEntityExpr(context->entityExpr()));
};

/*
waitStmt   : WAIT entityExpr ;
*/
std::any Formatter::visitWaitStmt(OpenCMLParser::WaitStmtContext *context) {
    return "wait " + std::any_cast<std::string>(visitEntityExpr(context->entityExpr()));
};

/*
withDef : WITH angledParams ;
*/
std::any Formatter::visitWithDef(OpenCMLParser::WithDefContext *context) {
    return "with " + std::any_cast<std::string>(visitAngledParams(context->angledParams()));
};

/*
funcDef    : annotations? withDef? modifiers? FUNC identRef parentParams (':' typeExpr)? bracedStmts ;
*/
std::any Formatter::visitFuncDef(OpenCMLParser::FuncDefContext *context) {
    std::string result;
    const auto &annotations = context->annotations();
    const auto &withDef = context->withDef();
    const auto &modifiers = context->modifiers();
    const auto &identRef = context->identRef();
    const auto &parentParams = context->parentParams();
    const auto &typeExpr = context->typeExpr();
    const auto &bracedStmts = context->bracedStmts();
    if (annotations) {
        result += std::any_cast<std::string>(visitAnnotations(annotations, true)) + lineEnd();
    }
    if (withDef) {
        result += std::any_cast<std::string>(visitWithDef(withDef)) + lineEnd();
    }
    if (modifiers) {
        result += std::any_cast<std::string>(visitModifiers(modifiers)) + " ";
    }
    result += "func " + std::any_cast<std::string>(visitIdentRef(identRef)) +
              std::any_cast<std::string>(visitParentParams(parentParams));
    if (typeExpr) {
        result += ": " + std::any_cast<std::string>(visitTypeExpr(typeExpr));
    }
    return result + " " + std::any_cast<std::string>(visitBracedStmts(bracedStmts));
};

/*
retStmt : RETURN entityExpr? ;
*/
std::any Formatter::visitRetStmt(OpenCMLParser::RetStmtContext *context) {
    std::string result = "return";
    const auto &entityExpr = context->entityExpr();
    if (entityExpr) {
        result += " " + std::any_cast<std::string>(visitEntityExpr(entityExpr));
    }
    return result;
};

/*
lambdaExpr : modifiers? angledParams? parentParams (':' typeExpr)? '=>' (bracedStmts | entityExpr) ;
*/
std::any Formatter::visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) {
    std::string result;
    const auto &modifiers = context->modifiers();
    const auto &angledParams = context->angledParams();
    const auto &parentParams = context->parentParams();

    if (modifiers) {
        result += std::any_cast<std::string>(visitModifiers(modifiers)) + " ";
    }

    if (angledParams) {
        result += std::any_cast<std::string>(visitAngledParams(angledParams));
    }
    result += std::any_cast<std::string>(visitParentParams(parentParams));

    const auto &typeExpr = context->typeExpr();
    if (typeExpr) {
        result += ": " + std::any_cast<std::string>(visitTypeExpr(typeExpr));
    }

    result += " => ";

    const auto &bracedStmts = context->bracedStmts();
    if (bracedStmts) {
        result += std::any_cast<std::string>(visitBracedStmts(bracedStmts));
    } else {
        const auto &entityExpr = context->entityExpr();
        result += std::any_cast<std::string>(visitEntityExpr(entityExpr));
    }

    return result;
};

/*
carrier    : identRef | bracedIdents | bracketIdents ;
*/
std::any Formatter::visitCarrier(OpenCMLParser::CarrierContext *context) { return visit(context->children[0]); };

/*
annotation  : '@' primaryExpr ;
*/
std::any Formatter::visitAnnotation(OpenCMLParser::AnnotationContext *context) {
    return "@" + std::any_cast<std::string>(visitPrimaryExpr(context->primaryExpr()));
};

/*
annotations : annotation+ ;
*/
std::any Formatter::visitAnnotations(OpenCMLParser::AnnotationsContext *context, bool multiLine) {
    std::string result;
    const auto &annotations = context->annotation();
    for (int i = 0; i < annotations.size(); i++) {
        if (i > 0 && multiLine) {
            result += lineEnd();
        }
        result += std::any_cast<std::string>(visitAnnotation(annotations[i]));
    }
    return result;
};

/*
modifiers   : (INNER | OUTER | ATOMIC | STATIC)+ ;
*/
std::any Formatter::visitModifiers(OpenCMLParser::ModifiersContext *context) {
    std::string result;
    (context->INNER()).size() ? result += "inner " : result;
    (context->OUTER()).size() ? result += "outer " : result;
    (context->ATOMIC()).size() ? result += "atomic " : result;
    (context->STATIC()).size() ? result += "static " : result;
    // remove trailing space
    if (!result.empty()) {
        result.pop_back();
    }
    return result;
};

/*
keyTypePair  : identRef ':' typeExpr ;
*/
std::any Formatter::visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) {
    return std::any_cast<std::string>(visitIdentRef(context->identRef())) + ": " +
           std::any_cast<std::string>(visitTypeExpr(context->typeExpr()));
};

/*
keyValuePair : identRef ':' entityExpr ;
*/
std::any Formatter::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) {
    return context->identRef()->getText() + ": " + std::any_cast<std::string>(visitEntityExpr(context->entityExpr()));
};

/*
keyParamPair : VAR? identRef annotation? ':' typeExpr ('=' entityExpr)? ;
*/
std::any Formatter::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) {
    std::string result = context->VAR() ? "var " : "";
    result += std::any_cast<std::string>(visitIdentRef(context->identRef()));
    const auto &annotation = context->annotation();
    const auto &typeExpr = context->typeExpr();
    const auto &entityExpr = context->entityExpr();
    if (annotation) {
        result += std::any_cast<std::string>(visitAnnotation(annotation));
    }
    result += ": " + std::any_cast<std::string>(visitTypeExpr(typeExpr));
    if (entityExpr) {
        result += " = " + std::any_cast<std::string>(visitEntityExpr(entityExpr));
    }
    return result;
};

/*
indexKTPair  : '[' typeExpr ']' ':' typeExpr ;
*/
std::any Formatter::visitIndexKTPair(OpenCMLParser::IndexKTPairContext *context) {
    const auto &typeExprs = context->typeExpr();
    return "[" + std::any_cast<std::string>(visitTypeExpr(typeExprs[0])) +
           "]: " + std::any_cast<std::string>(visitTypeExpr(typeExprs[1]));
};

/*
indexKVPair  : '[' entityExpr ']' ':' entityExpr ;
*/
std::any Formatter::visitIndexKVPair(OpenCMLParser::IndexKVPairContext *context) {
    return "[" + std::any_cast<std::string>(visitEntityExpr(context->entityExpr(0))) +
           "]: " + std::any_cast<std::string>(visitEntityExpr(context->entityExpr(1)));
};

/*
typeList     : typeExpr (',' typeExpr)* ;
*/
std::any Formatter::visitTypeList(OpenCMLParser::TypeListContext *context, bool trailingComma, bool padding,
                                  bool forceMultiLine) {
    return formatList(context->typeExpr(), context, trailingComma, ",", padding, forceMultiLine);
};

/*
identList    : identRef (',' identRef)* ;
*/
std::any Formatter::visitIdentList(OpenCMLParser::IdentListContext *context, bool trailingComma, bool padding,
                                   bool forceMultiLine) {
    return formatList(context->identRef(), context, trailingComma, ",", padding, forceMultiLine);
};

/*
valueList    : entityExpr (',' entityExpr)* ;
*/
std::any Formatter::visitValueList(OpenCMLParser::ValueListContext *context, bool trailingComma, bool padding,
                                   bool forceMultiLine) {
    return formatList(context->entityExpr(), context, trailingComma, ",", padding, forceMultiLine);
};

/*
pairedTypes  : keyTypePair (',' keyTypePair)* ;
*/
std::any Formatter::visitPairedTypes(OpenCMLParser::PairedTypesContext *context, bool trailingComma, bool padding,
                                     bool forceMultiLine) {
    return formatList(context->keyTypePair(), context, trailingComma, ",", padding, forceMultiLine);
};

/*
pairedValues : keyValuePair (',' keyValuePair)* ;
*/
std::any Formatter::visitPairedValues(OpenCMLParser::PairedValuesContext *context, bool trailingComma, bool padding,
                                      bool forceMultiLine) {
    return formatList(context->keyValuePair(), context, trailingComma, ",", padding, forceMultiLine);
};

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
std::any Formatter::visitPairedParams(OpenCMLParser::PairedParamsContext *context, bool trailingComma, bool padding,
                                      bool forceMultiLine) {
    return formatList(context->keyParamPair(), context, trailingComma, ",", padding, forceMultiLine);
};

/*
indexKVPairs : indexKVPair (',' indexKVPair)* ;
*/
std::any Formatter::visitIndexKVPairs(OpenCMLParser::IndexKVPairsContext *context, bool trailingComma, bool padding,
                                      bool forceMultiLine) {
    return formatList(context->indexKVPair(), context, trailingComma, ",", padding, forceMultiLine);
}

/*
argumentList : valueList (',' pairedValues)? | pairedValues ;
*/
std::any Formatter::visitArgumentList(OpenCMLParser::ArgumentListContext *context, bool trailingComma, bool padding,
                                      bool forceMultiLine) {
    const auto &valueList = context->valueList();
    const auto &pairedValues = context->pairedValues();
    if (valueList && pairedValues) {
        return std::any_cast<std::string>(visitValueList(valueList, true, false, forceMultiLine)) +
               std::any_cast<std::string>(visitPairedValues(pairedValues, trailingComma, false, forceMultiLine));
    } else if (valueList) {
        return std::any_cast<std::string>(visitValueList(valueList, trailingComma, false, forceMultiLine));
    } else {
        return std::any_cast<std::string>(visitPairedValues(pairedValues, trailingComma, false, forceMultiLine));
    }
};

/*
bracedPairedValues : '{' pairedValues? ','? '}' ;
*/
std::any Formatter::visitBracedPairedValues(OpenCMLParser::BracedPairedValuesContext *context) {
    const auto &pairedValues = context->pairedValues();
    const bool hasComma = context->children.size() > 2 + (pairedValues ? 1 : 0);
    return "{" +
           (pairedValues
                ? std::any_cast<std::string>(visitPairedValues(pairedValues, hasComma, true, isMultiLine(context)))
                : "") +
           "}";
};

/*
bracedIdents       : '{' identList? ','? '}' ;
*/
std::any Formatter::visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) {
    const auto &identList = context->identList();
    const bool hasComma = context->children.size() > 2 + (identList ? 1 : 0);
    return "{" +
           (identList ? std::any_cast<std::string>(visitIdentList(identList, hasComma, true, isMultiLine(context)))
                      : "") +
           "}";
};

/*
bracedStmts        : '{' stmtList? '}' ;
*/
std::any Formatter::visitBracedStmts(OpenCMLParser::BracedStmtsContext *context) {
    const auto &stmtList = context->stmtList();
    const bool hasComma = stmtList && stmtList->getStop()->getText() == ";";
    if (stmtList) {
        return "{" + std::any_cast<std::string>(visitStmtList(stmtList, true, isMultiLine(context), hasComma)) + "}";
    } else {
        // check if there are comments inside the braces where there are no statements
        std::string result = "{";
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
std::any Formatter::visitBracedValues(OpenCMLParser::BracedValuesContext *context) {
    const auto &valueList = context->valueList();
    const bool hasComma = context->children.size() > 2 + (valueList ? 1 : 0);
    return "{" +
           (valueList ? std::any_cast<std::string>(visitValueList(valueList, hasComma, true, isMultiLine(context)))
                      : "") +
           "}";
};

/*
bracedIndexKVPairs : '{' indexKVPairs? ','? '}' ;
*/
std::any Formatter::visitBracedIndexKVPairs(OpenCMLParser::BracedIndexKVPairsContext *context) {
    const auto &indexKVPairs = context->indexKVPairs();
    const bool hasComma = context->children.size() > 2 + (indexKVPairs ? 1 : 0);
    return "{" +
           (indexKVPairs
                ? std::any_cast<std::string>(visitIndexKVPairs(indexKVPairs, hasComma, true, isMultiLine(context)))
                : "") +
           "}";
};

/*
bracketIdents : '[' identList? ','? ']' ;
*/
std::any Formatter::visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) {
    const auto &identList = context->identList();
    const bool hasComma = context->children.size() > 2 + (identList ? 1 : 0);
    return "[" +
           (identList ? std::any_cast<std::string>(visitIdentList(identList, hasComma, false, isMultiLine(context)))
                      : "") +
           "]";
};

/*
bracketValues : '[' valueList? ','? ']' ;
*/
std::any Formatter::visitBracketValues(OpenCMLParser::BracketValuesContext *context) {
    const auto &valueList = context->valueList();
    const bool hasComma = context->children.size() > 2 + (valueList ? 1 : 0);
    return "[" +
           (valueList ? std::any_cast<std::string>(visitValueList(valueList, hasComma, false, isMultiLine(context)))
                      : "") +
           "]";
};

/*
memberAccess : '[' entityExpr ']' ;
*/
std::any Formatter::visitMemberAccess(OpenCMLParser::MemberAccessContext *context) {
    return "[" + std::any_cast<std::string>(visitEntityExpr(context->entityExpr())) + "]";
};

/*
parentParams : '(' pairedParams? ','? ')' ;
*/
std::any Formatter::visitParentParams(OpenCMLParser::ParentParamsContext *context) {
    const auto &pairedParams = context->pairedParams();
    const bool hasComma = context->children.size() > 2 + (pairedParams ? 1 : 0);
    return "(" +
           (pairedParams
                ? std::any_cast<std::string>(visitPairedParams(pairedParams, hasComma, false, isMultiLine(context)))
                : "") +
           ")";
};

/*
parentArgues       : '(' argumentList? ','? ')' ;
*/
std::any Formatter::visitParentArgues(OpenCMLParser::ParentArguesContext *context) {
    const auto &argumentList = context->argumentList();
    const bool hasComma = context->children.size() > 2 + (argumentList ? 1 : 0);
    return "(" +
           (argumentList
                ? std::any_cast<std::string>(visitArgumentList(argumentList, hasComma, false, isMultiLine(context)))
                : "") +
           ")";
};

/*
parentValues       : '(' valueList? ','? ')' ;
*/
std::any Formatter::visitParentValues(OpenCMLParser::ParentValuesContext *context) {
    const auto &valueList = context->valueList();
    const bool hasComma = context->children.size() > 2 + (valueList ? 1 : 0);
    return "(" +
           (valueList ? std::any_cast<std::string>(visitValueList(valueList, hasComma, false, isMultiLine(context)))
                      : "") +
           ")";
};

/*
angledParams       : '<' pairedParams? ','? '>' ;
*/
std::any Formatter::visitAngledParams(OpenCMLParser::AngledParamsContext *context) {
    const auto &pairedParams = context->pairedParams();
    const bool hasComma = context->children.size() > 2 + (pairedParams ? 1 : 0);
    return "<" +
           (pairedParams
                ? std::any_cast<std::string>(visitPairedParams(pairedParams, hasComma, false, isMultiLine(context)))
                : "") +
           ">";
};

/*
angledValues       : '<' argumentList? ','? '>' ;
*/
std::any Formatter::visitAngledValues(OpenCMLParser::AngledValuesContext *context) {
    const auto &argumentList = context->argumentList();
    const bool hasComma = context->children.size() > 2 + (argumentList ? 1 : 0);
    return "<" +
           (argumentList
                ? std::any_cast<std::string>(visitArgumentList(argumentList, hasComma, false, isMultiLine(context)))
                : "") +
           ">";
};

/*
entityExpr
    : ternaryExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '&=' | '|=') ternaryExpr)?
    ;
*/
std::any Formatter::visitEntityExpr(OpenCMLParser::EntityExprContext *context) {
    std::string result = std::any_cast<std::string>(visitTernaryExpr(context->ternaryExpr(0)));
    if (context->children.size() > 1) {
        result += " " + context->children[1]->getText() + " " +
                  std::any_cast<std::string>(visitTernaryExpr(context->ternaryExpr(1)));
    }
    return result;
};

/*
ternaryExpr
    : relationalExpr ('?' ternaryExpr ':' ternaryExpr)?
    ;
*/
std::any Formatter::visitTernaryExpr(OpenCMLParser::TernaryExprContext *context) {
    std::string result = std::any_cast<std::string>(visitRelationalExpr(context->relationalExpr()));
    if (context->children.size() > 1) {
        result += " ? " + std::any_cast<std::string>(visitTernaryExpr(context->ternaryExpr(0))) + " : " +
                  std::any_cast<std::string>(visitTernaryExpr(context->ternaryExpr(1)));
    }
    return result;
};

/*
logicalOrExpr
    : logicalAndExpr ('||' logicalAndExpr)*
    ;
*/
std::any Formatter::visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) {
    return formatList(context->logicalAndExpr(), context, false, " ||", false, false);
};

/*
logicalAndExpr
    : equalityExpr ('&&' equalityExpr)*
    ;
*/
std::any Formatter::visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) {
    return formatList(context->equalityExpr(), context, false, " &&", false, false);
};

/*
equalityExpr
    : relationalExpr (('==' | '!=') relationalExpr)*
    ;
*/
std::any Formatter::visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) {
    std::string result = std::any_cast<std::string>(visitRelationalExpr(context->relationalExpr(0)));
    for (int i = 1; i < context->relationalExpr().size(); i++) {
        result += " " + context->children[i * 2 - 1]->getText() + " " +
                  std::any_cast<std::string>(visitRelationalExpr(context->relationalExpr(i)));
    }
};

/*
relationalExpr
    : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
    ;
*/
std::any Formatter::visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) {
    std::string result = std::any_cast<std::string>(visitAdditiveExpr(context->additiveExpr(0)));
    for (int i = 1; i < context->additiveExpr().size(); i++) {
        result += " " + context->children[i * 2 - 1]->getText() + " " +
                  std::any_cast<std::string>(visitAdditiveExpr(context->additiveExpr(i)));
    }
    return result;
};

/*
additiveExpr
    : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
    ;
*/
std::any Formatter::visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) {
    std::string result = std::any_cast<std::string>(visitMultiplicativeExpr(context->multiplicativeExpr(0)));
    for (int i = 1; i < context->multiplicativeExpr().size(); i++) {
        result += " " + context->children[i * 2 - 1]->getText() + " " +
                  std::any_cast<std::string>(visitMultiplicativeExpr(context->multiplicativeExpr(i)));
    }
    return result;
};

/*
multiplicativeExpr
    : unaryExpr (('^' | '*' | '/' | '%' | AS | IS) unaryExpr)*
    ;
*/
std::any Formatter::visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) {
    std::string result = std::any_cast<std::string>(visitUnaryExpr(context->unaryExpr(0)));
    for (int i = 1; i < context->unaryExpr().size(); i++) {
        result += " " + context->children[i * 2 - 1]->getText() + " " +
                  std::any_cast<std::string>(visitUnaryExpr(context->unaryExpr(i)));
    }
    return result;
};

/*
unaryExpr
    : linkExpr
    | ('!' | '-' | '~') linkExpr
    ;
*/
std::any Formatter::visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) {
    if (context->children.size() == 1) {
        return visitLinkExpr(context->linkExpr());
    } else {
        return context->children[0]->getText() + std::any_cast<std::string>(visitLinkExpr(context->linkExpr()));
    }
};

/*
linkExpr
    : withExpr ('->' withExpr)*
    ;
*/
std::any Formatter::visitLinkExpr(OpenCMLParser::LinkExprContext *context) {
    std::string result = std::any_cast<std::string>(visitWithExpr(context->withExpr(0)));
    for (int i = 1; i < context->withExpr().size(); i++) {
        result += "->" + std::any_cast<std::string>(visitWithExpr(context->withExpr(i)));
    }
    return result;
};

/*
withExpr
    : annotatedExpr ('.' annotatedExpr)*
    ;
*/
std::any Formatter::visitWithExpr(OpenCMLParser::WithExprContext *context) {
    std::string result = std::any_cast<std::string>(visitAnnotatedExpr(context->annotatedExpr(0)));
    for (int i = 1; i < context->annotatedExpr().size(); i++) {
        result += "." + std::any_cast<std::string>(visitAnnotatedExpr(context->annotatedExpr(i)));
    }
    return result;
};

/*
annotatedExpr
    : primaryExpr (memberAccess | parentArgues | angledValues | annotation)*
    ;
*/
std::any Formatter::visitAnnotatedExpr(OpenCMLParser::AnnotatedExprContext *context) {
    std::string result = std::any_cast<std::string>(visitPrimaryExpr(context->primaryExpr()));
    for (int i = 1; i < context->children.size(); i++) {
        result += std::any_cast<std::string>(visit(context->children[i]));
    }
    return result;
};

/*
primaryExpr
    : identRef
    | literal
    | bracketValues         // for list
    | bracedPairedValues    // for dict
    | '(' entityExpr ')'    // if there is only one entity, it will be recognized as a primary expression rather than a tuple
    | parentValues          // for tuple
    \\ for vector | array | tensor | set | map
    | '<' typeExpr (',' (typeExpr | INTEGER | '[' INTEGER (',' INTEGER)* ']'))? '>' (bracketValues | bracedValues | bracedIndexKVPairs)
    | lambdaExpr ;
*/
std::any Formatter::visitPrimaryExpr(OpenCMLParser::PrimaryExprContext *context) {
    const auto &alt = context->getAltNumber();
    if (alt == 5) { // '(' entityExpr ')'
        return "(" + std::any_cast<std::string>(visitEntityExpr(context->entityExpr())) + ")";
    } else if (alt == 7) {
        std::string result;
        const auto &typeExpr = context->typeExpr();
        if (typeExpr.size() > 0) {
            result += "<" + std::any_cast<std::string>(visitTypeExpr(typeExpr[0]));
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
                    result += std::any_cast<std::string>(visitTypeExpr(context->typeExpr(1)));
                }
            }
            result += ">";
        }
        return result + std::any_cast<std::string>(visit(context->children.back()));
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
std::any Formatter::visitLiteral(OpenCMLParser::LiteralContext *context) {
    const std::string &text = context->getText();
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
std::any Formatter::visitTypeExpr(OpenCMLParser::TypeExprContext *context) {
    std::string result = std::any_cast<std::string>(visitArrayType(context->arrayType(0)));
    for (int i = 1; i < context->arrayType().size(); i++) {
        result += " " + context->children[i * 2 - 1]->getText() + " " +
                  std::any_cast<std::string>(visitArrayType(context->arrayType(i)));
    }
    return result;
};

/*
arrayType
    : atomType ('[' INTEGER? ']')*
    ;
*/
std::any Formatter::visitArrayType(OpenCMLParser::ArrayTypeContext *context) {
    std::string result = std::any_cast<std::string>(visitAtomType(context->atomType()));
    for (int i = 1; i < context->children.size() - 1; i++) {
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
std::any Formatter::visitAtomType(OpenCMLParser::AtomTypeContext *context) {
    if (context->getAltNumber() == 5) {
        return "(" + std::any_cast<std::string>(visitTypeExpr(context->typeExpr())) + ")";
    } else {
        return visit(context->children[0]);
    }
};

/*
lambdaType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;
*/
std::any Formatter::visitLambdaType(OpenCMLParser::LambdaTypeContext *context) {
    std::string result;
    const auto &modifiers = context->modifiers();
    const auto &angledParams = context->angledParams();
    const auto &parentParams = context->parentParams();
    if (modifiers) {
        result += std::any_cast<std::string>(visitModifiers(modifiers)) + " ";
    }
    if (angledParams) {
        result += std::any_cast<std::string>(visitAngledParams(angledParams));
    }
    result += std::any_cast<std::string>(visitParentParams(parentParams));
    return result + " => " + std::any_cast<std::string>(visitTypeExpr(context->typeExpr()));
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
std::any Formatter::visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) { return context->getText(); };

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
std::any Formatter::visitStructType(OpenCMLParser::StructTypeContext *context) {
    // return formatted string
    switch (context->getAltNumber()) {
    case 1: // SET_TYPE ('<' typeExpr '>')?
    {
        const auto &typeExpr = context->typeExpr(0);
        return typeExpr ? "Set<" + std::any_cast<std::string>(visitTypeExpr(typeExpr)) + ">" : std::string("Set");
    } break;
    case 2: // MAP_TYPE ('<' typeExpr ',' typeExpr '>')?
    {
        const auto &typeExpr0 = context->typeExpr(0);
        const auto &typeExpr1 = context->typeExpr(1);
        return typeExpr0 && typeExpr1 ? "Map<" + std::any_cast<std::string>(visitTypeExpr(typeExpr0)) + ", " +
                                            std::any_cast<std::string>(visitTypeExpr(typeExpr1)) + ">"
                                      : std::string("Map");
    } break;
    case 3: // LIST_TYPE
        return std::string("List");
        break;
    case 4: // DICT_TYPE
        return std::string("Dict");
        break;
    case 5: // ARRAY_TYPE ('<' typeExpr (',' INTEGER)? '>')?
    {
        const auto &typeExpr = context->typeExpr(0);
        const auto &integer = context->INTEGER(0);
        return typeExpr ? integer ? "Array<" + std::any_cast<std::string>(visitTypeExpr(typeExpr)) + ", " +
                                        integer->getText() + ">"
                                  : "Array<" + std::any_cast<std::string>(visitTypeExpr(typeExpr)) + ">"
                        : std::string("Array");
    } break;
    case 6: // TUPLE_TYPE ('<' typeList? ','? '>')?
    {
        const auto &typeList = context->typeList();
        return typeList
                   ? "Tuple<" +
                         std::any_cast<std::string>(visitTypeList(typeList, false, false, isMultiLine(context))) + ">"
                   : "Tuple";
    } break;
    case 7: // UNION_TYPE ('<' typeList? ','? '>')?
    {
        const auto &typeList = context->typeList();
        return typeList
                   ? "Union<" +
                         std::any_cast<std::string>(visitTypeList(typeList, false, false, isMultiLine(context))) + ">"
                   : "Union";
    } break;
    case 8: // VECTOR_TYPE ('<' typeExpr '>')?
    {
        const auto &typeExpr = context->typeExpr(0);
        return typeExpr ? "Vector<" + std::any_cast<std::string>(visitTypeExpr(typeExpr)) + ">" : "Vector";
    } break;
    case 9: // TENSOR_TYPE ('<' typeExpr (',' '[' INTEGER (',' INTEGER)* ']')? '>')?
    {
        const auto &typeExpr = context->typeExpr(0);
        const auto &integers = context->INTEGER();
        if (typeExpr) {
            if (integers.size() == 1) {
                return "Tensor<" + std::any_cast<std::string>(visitTypeExpr(typeExpr)) + ", [" +
                       integers[0]->getText() + "]>";
            } else {
                std::string result = "Tensor<" + std::any_cast<std::string>(visitTypeExpr(typeExpr)) + ", [";
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
            return std::string("Tensor");
        }
    } break;
    case 10: // '{' pairedTypes? ','? '}'
    {
        const auto &pairedTypes = context->pairedTypes();
        return pairedTypes ? "{" + std::any_cast<std::string>(visitPairedTypes(pairedTypes)) + "}" : "{}";
    } break;
    case 11: // '{' indexKTPair '}'
    {
        return "{ " + std::any_cast<std::string>(visitIndexKTPair(context->indexKTPair())) + " }";
    } break;

    default:
        return std::string("");
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
std::any Formatter::visitSpecialType(OpenCMLParser::SpecialTypeContext *context) { return context->getText(); };

/*
identRef : IDENTIFIER ;
*/
std::any Formatter::visitIdentRef(OpenCMLParser::IdentRefContext *context) { return context->getText(); };
