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

stmt
    : letDecl
    | useDecl
    | funcDecl
    | typeDecl
    | enumDecl
    | retStmt
    | waitStmt
    | dataExpr
    | stmtBlock
    ;

stmtList : stmt (SEP? stmt)* SEP? ;

moduleDecl : MODULE identDef ;
importDecl : IMPORT (STRING | (identDef | bracedIdents) FROM STRING) ;
exportDecl : EXPORT (letDecl | typeDecl | bracedIdents) ;

stmtBlock  : SYNC? '{' stmtList? '}' ;
blockExpr  : stmtBlock | waitExpr ;
waitStmt   : WAIT (stmtBlock | dataList) ;
lambdaExpr : modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr ;
funcDecl   : (WITH angledParams)? EXPORT? implMark? modifiers? FUNC identDef parentParams (':' typeExpr)? stmtBlock ;

parentIdents  : '(' identList? ','? ')' ;    // for tuple unpacking
bracedIdents  : '{' identList? ','? '}' ;    // for dict unpacking
bracketIdents : '[' identList? ','? ']' ;    // for list unpacking
carrier       : identList | parentIdents | bracedIdents | bracketIdents ;

letDecl    : (LET | VAR) carrier (':' typeList)? '=' valueList ;
useDecl    : USE (identDef '=')? identRef ;
retStmt    : (RETURN | RAISE | THROW) valueList ;
typeDecl   : implMark? TYPE identDef '=' (typeExpr | STRING) ;
enumDecl   : ENUM identDef (OF typeExpr)? '=' '{' pairedValues ','? '}' ;

implMark    : INNER | OUTER ;
modifiers   : (ATOMIC | SHARED | SYNC | MACRO)+ ;

indexValue   : '...'? waitExpr ;
keyTypePair  : identDef ':' typeExpr ;
keyValuePair : identDef ':' waitExpr | '...' waitExpr ;
keyParamPair : VAR? identDef ':' typeExpr ('=' waitExpr)? ;

dataList     : dataExpr (',' dataExpr)* ;
identList    : identDef (',' identDef)* ;
valueList    : waitExpr (',' waitExpr)* ;
indexValues  : indexValue (',' indexValue)* ;
pairedValues : keyValuePair (',' keyValuePair)* ;
pairedParams : keyParamPair (',' keyParamPair)* ;

argumentList : indexValues (',' pairedValues)? | pairedValues ;

memberAccess : '[' waitExpr (':' waitExpr (':' waitExpr)?)? ']' ;

parentParams : '(' pairedParams? ','? ')' ; // for functor parameters definition
parentArgues : '(' argumentList? ','? ')' ; // for functor arguments
angledParams : '<' pairedParams? ','? '>' ; // for functor super parameters definition
angledValues : '<' argumentList? ','? '>' ; // for functor super arguments

waitExpr : WAIT? dataExpr ;

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

ctrlExpr
    : IF logicalOrExpr THEN blockExpr (ELSE blockExpr)?
    | MATCH identRef '{' matchCase+ '}'
    | TRY stmtBlock catchClause+ (FINALLY stmtBlock)?
    ;

dataExpr
    : assignExpr
    | ctrlExpr
    ;

assignExpr
    : logicalOrExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&=' | '|=') logicalOrExpr)?
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
    : nullableExpr (('*' | '/' | '^' | '@' | '%') nullableExpr)*
    ;

nullableExpr
    : unaryExpr (('??' | '!!') waitExpr)?
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
    : primaryData ({isAdjacent()}? (memberAccess | parentArgues | angledValues | '!'))*
    ;

dictExpr
    : '{' (pairedValues ','?)? '}' // no list comprehension because the struct of dict is immutable
    ;

listExpr
    : '[' ((indexValues ','?) | waitExpr FOR identRef IN waitExpr (IF waitExpr)?)? ']'
    ;

primaryData
    : identRef
    | literal
    | listExpr
    | dictExpr
    | '(' waitExpr ')'
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
    | TYPEOF waitExpr
    | TYPEAS identDef
    ;

dictExprType
    : dictType (('&' | '^') dictType)*
    ;

dictType
    : '{' (keyTypePair (',' keyTypePair)*)? ','? '}'
    ;

typeList
    : typeExpr (',' typeExpr)*
    ;

tupleType
    : '(' typeList? ','? ')'
    ;

lambdaType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;

identDef : IDENTIFIER ;
identRef : (IDENTIFIER '::')* IDENTIFIER ;
