#include "fmt-new.h"
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
    result += commentText;
    if (comment->getChannel() == 4) {
        // block comment
        result += " ";
    }
};

/*
program : SEP? ((decl | stmt) SEP?)* EOF;
*/
any Formatter::visitProgram(OpenCMLParser::ProgramContext *context) {}

/*
decl
    : moduleDecl
    | importDecl
    | exportDecl
    | funcDecl
    ;
*/
any Formatter::visitDecl(OpenCMLParser::DeclContext *context) {}

/*
stmt
    : letStmt
    | useStmt
    | retStmt
    | typeStmt
    | enumStmt
    | exprStmt
    | stmtBlock
    ;
*/
any Formatter::visitStmt(OpenCMLParser::StmtContext *context) {}

/*
stmtList : stmt (SEP? stmt)* SEP? ;
*/
any Formatter::visitStmtList(OpenCMLParser::StmtListContext *context) {}

/*
moduleDecl : MODULE identDef ;
*/
any Formatter::visitModuleDecl(OpenCMLParser::ModuleDeclContext *context) {}

/*
importDecl : IMPORT (STRING | (identDef | bracedIdents) FROM STRING) ;
*/
any Formatter::visitImportDecl(OpenCMLParser::ImportDeclContext *context) {}

/*
exportDecl : EXPORT (letStmt | typeStmt | bracedIdents) ;
*/
any Formatter::visitExportDecl(OpenCMLParser::ExportDeclContext *context) {}

/*
stmtBlock  : SYNC? '{' stmtList? '}' ;
*/
any Formatter::visitStmtBlock(OpenCMLParser::StmtBlockContext *context) {}

/*
lambdaExpr : modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr ;
*/
any Formatter::visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) {}

/*
funcDecl   : annotations? (WITH angledParams)? EXPORT? modifiers? FUNC identDef parentParams (':' typeExpr)? stmtBlock ;
*/
any Formatter::visitFuncDecl(OpenCMLParser::FuncDeclContext *context) {}

/*
parentIdents  : '(' identList? ','? ')' ;    // for tuple unpacking
*/
any Formatter::visitParentIdents(OpenCMLParser::ParentIdentsContext *context) {}

/*
bracedIdents  : '{' identList? ','? '}' ;    // for dict unpacking
*/
any Formatter::visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) {}

/*
bracketIdents : '[' identList? ','? ']' ;    // for list unpacking
*/
any Formatter::visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) {}

/*
carrier       : identDef | parentIdents | bracedIdents | bracketIdents ;
*/
any Formatter::visitCarrier(OpenCMLParser::CarrierContext *context) {}

/*
letStmt    : (LET | VAR) carrier (':' typeExpr)? '=' dataExpr ;
*/
any Formatter::visitLetStmt(OpenCMLParser::LetStmtContext *context) {}

/*
useStmt    : USE (identDef '=')? identRef ;
*/
any Formatter::visitUseStmt(OpenCMLParser::UseStmtContext *context) {}

/*
retStmt    : (RETURN | RAISE | THROW) dataExpr ;
*/
any Formatter::visitRetStmt(OpenCMLParser::RetStmtContext *context) {}

/*
typeStmt   : TYPE identDef '=' typeExpr ;
*/
any Formatter::visitTypeStmt(OpenCMLParser::TypeStmtContext *context) {}

/*
enumStmt   : ENUM identDef (OF typeExpr)? '=' '{' pairedValues ','? '}' ;
*/
any Formatter::visitEnumStmt(OpenCMLParser::EnumStmtContext *context) {}

/*
exprStmt   : annotations? dataExpr ;
*/
any Formatter::visitExprStmt(OpenCMLParser::ExprStmtContext *context) {}

/*
annotation  : '@' primaryData ;
*/
any Formatter::visitAnnotation(OpenCMLParser::AnnotationContext *context) {}

/*
annotations : annotation+ ;
*/
any Formatter::visitAnnotations(OpenCMLParser::AnnotationsContext *context) {}

/*
modifiers   : (INNER | OUTER | ATOMIC | SHARED | SYNC | MACRO)+ ;
*/
any Formatter::visitModifiers(OpenCMLParser::ModifiersContext *context) {}

/*
indexValue   : dataExpr | '...' dataExpr ;
*/
any Formatter::visitIndexValue(OpenCMLParser::IndexValueContext *context) {}

/*
keyTypePair  : identDef ':' typeExpr ;
*/
any Formatter::visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) {}

/*
keyValuePair : identDef ':' dataExpr | '...' dataExpr ;
*/
any Formatter::visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) {}

/*
keyParamPair : VAR? identDef annotation? ':' (typeExpr | TYPEAS identDef) ('=' dataExpr)? ;
*/
any Formatter::visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) {}

/*
identList    : identDef (',' identDef)* ;
*/
any Formatter::visitIdentList(OpenCMLParser::IdentListContext *context) {}

/*
valueList    : dataExpr (',' dataExpr)* ;
*/
any Formatter::visitValueList(OpenCMLParser::ValueListContext *context) {}

/*
indexValues  : indexValue (',' indexValue)* ;
*/
any Formatter::visitIndexValues(OpenCMLParser::IndexValuesContext *context) {}

/*
pairedValues : keyValuePair (',' keyValuePair)* ;
*/
any Formatter::visitPairedValues(OpenCMLParser::PairedValuesContext *context) {}

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
any Formatter::visitPairedParams(OpenCMLParser::PairedParamsContext *context) {}

/*
argumentList : indexValues (',' pairedValues)? | pairedValues ;
*/
any Formatter::visitArgumentList(OpenCMLParser::ArgumentListContext *context) {}

/*
memberAccess : '[' dataExpr (':' dataExpr (':' dataExpr)?)? ']' ;
*/
any Formatter::visitMemberAccess(OpenCMLParser::MemberAccessContext *context) {}

/*
parentParams : '(' pairedParams? ','? ')' ; // for functor parameters definition
*/
any Formatter::visitParentParams(OpenCMLParser::ParentParamsContext *context) {}

/*
parentArgues : '(' argumentList? ','? ')' ; // for functor arguments
*/
any Formatter::visitParentArgues(OpenCMLParser::ParentArguesContext *context) {}

/*
angledParams : '<' pairedParams? ','? '>' ; // for functor super parameters definition
*/
any Formatter::visitAngledParams(OpenCMLParser::AngledParamsContext *context) {}

/*
angledValues : '<' argumentList? ','? '>' ; // for functor super arguments
*/
any Formatter::visitAngledValues(OpenCMLParser::AngledValuesContext *context) {}

/*
blockExpr : stmtBlock | dataExpr ;
*/
any Formatter::visitBlockExpr(OpenCMLParser::BlockExprContext *context) {}

/*
dataExpr
    : WAIT? structExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '&=' | '|=') structExpr)?
    ;
*/
any Formatter::visitDataExpr(OpenCMLParser::DataExprContext *context) {}

/*
pattern
    : identRef
    | literal
    | '(' (valueList | identList)? ','? ')'
    | '{' (pairedValues | identList)? ','? '}'
    | '_' // wildcard
    ;
*/
any Formatter::visitPattern(OpenCMLParser::PatternContext *context) {}

/*
matchCase
    : CASE pattern ('|' pattern)* '=>' blockExpr
    ;
*/
any Formatter::visitMatchCase(OpenCMLParser::MatchCaseContext *context) {}

/*
structExpr
    : logicalOrExpr
    | IF logicalOrExpr THEN blockExpr ELSE blockExpr
    | MATCH identRef '{' matchCase+ '}'
    | TRY stmtBlock (CATCH identDef ':' typeExpr stmtBlock)+ (FINALLY stmtBlock)?
    ;
*/
any Formatter::visitStructExpr(OpenCMLParser::StructExprContext *context) {}

/*
logicalOrExpr
    : logicalAndExpr ('||' logicalAndExpr)*
    ;
*/
any Formatter::visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) {}

/*
logicalAndExpr
    : equalityExpr ('&&' equalityExpr)*
    ;
*/
any Formatter::visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) {}

/*
equalityExpr
    : relationalExpr (('===' | '!==' | '==' | '!=') relationalExpr)*
    ;
*/
any Formatter::visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) {}

/*
relationalExpr
    : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
    ;
*/
any Formatter::visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) {}

/*
additiveExpr
    : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
    ;
*/
any Formatter::visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) {}

/*
multiplicativeExpr
    : nullableExpr (('^' | '*' | '/' | '%') nullableExpr)*
    ;
*/
any Formatter::visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) {}

/*
nullableExpr
    : unaryExpr (('??' | '!!') dataExpr)?
    ;
*/
any Formatter::visitNullableExpr(OpenCMLParser::NullableExprContext *context) {}

/*
unaryExpr
    : linkExpr ((AS | IS) typeExpr)?
    | ('!' | '-' | '~') linkExpr
    ;
*/
any Formatter::visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) {
    
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
    : withExpr (('..' | '?..') withExpr)*
    ;
*/
any Formatter::visitBindExpr(OpenCMLParser::BindExprContext *context) {
    return formatBiOpsList(context->withExpr(), context->children, false);
}

/*
withExpr
    : annoExpr (('.' | '?.') annoExpr)*
    ;
*/
any Formatter::visitWithExpr(OpenCMLParser::WithExprContext *context) {
    return formatBiOpsList(context->annoExpr(), context->children, false);
}

/*
annoExpr
    : primaryData ({isAdjacent()}? (memberAccess | parentArgues | angledValues | '!') | annotation)*
    ;
*/
any Formatter::visitAnnoExpr(OpenCMLParser::AnnoExprContext *context) {}

/*
dictExpr
    : '{' (pairedValues ','?)? '}' // no list comprehension because the struct of dict is immutable
    ;
*/
any Formatter::visitDictExpr(OpenCMLParser::DictExprContext *context) {}

/*
listExpr
    : '[' ((indexValues ','?) | dataExpr FOR identRef IN dataExpr (IF dataExpr)?)? ']'
    ;
*/
any Formatter::visitListExpr(OpenCMLParser::ListExprContext *context) {}

/*
primaryData
    : identRef
    | literal
    | listExpr
    | dictExpr
    | '(' dataExpr ')'        // if there is only one data, it will be recognized as a primary expression rather than a tuple
    | '(' valueList? ','? ')' // for tuple
    | lambdaExpr
    ;
*/
any Formatter::visitPrimaryData(OpenCMLParser::PrimaryDataContext *context) {}

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
any Formatter::visitLiteral(OpenCMLParser::LiteralContext *context) {}

/*
typeExpr
    : unionType ('?' unionType?)?
    ;
*/
any Formatter::visitTypeExpr(OpenCMLParser::TypeExprContext *context) {}

/*
unionType
    : unionUnit ('|' unionUnit)*
    ;
*/
any Formatter::visitUnionType(OpenCMLParser::UnionTypeContext *context) {
    return formatBiOpsList(context->unionUnit(), context->children);
}

/*
unionUnit : (identDef OF)? listType ;
*/
any Formatter::visitUnionUnit(OpenCMLParser::UnionUnitContext *context) {}

/*
listType
    : argsType ('[' ']')*
    ;
*/
any Formatter::visitListType(OpenCMLParser::ListTypeContext *context) {}

/*
argsType
    : primaryType ('<' (typeExpr | primaryData) (',' (typeExpr | primaryData))* '>')?
    ;
*/
any Formatter::visitArgsType(OpenCMLParser::ArgsTypeContext *context) {}

/*
primaryType
    : INNER_ATOM_TYPE
    | dictExprType
    | identRef
    | '(' typeExpr ')'
    | tupleType
    | lambdaType
    | TYPEOF dataExpr
    ;
*/
any Formatter::visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) {}

/*
dictExprType
    : dictType (('&' | '^') dictType)*
    ;
*/
any Formatter::visitDictExprType(OpenCMLParser::DictExprTypeContext *context) {
    return formatBiOpsList(context->dictType(), context->children);
}

/*
dictType
    : '{' (keyTypePair (',' keyTypePair)*)? ','? '}'
    ;
*/
any Formatter::visitDictType(OpenCMLParser::DictTypeContext *context) {
    // const auto &pairedTypes = context->pairedTypes();
    // if (pairedTypes) {
    //     return "{" + any_cast<string>(visitPairedTypes(pairedTypes)) + "}";
    // } else {
    //     return "{}";
    // }
}

/*
tupleType
    : '(' (typeExpr (',' typeExpr)*)? ','? ')'
    ;
*/
any Formatter::visitTupleType(OpenCMLParser::TupleTypeContext *context) {
    // const auto &pairedTypes = context->pairedTypes();
    // if (pairedTypes) {
    //     return "{" + any_cast<string>(visitPairedTypes(pairedTypes)) + "}";
    // } else {
    //     return "{}";
    // }
}

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
}

/*
identDef : IDENTIFIER ;
*/
any Formatter::visitIdentDef(OpenCMLParser::IdentDefContext *context) { return context->IDENTIFIER()->getText(); }

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
