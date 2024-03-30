grammar OpenCML;
import OpenCMLLex;

program : stmtList? EOF;

stmtList : stmt (SEP stmt)* SEP? ;

stmt : letStmt
     | useStmt
     | funcDef
     | typeDef
     | enumDef
     | retStmt
     | exprStmt
     | assignStmt
     ;

letStmt : LET carrier (':' type)? '='? expr
        | carrier (':' type)? ':=' expr ;
useStmt : USE carrier '='? expr
        | carrier '::' expr ;
withDef : WITH (entityRef | withDecl) ;
funcDef : annotations withDef? modifiers FUNC identRef paramDef ('->' type)? '='? stmtPack ;
typeDef : TYPE identRef '='? (type | typePack) ;
enumDef : ENUM identRef '='? dictUnpack ;
retStmt : RETURN expr? ;
exprStmt : annotations expr ;
assignStmt : entityRef (':' type)? '='? expr ;

carrier : identRef
        | listUnpack
        | dictUnpack
        ;

biasAnno : '(' expr ',' expr ')' ;
sizeAnno : '[' expr ',' expr ']' ;
annotation : '@' (identRef | dictPack | biasAnno | sizeAnno) ;
annotations : (annotation SEP?)* ;
modifiers : (INNER | OUTER | SYNC | SCOPED | STATIC | ATOMIC)* ;
withList : '<' argument (','  argument)* ','? '>' ;
withDecl : '<' keyValDecl (',' keyValDecl)* ','? '>' ;
paramDef : '(' (keyValDecl (',' keyValDecl)*)? ','? ')' ;
argsList : '(' (argument (',' argument)*)? ','? ')' ;
argument : identRef | entity
         | keyValExpr | expr
         ;

typePack : '{' (keyValDecl (',' keyValDecl)*)? ','? '}' ;
keyValDecl : identRef '?'? annotation? ':' type ('=' expr)? ;
keyValExpr : identRef annotation? '=' expr ;

entityRef : identRef ('.' (INTEGER | identRef))* annotation? ;
functorRef: identRef (withList)? annotation? ;
listUnpack : '[' (identRef (',' identRef)*)? ','? ']' ;
dictUnpack : '{' (identRef (',' identRef)*)? ','? '}' ;
dictPack : '{' (keyValExpr (',' keyValExpr)*)? ','? '}' ;
listPack : '[' (expr (',' expr)*)? ','? ']' ;
stmtPack : '{' stmtList?  '}' ;
lambda   : (paramDef '=>')? stmtPack ;

entityExpr : (entity | normCall | linkCall | entityChain) (AS type)? ;
entityChain : (identRef | entity | linkCall)+ ;
entity : ((identRef | literal | listPack | dictPack | lambda) annotation?) | entityRef | functorRef ;
normCall : functorRef argsList ;
linkCall : linkCall '->' (identRef | functorRef | entity | normCall)
         | identRef | entityRef | entity | functorRef | normCall
         ;

calcExpr
    : relaExpr
    | calcExpr '*=' relaExpr
    | calcExpr '/=' relaExpr
    | calcExpr '%=' relaExpr
    | calcExpr '+=' relaExpr
    | calcExpr '-=' relaExpr
    ;

relaExpr
    : addExpr
    | relaExpr '<' addExpr
    | relaExpr '>' addExpr
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
    | '++' primExpr
    | '--' primExpr
    | primExpr '++'
    | primExpr '--'
    | '!' primExpr
    | '-' primExpr
    ;

primExpr
    : literal
    | entity
    | entityExpr
    | '(' calcExpr ')'
    ;

expr : entityExpr | calcExpr ;

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
