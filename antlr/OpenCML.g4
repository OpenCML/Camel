grammar OpenCML;
import OpenCMLLex;

program : stmtList? ;

stmtList : (sepMark? stmt)+ ;

stmt : letStmt
     | useStmt
     | funcDef
     | typeDef
     | enumDef
     | retStmt
     | exprStmt
     ;

letStmt : LET carrier (':' type)? '='? entityExpr stmtEnd ;
useStmt : USE carrier '='? entityExpr stmtEnd ;
withDef : WITH (entityRef | withDecl) LINE_END? ;
funcDef : annotations withDef? modifiers FUNC identRef paramDef ('->' type)? '='? LINE_END? stmtPack stmtEnd ;
typeDef : TYPE identRef '='? (type | typePack) stmtEnd ;
enumDef : ENUM identRef '='? dictUnpack stmtEnd ;
retStmt : RETURN entityExpr? stmtEnd ;
exprStmt : annotations entityExpr stmtEnd ;

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

entityExpr : (entity | normCall | linkCall | entityChain) (AS type)? ;
entityChain : (identRef | entity | linkCall)+ ;
entity : ((identRef | literal | listPack | dictPack) annotation?) | entityRef | functorRef ;
normCall : functorRef argsList ;
linkCall : linkCall '->' (identRef | functorRef | entity | normCall)
         | (identRef | entityRef | entity | functorRef | normCall)
           '->' (identRef | functorRef | entity | normCall)
         | identRef | entityRef | entity | functorRef | normCall
         ;

stmtEnd : sepMark | ';' | EOF ;
sepMark : LINE_END+ ;


literal : value
        | STRING
        | MULTI_STR
        | FSTRING
        | NULL
        | TRUE
        | FALSE
        ;
value : (INTEGER | REAL | complex) UNIT? ;


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
complex    : INTEGER ('+' | '-') INTEGER 'i'
           | REAL ('+' | '-') REAL 'i'
           ;
vectorType : ARRAY_TYPE ('<' scalarType '>')? ('[' INTEGER ']')?
           | MATRIX_TYPE ('<' scalarType '>')? ('[' INTEGER']')*
              ;
structType : LIST_TYPE ('<' nullableType (',' nullableType)* '>')? ('[' INTEGER ']')?
           | DICT_TYPE ('<' type ',' nullableType '>')?
           ;
nullableType : type '?'? ;

identRef : IDENTIFIER ;
