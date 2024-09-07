grammar OpenCML;
import OpenCMLLex;

@header {
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
 * Supported by: National Key Research and Development Program of China
 */
}

program : stmtList? EOF;

stmtList : stmt (SEP stmt)* SEP? ;

stmt
    : letStmt
    | useStmt
    | typeStmt
    | exprStmt
    | waitStmt  
    | funcDef
    | retStmt
    ;

letStmt    : (LET | VAR) carrier (':' typeExpr)? '=' entityExpr
           | carrier (':' typeExpr)? ':=' entityExpr ;
useStmt    : USE carrier ('=' entityExpr | FROM STRING);
typeStmt   : TYPE identRef '=' typeExpr ;
exprStmt   : annotations? entityExpr ;
waitStmt   : WAIT entityExpr ;

withDef    : WITH angledParams ;
funcDef    : annotations? withDef? modifiers? FUNC identRef parentParams (':' typeExpr)? bracedStmts ;
retStmt    : RETURN entityExpr? ;
lambdaExpr : modifiers? parentParams (':' typeExpr)? '=>' (bracedStmts | entityExpr) ;

carrier    : identRef | bracedIdents | bracketIdents ;

annotation  : '@' primaryExpr ;
annotations : annotation+ ;
modifiers   : (INNER | OUTER | ATOMIC | STATIC)+ ;

keyTypePair  : identRef ':' typeExpr ;
keyValuePair : identRef ':' entityExpr ;
keyParamPair : VAR? identRef annotation? ':' typeExpr ('=' entityExpr)? ;
indexKTPair  : '[' entityExpr ']' ':' typeExpr ;
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
bracedHomoValues   : '{|' valueList ','? '|}' ;   // for literal construction of set (at least one value, otherwise the element type is unknown)
bracedIndexKVPairs : '{' indexKVPairs ','? '}' ;  // for literal construction of map (at least one pair, otherwise the key type is unknown)

bracketIdents      : '[' identList? ','? ']' ;    // for list unpacking
bracketHomoValues  : '[|' valueList? ','? '|]' ;  // for literal construction of vector (variable length, homogeneous)
bracketHeteValues  : '[' valueList? ','? ']' ;    // for literal construction of list (variable length, heterogeneous)
memberAccess       : '[' entityExpr ']' ;

parentParams       : '(' pairedParams? ','? ')' ; // for functor parameters definition
parentValues       : '(' argumentList? ','? ')' ; // for functor arguments
parentHomoValues   : '(|' valueList? ','? '|)' ;  // for literal construction of array (fixed length, homogeneous)
parentHeteValues   : '(' valueList? ','? ')' ;    // for literal construction of tuple (fixed length, heterogeneous)

angledParams       : '<' pairedParams? ','? '>' ; // for functor super parameters definition
angledValues       : '<' argumentList? ','? '>' ; // for functor super arguments

entityExpr
    : ternaryExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '&=' | '|=') ternaryExpr)*
    ;

ternaryExpr
    : relationalExpr ('?' ternaryExpr ':' ternaryExpr)?
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
    | ('!' | '~') linkExpr
    ;

linkExpr
    : withExpr ('->' withExpr)*
    ;

withExpr
    : annotatedExpr ('.' annotatedExpr)*
    ;

annotatedExpr
    : primaryExpr (memberAccess | parentValues | angledValues | annotation)*
    ;

primaryExpr
    : identRef
    | literal
    | bracedIndexKVPairs    // for map
    | bracedHomoValues      // for set
    | bracedPairedValues    // for dict
    | bracketHeteValues     // for list
    | bracketHomoValues     // for vector
    | '(' entityExpr ')'    // if there is only one entity, it will be recognized as a primary expression rather than a tuple
    | parentHeteValues      // for tuple
    | parentHomoValues      // for array
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
    : ('<' pairedParams? '>')? '(' pairedParams? ')' '=>' typeExpr
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
    | LIST_TYPE
    | DICT_TYPE                 // universal dict type
    | ARRAY_TYPE ('<' typeExpr '>')?
    | TUPLE_TYPE ('<' typeList? ','? '>')?
    | UNION_TYPE ('<' typeList? ','? '>')?
    | VECTOR_TYPE ('<' typeExpr (',' INTEGER)? '>')?
    | TENSOR_TYPE ('<' typeExpr (',' '[' INTEGER (',' INTEGER)* ']')? '>')?
    | '{' pairedTypes? ','? '}' // concrete dict type
    | '{' indexKTPair '}'       // concrete map type
    ;

specialType
    : ANY_TYPE
    | VOID_TYPE
    | FUNCTOR_TYPE
    ;

identRef : IDENTIFIER ;
