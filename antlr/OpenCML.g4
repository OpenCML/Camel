grammar OpenCML;
import OpenCMLLex;

program : stmtList? EOF;

stmtList : stmt+ ;

stmt : letStmt SEP
     | useStmt SEP
     | funcDef SEP
     | retStmt SEP
     | exprStmt SEP
     | assignStmt SEP
     ;

letStmt : LET carrier (':' type)? '='? expr
        | carrier (':' type)? ':=' expr ;
useStmt : USE carrier '='? expr
        | carrier '::' expr ;

exprStmt : annotations? expr ;
assignStmt : entity '=' expr ;

withDef : WITH (entity | withDecl) ;
funcDef : annotations? withDef? modifiers? FUNC identRef argsDecl (':' type)? stmtPack ;
retStmt : RETURN expr? ;

lambda : modifiers? (argsDecl (':' type)? '=>')? stmtPack ;

carrier : identRef | listPack | dictPack ;

annotation : '@' carrier ;
annotations : (annotation SEP?)+ ;
modifiers : (INNER | OUTER | SYNC | SCOPED | STATIC | ATOMIC)+ ;

argument : expr | (identRef '=' expr) | entityUnpack ;
argsList : argument (',' argument)* ;
exprList : (expr | entityUnpack) (',' (expr | entityUnpack))* ;

keyValExpr : identRef '?'? annotation? (':' type)? ('=' expr)? ;
keyValList : (keyValExpr | entityUnpack) (',' (keyValExpr | entityUnpack))* ;

dictPack : '{' keyValList? '}' ;
listPack : '[' exprList? ']' ;
withDecl : '<' keyValList? '>' ;
withPack : '<' argsList? '>' ;
argsDecl : '(' keyValList? ')' ;
argsPack : '(' argsList? ')' ;
stmtPack : '{' stmtList? '}' ;

primEntity : identRef | literal | listPack | dictPack | lambda ;
memberAccess : ('.' (identRef | INTEGER | '(' expr ')'))+ ;
entity : primEntity memberAccess? withPack? argsPack? annotation? ;

entityLink : entityLink '->' entity | entity ;
entityChain : entityLink+ ;
entityUnpack : '...' entity ;
entityExpr : entityChain (AS type)? ;

expr
    : relaExpr
    | expr '*=' relaExpr
    | expr '/=' relaExpr
    | expr '%=' relaExpr
    | expr '+=' relaExpr
    | expr '-=' relaExpr
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
    ;

multiExpr
    : unaryExpr
    | multiExpr '^' unaryExpr
    | multiExpr '*' unaryExpr
    | multiExpr '/' unaryExpr
    | multiExpr '%' unaryExpr
    ;

unaryExpr
    : primExpr
    | '!' primExpr
    | '-' primExpr
    ;

primExpr
    : entityExpr
    | '(' expr ')'
    ;

literal : value
        | STRING
        | MULTI_STR
        | FSTRING
        | NULL
        | TRUE
        | FALSE
        ;

value : (INTEGER | REAL) UNIT? ;


type : innerType
     | identRef
     | ANY_TYPE
     ;

innerType : NUMBER_TYPE
          | STRING_TYPE
          | BOOLEAN_TYPE
          | FUNCTOR_TYPE
          | numberType
          | structType
          ;
numberType : scalarType
           | vectorType
           ;
scalarType : INTEGER_TYPE
           | REAL_TYPE
           | COMPLEX_TYPE
           ;
vectorType : ARRAY_TYPE ('<' scalarType '>')? ('[' INTEGER ']')?
           | MATRIX_TYPE ('<' scalarType '>')? ('[' INTEGER']')*
           ;
structType : LIST_TYPE ('<' type (',' type)* '>')? ('[' INTEGER ']')?
           | DICT_TYPE ('<' type ',' type '>')?
           ;

identRef : IDENTIFIER ;
