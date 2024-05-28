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

@parser::members {
bool isAdjacent() {
    const antlr4::Token *first = _input->LT(-1);
    const antlr4::Token *curr = _input->LT(1);
    if (first == nullptr || curr == nullptr)
        return false;
    if (first->getStopIndex() + 1 != curr->getStartIndex())
        return false;
    return true;
}
}

program : stmtList? EOF;

stmtList : stmt (SEP stmt)* SEP? ;

stmt
    : letStmt
    | useStmt
    | typeStmt
    | exprStmt
    | assignStmt
    | funcDef
    | retStmt
    ;

letStmt : LET carrier (':' typeExpr)? ('='? entityExpr)?
        | carrier (':' typeExpr)? ':=' entityExpr ;
useStmt : USE carrier '='? entityExpr
        | carrier '::' entityExpr ;
typeStmt : TYPE identRef '='? typeExpr ;
exprStmt : annotations? entityExpr ;
assignStmt : identRef memberAccess? '=' entityExpr ;

withDef : WITH angledParams ;
funcDef : annotations? withDef? modifiers? FUNC identRef parentParams (':' typeExpr)? bracedStmts ;
retStmt : RETURN entityExpr? ;
lambdaExpr : modifiers? ((parentParams (':' typeExpr)? '=>' (bracedStmts | entityExpr)) | '{' stmtList '}' ) ;

carrier : identRef | bracedIdents | bracketIdents ;

annotation  : '@' primEntity ;
annotations : annotation+ ;
modifiers   : (INNER | OUTER | ATOMIC | STATIC | SYNC)+ ;

keyTypePair  : identRef ':' typeExpr ;
keyValuePair : identRef ':' entityExpr ;
keyParamPair : identRef annotation? ':' typeExpr ('=' entityExpr)? ;

typeList     : typeExpr (',' typeExpr)* ;
identList    : identRef (',' identRef)* ;
valueList    : entityExpr (',' entityExpr)* ;
pairedTypes  : keyTypePair (',' keyTypePair)* ;
pairedValues : keyValuePair (',' keyValuePair)* ;
pairedParams : keyParamPair (',' keyParamPair)* ;
argumentList : valueList (',' pairedValues)? | pairedValues ;

bracedPairedValues : '{' pairedValues? ','? '}' ; // for literal construction of dict
bracedIdents       : '{' identList? ','? '}' ;    // for dict unpacking
bracedStmts        : '{' stmtList? '}' ;     // for block statement

bracketIdents : '[' identList? ','? ']' ; // for list unpacking
bracketValues : '[' valueList? ','? ']' ; // for literal construction of list, vector, or tensor

parentParams : '(' pairedParams? ','? ')' ; // for functor parameters definition
parentValues : '(' argumentList? ','? ')' ; // for functor arguments

angledParams : '<' pairedParams? ','? '>' ; // for functor super parameters definition
angledValues : '<' argumentList? ','? '>' ; // for functor super arguments

primEntity
    : identRef
    | literal
    | bracketValues
    | bracedPairedValues
    | lambdaExpr
    | '(' entityExpr ')' ;
memberAccess : '[' entityExpr ']' ;
entity       : primEntity (({isAdjacent()}? (memberAccess | angledValues | parentValues)) | annotation)* ;

entityChain  : entityLink+ ;
entityLink   : entityCall | entityLink '->' entityCall ;
entityCall   : entity | entityCall '.' entity ;
entitySpread : '...' entity ;

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

addExpr
    : multiExpr
    | addExpr '+' multiExpr
    | addExpr '-' multiExpr
    | addExpr '&' multiExpr
    | addExpr '|' multiExpr
    ;

multiExpr
    : unaryExpr
    | multiExpr '^' unaryExpr
    | multiExpr '*' unaryExpr
    | multiExpr '/' unaryExpr
    | multiExpr '%' unaryExpr
    | multiExpr AS typeExpr
    | multiExpr IS typeExpr
    ;

unaryExpr
    : primExpr
    | '!' primExpr
    | '~' primExpr
    ;

primExpr
    : entityChain
    | '(' entityExpr ')'
    ;

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
    : type ('[' INTEGER? ']')*
    | typeExpr '|' typeExpr
    | typeExpr '&' typeExpr
    ;

type
    : primType
    | structType
    | specialType
    | identRef
    | '(' typeExpr ')'
    | lambdaType
    ;

lambdaType
    : ('<' pairedParams? '>')? '(' pairedParams? ')' '=>' typeExpr
    ;

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

specialType
    : ANY_TYPE
    | VOID_TYPE
    | FUNCTOR_TYPE
    ;

identRef : IDENTIFIER ;
