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
stmtList : stmt+ ;
*/
std::any Formatter::visitStmtList(OpenCMLParser::StmtListContext *context, bool padding, bool forceMultiLine,
                                  bool trailingComma) {
    return formatList(context->stmt(), context, trailingComma, ";", padding, forceMultiLine);
};

/*
stmt
    : letStmt SEP
    | useStmt SEP
    | typeStmt SEP
    | exprStmt SEP
    | assignStmt SEP
    | funcDef SEP
    | retStmt SEP
    ;
*/
std::any Formatter::visitStmt(OpenCMLParser::StmtContext *context) { return visit(context->children[0]); };

/*
letStmt : LET carrier (':' typeExpr)? ('='? entityExpr)?
        | carrier (':' typeExpr)? ':=' entityExpr ;
*/
std::any Formatter::visitLetStmt(OpenCMLParser::LetStmtContext *context) {
    std::string result;
    const auto &carrier = context->carrier();
    const auto &typeExpr = context->typeExpr();
    const auto &entityExpr = context->entityExpr();
    if (context->getAltNumber() == 1) {
        result += "let " + std::any_cast<std::string>(visitCarrier(carrier));
        if (typeExpr) {
            result += ": " + std::any_cast<std::string>(visitTypeExpr(typeExpr));
        }
        if (entityExpr) {
            if (context->children[typeExpr ? 4 : 2]->getText() == "=") {
                result += " =";
            }
            result += " " + std::any_cast<std::string>(visitEntityExpr(entityExpr));
        }
        return result;
    } else {
        result += std::any_cast<std::string>(visitCarrier(carrier));
        if (typeExpr) {
            result += ": " + std::any_cast<std::string>(visitTypeExpr(typeExpr));
        }
        result += " := " + std::any_cast<std::string>(visitEntityExpr(entityExpr));
        return result;
    }
};

/*
useStmt : USE carrier '='? entityExpr
        | carrier '::' entityExpr ;
*/
std::any Formatter::visitUseStmt(OpenCMLParser::UseStmtContext *context) {
    std::string result;
    const auto &carrier = context->carrier();
    const auto &entityExpr = context->entityExpr();
    if (context->getAltNumber() == 1) {
        result += "use " + std::any_cast<std::string>(visitCarrier(carrier));
        if (entityExpr) {
            if (context->children[2]->getText() == "=") {
                result += " =";
            }
            result += " " + std::any_cast<std::string>(visitEntityExpr(entityExpr));
        }
        return result;
    } else {
        result += std::any_cast<std::string>(visitCarrier(carrier)) +
                  " :: " + std::any_cast<std::string>(visitEntityExpr(entityExpr));
        return result;
    }
};

/*
typeStmt : TYPE identRef '='? typeExpr ;
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
assignStmt : identRef memberAccess* '=' entityExpr ;
*/
std::any Formatter::visitAssignStmt(OpenCMLParser::AssignStmtContext *context) {
    std::string result;
    const auto &identRef = context->identRef();
    const auto &memberAccesses = context->memberAccess();
    const auto &entityExpr = context->entityExpr();
    result += std::any_cast<std::string>(visitIdentRef(identRef));
    for (const auto &memberAccess : memberAccesses) {
        result += std::any_cast<std::string>(visitMemberAccess(memberAccess));
    }
    result += " = " + std::any_cast<std::string>(visitEntityExpr(entityExpr));
    return result;
};

/*
withDef : WITH angledParams ;
*/
std::any Formatter::visitWithDef(OpenCMLParser::WithDefContext *context) {
    return "with " + std::any_cast<std::string>(visitAngledParams(context->angledParams()));
};

/*
funcDef : annotations? withDef? modifiers? FUNC identRef parentParams (':' typeExpr)? bracedStmts ;
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
lambdaExpr : modifiers? ((parentParams (':' typeExpr)? '=>' (bracedStmts | entityExpr)) | '{' stmtList '}' ) ;
*/
std::any Formatter::visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) {
    std::string result;
    const auto &modifiers = context->modifiers();
    const auto &parentParams = context->parentParams();
    if (modifiers) {
        result += std::any_cast<std::string>(visitModifiers(modifiers)) + " ";
    }
    if (parentParams) {
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
    } else {
        const auto &stmtList = context->stmtList();
        const bool hasComma = stmtList && stmtList->getStop()->getText() == ";";
        const bool multiLine =
            modifiers ? modifiers->getStart()->getLine() != stmtList->getStart()->getLine() : isMultiLine(context);
        result += "{" + std::any_cast<std::string>(visitStmtList(stmtList, true, multiLine, hasComma)) + "}";
    }
    return result;
};

/*
carrier : identRef | bracedIdents | bracketIdents ;
*/
std::any Formatter::visitCarrier(OpenCMLParser::CarrierContext *context) { return visit(context->children[0]); };

/*
annotation  : '@' primEntity ;
*/
std::any Formatter::visitAnnotation(OpenCMLParser::AnnotationContext *context) {
    return "@" + std::any_cast<std::string>(visitPrimEntity(context->primEntity()));
};

/*
annotations : (annotation SEP?)+ ;
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
modifiers   : (INNER | OUTER | ATOMIC | STATIC | SYNC)+ ;
*/
std::any Formatter::visitModifiers(OpenCMLParser::ModifiersContext *context) {
    std::string result;
    (context->INNER()).size() ? result += "inner " : result;
    (context->OUTER()).size() ? result += "outer " : result;
    (context->ATOMIC()).size() ? result += "atomic " : result;
    (context->STATIC()).size() ? result += "static " : result;
    (context->SYNC()).size() ? result += "sync " : result;
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
keyParamPair : identRef annotation? ':' typeExpr ('=' entityExpr)? ;
*/
std::any Formatter::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) {
    std::string result = std::any_cast<std::string>(visitIdentRef(context->identRef()));
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
        std::string result = "{";
        // check if there are comments inside the braces
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
parentValues : '(' argumentList? ','? ')' ;
*/
std::any Formatter::visitParentValues(OpenCMLParser::ParentValuesContext *context) {
    const auto &argumentList = context->argumentList();
    const bool hasComma = context->children.size() > 2 + (argumentList ? 1 : 0);
    return "(" +
           (argumentList
                ? std::any_cast<std::string>(visitArgumentList(argumentList, hasComma, false, isMultiLine(context)))
                : "") +
           ")";
};

/*
angledParams : '<' pairedParams? ','? '>' ;
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
angledValues : '<' argumentList? ','? '>' ;
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
primEntity
    : identRef
    | literal
    | bracketValues
    | bracedPairedValues
    | lambdaExpr
    | '(' entityExpr ')' ;
*/
std::any Formatter::visitPrimEntity(OpenCMLParser::PrimEntityContext *context) {
    if (context->getAltNumber() == 6) {
        return "(" + std::any_cast<std::string>(visitEntityExpr(context->entityExpr())) + ")";
    } else {
        return visit(context->children[0]);
    }
};

/*
memberAccess : '[' entityExpr ']' ;
*/
std::any Formatter::visitMemberAccess(OpenCMLParser::MemberAccessContext *context) {
    return "[" + std::any_cast<std::string>(visitEntityExpr(context->entityExpr())) + "]";
};

/*
entityChain  : entityLink+ ;
*/
std::any Formatter::visitEntityChain(OpenCMLParser::EntityChainContext *context) {
    std::string result;
    const auto &entityLink = context->entityLink();
    for (int i = 0; i < entityLink.size(); i++) {
        if (i > 0) {
            result += " ";
        }
        result += std::any_cast<std::string>(visitEntityLink(entityLink[i]));
    }
    return result;
};

/*
entityLink   : entityUnit | entityLink '->' entityUnit ;
*/
std::any Formatter::visitEntityLink(OpenCMLParser::EntityLinkContext *context) {
    if (context->getAltNumber() == 1) {
        return visitEntityUnit(context->entityUnit());
    } else {
        return std::any_cast<std::string>(visitEntityLink(context->entityLink())) + "->" +
               std::any_cast<std::string>(visitEntityUnit(context->entityUnit()));
    }
};

/*
entityUnit   : entityWith (({isAdjacent()}? (memberAccess | angledValues | parentValues)) | annotation)* ;
*/
std::any Formatter::visitEntityUnit(OpenCMLParser::EntityUnitContext *context) {
    std::string result;
    const auto &entityWith = context->entityWith();
    result += std::any_cast<std::string>(visitEntityWith(entityWith));
    const auto &children = context->children;
    for (int i = 1; i < children.size(); i++) {
        const auto &child = children[i];
        result += std::any_cast<std::string>(child->accept(this));
    }
    return result;
};

/*
entityWith   : primEntity | entityWith '.' primEntity ;
*/
std::any Formatter::visitEntityWith(OpenCMLParser::EntityWithContext *context) {
    if (context->getAltNumber() == 1) {
        return visitPrimEntity(context->primEntity());
    } else {
        return std::any_cast<std::string>(visitEntityWith(context->entityWith())) + "." +
               std::any_cast<std::string>(visitPrimEntity(context->primEntity()));
    }
};

/*
entityExpr
    : relaExpr
    | entityExpr '+=' relaExpr
    | entityExpr '-=' relaExpr
    | entityExpr '*=' relaExpr
    | entityExpr '/=' relaExpr
    | entityExpr '%=' relaExpr
    | entityExpr '^=' relaExpr
    | entityExpr '&=' relaExpr
    | entityExpr '|=' relaExpr
    ;
*/
std::any Formatter::visitEntityExpr(OpenCMLParser::EntityExprContext *context) {
    if (context->getAltNumber() == 1) {
        return visitRelaExpr(context->relaExpr());
    } else {
        std::string result;
        result += std::any_cast<std::string>(visitEntityExpr(context->entityExpr())) + " " +
                  context->children[1]->getText() + " " +
                  std::any_cast<std::string>(visitRelaExpr(context->relaExpr()));
        return result;
    }
};

/*
relaExpr
    : addExpr
    | relaExpr '<' addExpr
    | relaExpr '>' addExpr
    | relaExpr '<=' addExpr
    | relaExpr '>=' addExpr
    | relaExpr '==' addExpr
    | relaExpr '!=' addExpr
    | relaExpr '&&' addExpr
    | relaExpr '||' addExpr
    ;
*/
std::any Formatter::visitRelaExpr(OpenCMLParser::RelaExprContext *context) {
    if (context->getAltNumber() == 1) {
        return visitAddExpr(context->addExpr());
    } else {
        std::string result;
        result += std::any_cast<std::string>(visitRelaExpr(context->relaExpr())) + " " +
                  context->children[1]->getText() + " " + std::any_cast<std::string>(visitAddExpr(context->addExpr()));
        return result;
    }
};

/*
addExpr
    : multiExpr
    | addExpr '+' multiExpr
    | addExpr '-' multiExpr
    | addExpr '&' multiExpr
    | addExpr '|' multiExpr
    ;
*/
std::any Formatter::visitAddExpr(OpenCMLParser::AddExprContext *context) {
    if (context->getAltNumber() == 1) {
        return visitMultiExpr(context->multiExpr());
    } else {
        std::string result;
        result += std::any_cast<std::string>(visitAddExpr(context->addExpr())) + " " + context->children[1]->getText() +
                  " " + std::any_cast<std::string>(visitMultiExpr(context->multiExpr()));
        return result;
    }
};

/*
multiExpr
    : unaryExpr
    | multiExpr '^' unaryExpr
    | multiExpr '*' unaryExpr
    | multiExpr '/' unaryExpr
    | multiExpr '%' unaryExpr
    | multiExpr AS typeExpr
    | multiExpr IS typeExpr
    ;
*/
std::any Formatter::visitMultiExpr(OpenCMLParser::MultiExprContext *context) {
    const auto &unaryExpr = context->unaryExpr();
    const auto &multiExpr = context->multiExpr();
    const auto &typeExpr = context->typeExpr();
    if (context->children.size() == 1) {
        return visitUnaryExpr(unaryExpr);
    } else if (typeExpr) {
        std::string result;
        result += std::any_cast<std::string>(visitMultiExpr(multiExpr)) + " " + context->children[1]->getText() + " " +
                  std::any_cast<std::string>(visitTypeExpr(typeExpr));
        return result;
    } else {
        std::string result;
        result += std::any_cast<std::string>(visitMultiExpr(multiExpr)) + " " + context->children[1]->getText() + " " +
                  std::any_cast<std::string>(visitUnaryExpr(unaryExpr));
        return result;
    }
};

/*
unaryExpr
    : primExpr
    | '!' primExpr
    | '~' primExpr
    ;
*/
std::any Formatter::visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) {
    const auto &primExpr = context->primExpr();
    if (context->children.size() == 1) {
        return visitPrimExpr(primExpr);
    } else {
        return context->children[0]->getText() + std::any_cast<std::string>(visitPrimExpr(primExpr));
    }
};

/*
primExpr
    : entityChain
    | '(' entityExpr ')'
    ;
*/
std::any Formatter::visitPrimExpr(OpenCMLParser::PrimExprContext *context) {
    if (context->getAltNumber() == 1) {
        return visitEntityChain(context->entityChain());
    } else {
        return "(" + std::any_cast<std::string>(visitEntityExpr(context->entityExpr())) + ")";
    }
};

std::string Formatter::processStringLiteral(const std::string &input) {
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
        return processStringLiteral(text);
    } else {
        return text;
    }
};

/*
typeExpr
    : unaryType
    | typeExpr '&' unaryType
    | typeExpr '|' unaryType
    ;
*/
std::any Formatter::visitTypeExpr(OpenCMLParser::TypeExprContext *context) {
    if (context->getAltNumber() == 1) {
        return std::any_cast<std::string>(visitUnaryType(context->unaryType()));
    } else {
        std::string result;
        result += std::any_cast<std::string>(visitTypeExpr(context->typeExpr())) + " " +
                  context->children[1]->getText() + " " +
                  std::any_cast<std::string>(visitUnaryType(context->unaryType()));
        return result;
    }
};

/*
unaryType
    : atomType ('[' INTEGER? ']')*
    ;
*/
std::any Formatter::visitUnaryType(OpenCMLParser::UnaryTypeContext *context) {
    std::string result = std::any_cast<std::string>(visitAtomType(context->atomType()));
    for (size_t i = 1; i < context->children.size(); i++) {
        const std::string &text = context->children[i]->getText();
        if (text == "[") {
            result += "[";
            if (context->children[i + 1]->getText() != "]") {
                result += context->children[i + 1]->getText();
            }
            result += "]";
        }
    }
    return result;
};

/*
atomType
    : primType
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
    : ('<' pairedParams? '>')? '(' pairedParams? ')' '=>' typeExpr
    ;
*/
std::any Formatter::visitLambdaType(OpenCMLParser::LambdaTypeContext *context) {
    std::string result;
    const auto &params0 = context->pairedParams(0);
    const auto &params1 = context->pairedParams(1);
    const auto &typeExpr = context->typeExpr();
    if (params1) {
        result += "<" + std::any_cast<std::string>(visitPairedParams(params0, false, false, false)) + ">";
    }
    result += "(";
    result += std::any_cast<std::string>(visitPairedParams(params1 ? params1 : params0, false, false, false));
    result += ") => " + std::any_cast<std::string>(visitTypeExpr(typeExpr));
    return result;
};

/*
primType
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
std::any Formatter::visitPrimType(OpenCMLParser::PrimTypeContext *context) { return context->getText(); };

/*
structType
    : SET_TYPE ('<' typeExpr '>')?
    | MAP_TYPE ('<' typeExpr ',' typeExpr '>')?
    | LIST_TYPE
    | DICT_TYPE // universal dict type
    | '{' pairedTypes? ','? '}' // concrete dict type
    | ARRAY_TYPE ('<' typeExpr '>')?
    | TUPLE_TYPE ('<' typeList? ','? '>')?
    | UNION_TYPE ('<' typeList? ','? '>')?
    | VECTOR_TYPE ('<' typeExpr (',' INTEGER)? '>')?
    | TENSOR_TYPE ('<' typeExpr (',' '[' INTEGER (',' INTEGER)* ']')? '>')?
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
    case 5: // '{' pairedTypes? ','? '}'
    {
        const auto &pairedTypes = context->pairedTypes();
        return pairedTypes ? "{" + std::any_cast<std::string>(visitPairedTypes(pairedTypes)) + "}" : "{}";
    } break;
    case 6: // ARRAY_TYPE ('<' typeExpr '>')?
    {
        const auto &typeExpr = context->typeExpr(0);
        return typeExpr ? "Array<" + std::any_cast<std::string>(visitTypeExpr(typeExpr)) + ">" : "Array";
    } break;
    case 7: // TUPLE_TYPE ('<' typeList? ','? '>')?
    {
        const auto &typeList = context->typeList();
        return typeList
                   ? "Tuple<" +
                         std::any_cast<std::string>(visitTypeList(typeList, false, false, isMultiLine(context))) + ">"
                   : "Tuple";
    } break;
    case 8: // UNION_TYPE ('<' typeList? ','? '>')?
    {
        const auto &typeList = context->typeList();
        return typeList
                   ? "Union<" +
                         std::any_cast<std::string>(visitTypeList(typeList, false, false, isMultiLine(context))) + ">"
                   : "Union";
    } break;
    case 9: // VECTOR_TYPE ('<' typeExpr (',' INTEGER)? '>')?
    {
        const auto &typeExpr = context->typeExpr(0);
        const auto &integer = context->INTEGER(0);
        return typeExpr ? integer ? "Vector<" + std::any_cast<std::string>(visitTypeExpr(typeExpr)) + ", " +
                                        integer->getText() + ">"
                                  : "Vector<" + std::any_cast<std::string>(visitTypeExpr(typeExpr)) + ">"
                        : std::string("Vector");
    } break;
    case 10: // TENSOR_TYPE ('<' typeExpr (',' '[' INTEGER (',' INTEGER)* ']')? '>')?
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