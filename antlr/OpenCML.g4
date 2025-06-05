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
    | dataDecl
    | funcDecl
    | typeDecl
    | useDecl
    ;

//executable statement
stmt
    : dataDecl
    | funcDecl
    | typeDecl
    | dataExpr
    | useDecl
    | retStmt
    | blockStmt
    ;

stmtList : stmt (SEP? stmt)* SEP? ;

moduleDecl : MODULE identDef ;
importDecl : IMPORT (STRING | (identDef | bracedIdents) FROM STRING) ;
exportDecl : EXPORT (dataDecl | typeDecl | bracedIdents) ;

blockStmt  : WAIT? stmtBlock ;
stmtBlock  : SYNC? '{' stmtList? '}' ;
blockExpr  : stmtBlock | dataExpr ;
funcData   : modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr ;
funcDecl   :
        (WITH angledParams)?
        EXPORT? implMark? modifiers? 
        FUNC identDef parentParams (':' typeExpr)? stmtBlock ;

parentIdents  : '(' identList? ','? ')' ;    // for tuple unpacking
bracedIdents  : '{' identList? ','? '}' ;    // for dict unpacking
bracketIdents : '[' identList? ','? ']' ;    // for list unpacking
carrier       : identList | parentIdents | bracedIdents | bracketIdents ;

dataDecl   : (LET | VAR) carrier (':' typeList)? '=' dataList ;
typeDecl   : implMark? TYPE identDef '=' (typeExpr | STRING) ;
useDecl    : USE (identDef '=')? identRef ;
retStmt    : (RETURN | RAISE | THROW) dataList ;

implMark    : INNER | OUTER ;
modifiers   : (ATOMIC | SHARED | SYNC | MACRO)+ ;

indexValue   : '...'? dataExpr ;
keyTypePair  : identDef ':' typeExpr ;
keyValuePair : identDef ':' dataExpr | '...' dataExpr ;
keyParamPair : VAR? identDef ':' typeExpr ('=' dataExpr)? ;

dataList     : dataExpr (',' dataExpr)* ;
identList    : identDef (',' identDef)* ;
indexValues  : indexValue (',' indexValue)* ;
pairedValues : keyValuePair (',' keyValuePair)* ;
pairedParams : keyParamPair (',' keyParamPair)* ;

argumentList : indexValues (',' pairedValues)? | pairedValues ;

memberAccess : '[' dataExpr (':' dataExpr (':' dataExpr)?)? ']' ;

parentParams : '(' pairedParams? ','? ')' ; // for functor parameters definition
parentArgues : '(' argumentList? ','? ')' ; // for functor arguments
angledParams : '<' pairedParams? ','? '>' ; // for functor super parameters definition
angledValues : '<' argumentList? ','? '>' ; // for functor super arguments

pattern
    : identRef
    | literal
    | '(' (dataList | identList)? ','? ')'
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
    : waitExpr
    | ctrlExpr
    ;

waitExpr : WAIT? assignExpr ;

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
    : primaryData ({isAdjacent()}? (memberAccess | parentArgues | angledValues | '!'))*
    ;

dictData
    : '{' (pairedValues ','?)? '}' // no list comprehension because the struct of dict is immutable
    ;

listData
    : '[' ((indexValues ','?) | dataExpr FOR identRef IN dataExpr (IF dataExpr)?)? ']'
    ;

tupleData
    : '(' dataList? ','? ')'
    ;

primaryData
    : identRef
    | literal
    | listData
    | dictData
    | '(' dataExpr ')'
    | tupleData
    | funcData
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
    : interType ('|' interType)*
    ;

interType
    : diffType ('&' diffType)*
    ;

diffType
    : keyUnionDiffType ('\\' keyUnionDiffType)*
    ;

keyUnionDiffType
    : keyInterType (('+' | '-') keyInterType)*
    ;

keyInterType
    : typeUnit ('^' typeUnit)*
    ;

typeUnit : (identDef OF)? listType ;

listType
    : specType ('[' ']')*
    ;

typeOrData : typeExpr | CONST dataExpr ;

specType
    : primaryType ('<' typeOrData (',' typeOrData)* '>')?
    ;

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

dictType
    : '{' (keyTypePair (',' keyTypePair)*)? ','? '}'
    ;

typeList
    : typeExpr (',' typeExpr)*
    ;

tupleType
    : '(' typeList? ','? ')'
    ;

funcType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;

identDef : IDENTIFIER ;
identRef : (IDENTIFIER '::')* IDENTIFIER ;
