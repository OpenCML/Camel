grammar OpenCML;
import OpenCMLLex;

@header {
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

program : stmtList? EOF;

stmtList : stmt (SEP? stmt)* SEP? ;

stmt
    : letStmt
    | useStmt
    | typeStmt
    | exprStmt
    | waitStmt
    | funcDef
    | retStmt
    ;

letStmt    : (LET | VAR) carrier (':' typeExpr)? '=' entityExpr ;
useStmt    : USE (identRef | bracedIdents | '*') FROM STRING ;
typeStmt   : TYPE identRef '=' typeExpr ;
exprStmt   : annotations? entityExpr ;
waitStmt   : WAIT entityExpr ;

withDef    : WITH angledParams ;
funcDecl   : annotations? withDef? modifiers? FUNC identRef parentParams (':' typeExpr)? ;
funcDef    : funcDecl bracedStmts ;
retStmt    : RETURN entityExpr? ;
lambdaExpr : modifiers? angledParams? parentParams (':' typeExpr)? '=>' (bracedStmts | entityExpr) ;

carrier    : identRef | bracedIdents | bracketIdents ;

annotation  : '@' primaryExpr ;
annotations : annotation+ ;
modifiers   : (INNER | OUTER | ATOMIC | SHARED | SYNC)+ ;

keyTypePair  : identRef ':' typeExpr ;
keyValuePair : identRef ':' entityExpr ;
keyParamPair : VAR? identRef annotation? ':' typeExpr ('=' entityExpr)? ;
indexKTPair  : '[' typeExpr ']' ':' typeExpr ;
indexKVPair  : '[' entityExpr ']' ':' entityExpr ;

typeList     : typeExpr (',' typeExpr)* ;
identList    : identRef (',' identRef)* ;
valueList    : entityExpr (',' entityExpr)* ;
pairedTypes  : keyTypePair (',' keyTypePair)* ;
pairedValues : keyValuePair (',' keyValuePair)* ;
pairedParams : keyParamPair (',' keyParamPair)* ;
indexKVPairs : indexKVPair (',' indexKVPair)* ;

argumentList : valueList (',' pairedValues)? | pairedValues ;

bracedPairedValues : '{' pairedValues? ','? '}' ; // for literal construction of dict
bracedIdents       : '{' identList? ','? '}' ;    // for dict unpacking
bracedStmts        : '{' stmtList? '}' ;          // for block statement
bracedValues       : '{' valueList? ','? '}' ;     // for literal construction of set
bracedIndexKVPairs : '{' indexKVPairs? ','? '}' ;  // for literal construction of map

bracketIdents      : '[' identList? ','? ']' ;    // for list unpacking
bracketValues      : '[' valueList? ','? ']' ;    // for literal construction of list (variable length)
memberAccess       : '[' entityExpr ']' ;

parentParams       : '(' pairedParams? ','? ')' ; // for functor parameters definition
parentArgues       : '(' argumentList? ','? ')' ; // for functor arguments
parentValues       : '(' valueList? ','? ')' ;    // for literal construction of tuple (fixed length)

angledParams       : '<' pairedParams? ','? '>' ; // for functor super parameters definition
angledValues       : '<' argumentList? ','? '>' ; // for functor super arguments

entityExpr
    : ternaryExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '&=' | '|=') ternaryExpr)?
    ;

ternaryExpr
    : logicalOrExpr ('?' ternaryExpr ':' ternaryExpr)?
    ;

logicalOrExpr
    : logicalAndExpr ('||' logicalAndExpr)*
    ;

logicalAndExpr
    : equalityExpr ('&&' equalityExpr)*
    ;

equalityExpr
    : relationalExpr (('==' | '!=') relationalExpr)*
    ;

relationalExpr
    : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
    ;

additiveExpr
    : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
    ;

multiplicativeExpr
    : unaryExpr (('^' | '*' | '/' | '%' | AS | IS) unaryExpr)*
    ;

unaryExpr
    : linkExpr
    | ('!' | '-' | '~') linkExpr
    ;

linkExpr
    : withExpr ('->' withExpr)*
    ;

withExpr
    : annotatedExpr ('.' annotatedExpr)*
    ;

annotatedExpr
    : primaryExpr ({isAdjacent()}? (memberAccess | parentArgues | angledValues) | annotation)*
    ;

primaryExpr
    : identRef
    | literal
    | bracketValues         // for list
    | bracedPairedValues    // for dict
    | '(' entityExpr ')'    // if there is only one entity, it will be recognized as a primary expression rather than a tuple
    | parentValues          // for tuple
    // for vector | array | tensor | set | map
    | '<' typeExpr (',' (typeExpr | INTEGER | '[' INTEGER (',' INTEGER)* ']'))? '>' (bracketValues | bracedValues | bracedIndexKVPairs)
    | lambdaExpr ;

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

typeExpr
    : arrayType (('&' | '|' | '^') arrayType)*
    ;

arrayType
    : atomType ('[' INTEGER? ']')*
    ;

atomType
    : primaryType
    | structType
    | specialType
    | identRef
    | '(' typeExpr ')'
    | lambdaType
    ;

lambdaType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;

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

specialType
    : ANY_TYPE
    | VOID_TYPE
    | FUNCTOR_TYPE
    ;

identRef : IDENTIFIER ;
