grammar OpenCML;
import OpenCMLLex;

program : sepMark? stmtList? stmtSep+;

stmtList : stmt (stmtSep stmt)* ;

stmt : letStmt
     | useStmt
     | funcDef
     | typeDef
     | enumDef
     | retStmt
     | exprStmt
     ;

letStmt : LET carrier (':' type)? '='? entityExpr ;
useStmt : USE carrier '='? entityExpr ;
letSimple : carrier ':=' entityExpr ;
useSimple : carrier '=' entityExpr ;
withDef : WITH (entityRef | withDecl) LINE_END? ;
funcDef : annotations withDef? modifiers FUNC identRef paramDef ('->' type)? '='? LINE_END? stmtPack ;
typeDef : TYPE identRef '='? (type | typePack) ;
enumDef : ENUM identRef '='? dictUnpack ;
retStmt : RETURN entityExpr? ;
exprStmt : annotations entityExpr ;

carrier : identRef
        | listUnpack
        | dictUnpack
        ;

biasAnno : '(' INTEGER ',' INTEGER ')' ;
sizeAnno : '[' INTEGER ',' INTEGER ']' ;
annotation : '@' (identRef | dictPack | biasAnno | sizeAnno) ;
annotations : (annotation LINE_END)* ;
modifiers : (INNER | OUTER | SYNC | SCOPED | STATIC | ATOMIC)* ;
withList : '<' sepMark? argument (',' sepMark? argument)* sepMark? '>' ;
withDecl : '<' sepMark? keyValDecl (',' sepMark? keyValDecl)* sepMark? '>' ;
paramDef : '(' sepMark? (keyValDecl (',' sepMark? keyValDecl)*)? sepMark? ')' ;
argsList : '(' sepMark? (argument (',' sepMark? argument)*)? sepMark? ')' ;
argument : identRef | entity
         | keyValExpr | entityExpr
         ;

typePack : '{' sepMark? (keyValDecl (',' sepMark? keyValDecl)*)? sepMark? '}' ;
keyValDecl : identRef (annotation)? ':' nullableType ('=' entityExpr)? ;
keyValExpr : identRef '=' entityExpr ;

entityRef : identRef ('.' (INTEGER | identRef))* annotation? ;
functorRef: identRef (withList)? annotation? ;
listUnpack : '[' sepMark? (identRef (',' sepMark? identRef)*)? sepMark? ']' ;
dictUnpack : '{' sepMark? (identRef (',' sepMark? identRef)*)? sepMark? '}' ;
dictPack : '{' sepMark? (keyValExpr (',' sepMark? keyValExpr)*)? sepMark? '}' ;
listPack : '[' sepMark? (entityExpr (',' sepMark? entityExpr)*)? sepMark? ']' ;
stmtPack : '{' stmtList? sepMark? '}' ;
lambda   : (paramDef '=>')? stmtPack ;

entityExpr : (entity | normCall | linkCall | entityChain) (AS type)? ;
entityChain : (identRef | entity | linkCall)+ ;
entity : ((identRef | literal | listPack | dictPack | lambda) annotation?) | entityRef | functorRef ;
normCall : functorRef argsList ;
linkCall : linkCall '->' (identRef | functorRef | entity | normCall)
         | (identRef | entityRef | entity | functorRef | normCall)
           '->' (identRef | functorRef | entity | normCall)
         | identRef | entityRef | entity | functorRef | normCall
         ;

stmtSep : sepMark | ';' | EOF ;
sepMark : LINE_END+ ;


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
          | BLOCK_TYPE
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
structType : LIST_TYPE ('<' nullableType (',' nullableType)* '>')? ('[' INTEGER ']')?
           | DICT_TYPE ('<' type ',' nullableType '>')?
           ;
nullableType : type '?'? ;

identRef : IDENTIFIER ;
