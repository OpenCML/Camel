grammar OpenCML;
import OpenCMLLex;

@header {
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
 * Supported by: National Key Research and Development Program of China
 */
}

@parser::members {
bool isAdjacent() {
    const antlr4::Token *last = _input->LT(-1);
    const antlr4::Token *curr = _input->LT(1);
    if (last == nullptr || curr == nullptr)
        return false;
    if (last->getStopIndex() + 1 != curr->getStartIndex())
        return false;
    return true;
}
}

program : SEP? (decl SEP?)* EOF;

//declaration statement
decl
    : moduleDecl
    | importDecl
    | exportDecl
    | letDecl
    | useDecl
    | funcDecl
    | typeDecl
    | enumDecl
    ;

//executable statement
stmt
    : letDecl
    | useDecl
    | funcDecl
    | typeDecl
    | enumDecl
    | retStmt
    | exprStmt
    | blockStmt
    ;
stmtList : stmt (SEP? stmt)* SEP? ;

moduleDecl : MODULE identDef ;
importDecl : IMPORT (STRING | (identDef | bracedIdents) FROM STRING) ;
exportDecl : EXPORT (letDecl | typeDecl | bracedIdents) ;

stmtBlock  : SYNC? '{' stmtList? '}' ;
blockExpr  : stmtBlock | dataExpr ;
blockStmt  : WAIT? stmtBlock ;
lambdaExpr : modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr ;
funcDecl   : annotations? (WITH angledParams)? EXPORT? modifiers? FUNC identDef parentParams (':' typeExpr)? stmtBlock ;

parentIdents  : '(' identList? ','? ')' ;    // for tuple unpacking ()
bracedIdents  : '{' identList? ','? '}' ;    // for dict unpacking {}
bracketIdents : '[' identList? ','? ']' ;    // for list unpacking []
carrier       : identDef | parentIdents | bracedIdents | bracketIdents ;

letDecl    : (LET | VAR) carrier (':' typeExpr)? '=' dataExpr ;
useDecl    : USE (identDef '=')? identRef ;
retStmt    : (RETURN | RAISE | THROW) dataExpr ;
typeDecl   : TYPE identDef '=' typeExpr ;
enumDecl   : ENUM identDef (OF typeExpr)? '=' '{' pairedValues ','? '}' ;
exprStmt   : annotations? dataExpr ;

annotation  : '@' primaryData ;
annotations : annotation+ ;
modifiers   : (INNER | OUTER | ATOMIC | SHARED | SYNC | MACRO)+ ;

indexValue   : dataExpr | '...' dataExpr ;
keyTypePair  : identDef ':' typeExpr ;
keyValuePair : identDef ':' dataExpr | '...' dataExpr ;
keyParamPair : VAR? identDef annotation? ':' (typeExpr | TYPEAS identDef) ('=' dataExpr)? ;

identList    : identDef (',' identDef)* ;
valueList    : dataExpr (',' dataExpr)* ;
indexValues  : indexValue (',' indexValue)* ;
pairedValues : keyValuePair (',' keyValuePair)* ;
pairedParams : keyParamPair (',' keyParamPair)* ;

argumentList : indexValues (',' pairedValues)? | pairedValues ;

memberAccess : '[' dataExpr (':' dataExpr (':' dataExpr)?)? ']' ;

parentParams : '(' pairedParams? ','? ')' ; // for functor parameters definition
parentArgues : '(' argumentList? ','? ')' ; // for functor arguments
angledParams : '<' pairedParams? ','? '>' ; // for functor super parameters definition
angledValues : '<' argumentList? ','? '>' ; // for functor super arguments

dataExpr
    : WAIT? structExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '&=' | '|=') structExpr)?
    ;

pattern
    : identRef
    | literal
    | '(' (valueList | identList)? ','? ')'
    | '{' (pairedValues | identList)? ','? '}'
    | '_' // wildcard
    ;

matchCase
    : CASE pattern ('|' pattern)* '=>' blockExpr
    ;

catchClause
    : CATCH identDef ':' typeExpr stmtBlock
    ;

structExpr
    : logicalOrExpr
    | IF logicalOrExpr THEN blockExpr ELSE blockExpr
    | MATCH identRef '{' matchCase+ '}'
    | TRY stmtBlock catchClause+ (FINALLY stmtBlock)?
    ;

logicalOrExpr
    : logicalAndExpr ('||' logicalAndExpr)*
    ;

logicalAndExpr
    : equalityExpr ('&&' equalityExpr)*
    ;

equalityExpr
    : relationalExpr (('===' | '!==' | '==' | '!=') relationalExpr)*
    ;

relationalExpr
    : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
    ;

additiveExpr
    : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
    ;

multiplicativeExpr
    : nullableExpr (('^' | '*' | '/' | '%') nullableExpr)*
    ;

nullableExpr
    : unaryExpr (('??' | '!!') dataExpr)?
    ;

unaryExpr
    : linkExpr ((AS | IS) typeExpr)?
    | ('!' | '-' | '~') linkExpr
    ;

linkExpr
    : bindExpr (('->' | '?->') bindExpr)*
    ;

bindExpr
    : withExpr (('..' | '?..') withExpr)*
    ;

withExpr
    : annoExpr (('.' | '?.') annoExpr)*
    ;

annoExpr
    : primaryData ({isAdjacent()}? (memberAccess | parentArgues | angledValues | '!') | annotation)*
    ;

dictExpr
    : '{' (pairedValues ','?)? '}' // no list comprehension because the struct of dict is immutable
    ;

listExpr
    : '[' ((indexValues ','?) | dataExpr FOR identRef IN dataExpr (IF dataExpr)?)? ']'
    ;

primaryData
    : identRef
    | literal
    | listExpr
    | dictExpr
    | '(' dataExpr ')'        // if there is only one data, it will be recognized as a primary expression rather than a tuple
    | '(' valueList? ','? ')' // for tuple
    | lambdaExpr
    ;

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

typeExpr
    : unionType ('?' unionType?)?
    ;

unionType
    : unionUnit ('|' unionUnit)*
    ;

unionUnit : (identDef OF)? listType ;

listType
    : argsType ('[' ']')*
    ;

typeOrData : typeExpr | primaryData ;

argsType
    : primaryType ('<' typeOrData (',' typeOrData)* '>')?
    ;

primaryType
    : INNER_ATOM_TYPE
    | dictExprType
    | identRef
    | '(' typeExpr ')'
    | tupleType
    | lambdaType
    | TYPEOF dataExpr
    ;

dictExprType
    : dictType (('&' | '^') dictType)*
    ;

dictType
    : '{' (keyTypePair (',' keyTypePair)*)? ','? '}'
    ;

tupleType
    : '(' (typeExpr (',' typeExpr)*)? ','? ')'
    ;

lambdaType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;

identDef : IDENTIFIER ;
identRef : (IDENTIFIER '::')* IDENTIFIER ;
