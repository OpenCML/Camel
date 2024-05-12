grammar OpenCML;
import OpenCMLLex;

program : stmtList? EOF;

stmtList : stmt+ ;

stmt
    : letStmt SEP
    | useStmt SEP
    | typeStmt SEP
    | exprStmt SEP
    | assignStmt SEP
    | funcDef SEP
    | retStmt SEP
    ;

letStmt : LET carrier (':' typeExpr)? '='? entityExpr
        | carrier (':' typeExpr)? ':=' entityExpr ;
useStmt : USE carrier '='? entityExpr
        | carrier '::' entityExpr ;
typeStmt : TYPE identRef '='? typeExpr ;
exprStmt : annotations? entityExpr ;
assignStmt : identRef memberAccess? '=' entityExpr ;

withDef : WITH angledParams ;
funcDef : annotations? withDef? modifiers? FUNC identRef parentParams (':' typeExpr)? bracedStmts ;
retStmt : RETURN entityExpr? ;

lambdaExpr : modifiers? (parentParams (':' typeExpr)? '=>')? bracedStmts ;

carrier : identRef | bracedIdents | bracketIdents ;

annotation  : '@' primEntity ;
annotations : (annotation SEP?)+ ;
modifiers   : (INNER | OUTER | ATOMIC | STATIC | SYNC)+ ;

keyTypePair  : identRef ':' typeExpr ;
keyValuePair : entityExpr ':' entityExpr ;
keyParamPair : identRef annotation? ':' typeExpr ('=' entityExpr)? ;
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

bracedValues       : '{' valueList? ','? '}' ;    // for literal construction of set
bracedIndexKVPairs : '{' indexKVPairs? ','? '}' ; // for literal construction of map
bracedPairedValues : '{' pairedValues? ','? '}' ; // for literal construction of dict
bracedIdents       : '{' identList? ','? '}' ;    // for dict unpacking
bracedStmts        : '{' stmtList? ','? '}' ;     // for block statement

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
    | bracedValues
    | bracedPairedValues
    | bracedIndexKVPairs
    | lambdaExpr
    | '(' entityExpr ')' ;
memberAccess : '[' entityExpr ']' ;
entity       : primEntity (memberAccess | angledValues | annotation | parentValues)* ;

entityChain  : entityLink+ ;
entityLink   : entityCall | entityLink '->' entityCall ;
entityCall   : entity | entityCall '.' entity ;
entitySpread : '...' entity ;

entityExpr
    : relaExpr
    | entityExpr '*=' relaExpr
    | entityExpr '/=' relaExpr
    | entityExpr '%=' relaExpr
    | entityExpr '+=' relaExpr
    | entityExpr '-=' relaExpr
    ;

relaExpr
    : addExpr
    | relaExpr '<<' addExpr
    | relaExpr '>>' addExpr
    | relaExpr '<=' addExpr
    | relaExpr '>=' addExpr
    | relaExpr '==' addExpr
    | relaExpr '!=' addExpr
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
    | '-' primExpr
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
    : type ('[' ']')?
    | typeExpr '|' typeExpr
    | typeExpr '&' typeExpr
    ;

type
    : primType
    | structType
    | specialType
    | identRef
    | '(' typeExpr ')'
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
    | '{' indexKTPair '}' // concrete map type
    | LIST_TYPE
    | DICT_TYPE // universal dict type
    | '{' pairedTypes? ','? '}' // concrete dict type
    | ARRAY_TYPE ('<' typeExpr '>')?
    | TUPLE_TYPE ('<' typeList? ','? '>')?
    | VECTOR_TYPE ('<' typeExpr (',' INTEGER)? ','? '>')?
    | TENSOR_TYPE ('<' typeExpr (',' '[' INTEGER (',' INTEGER)* ']')? ','? '>')?
    ;

specialType
    : ANY_TYPE
    | VOID_TYPE
    | FUNCTOR_TYPE
    ;

identRef : IDENTIFIER ;
