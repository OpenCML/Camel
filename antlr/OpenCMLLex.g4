lexer grammar OpenCMLLex;
// 纯词法的语法声明
// PSL的词法规则定义，它定义了一些词法规则来识别PSL源码中的不同类型的词法单元。

SEP         : ';';
AS          : 'as';
IS          : 'is';
LET         : 'let';
USE         : 'use';
FUNC        : 'func';
TYPE        : 'type';
WITH        : 'with';
RETURN      : 'return';

INNER       : 'inner';
OUTER       : 'outer';
ATOMIC      : 'atomic';
STATIC      : 'static';
SYNC        : 'sync';

NULL        : 'null';
TRUE        : 'true';
FALSE       : 'false';

INTEGER_TYPE    : 'Int';
INTEGER32_TYPE  : 'Int32';
INTEGER64_TYPE  : 'Int64';
REAL_TYPE       : 'Real';
FLOAT_TYPE      : 'Float';
DOUBLE_TYPE     : 'Double';
NUMBER_TYPE     : 'Number';
STRING_TYPE     : 'String';
BOOL_TYPE       : 'Bool';
CHAR_TYPE       : 'Char';

SET_TYPE        : 'Set';
MAP_TYPE        : 'Map';
LIST_TYPE       : 'List';
DICT_TYPE       : 'Dict';
TUPLE_TYPE      : 'Tuple';
VECTOR_TYPE     : 'Vector';
TENSOR_TYPE     : 'Tensor';

ANY_TYPE      : 'Any';
VOID_TYPE     : 'Void';
FUNCTOR_TYPE  : 'Functor';

SKIP_
    : ( BLANK | LIN_CMT | BLK_CMT ) -> skip
    ;

fragment BLANK
    : [ \n\r\t\u000C]+
    ;

fragment LIN_CMT
    : '//' ~[\r\n]*
    | '# ' ~[\r\n\f]*
    ;

fragment BLK_CMT
    : '/*' .*? '*/'
    ;

MULTI_STR
    : '\'\'\'' .*? '\'\'\''
    | '"""' .*? '"""'
    ;

IDENTIFIER
    : [a-zA-Z_] [a-zA-Z_0-9]*
    ;

UNIT
    : '`' ('\\`' | '\\\\' | .)*? '`'
    ;

STRING
    : '"' ('\\"' | '\\\\' | .)*? '"'
    | '\'' ('\\\'' | '\\\\' | .)*? '\''
    ;

FSTRING
 : 'f' STRING
 ;

INTEGER
    : DECIMAL
    | OCTAL
    | HEXADECIMAL
    | BINARY
    | EXPONENT_DECIMAL
    ;

REAL
    : FLOAT
    | EXPONENT_FLOAT
    ;

fragment DECIMAL
    : [+-]? ([1-9] [0-9]* | '0')
    ;

fragment OCTAL
    : [+-]? '0' [0-7]+
    ;

fragment HEXADECIMAL
    : [+-]? '0x' [0-9a-fA-F]+
    ;

fragment BINARY
    : [+-]? '0b' [01]+
    ;

fragment FLOAT
    : [+-]? [0-9]+ '.' [0-9]+
    ;

fragment EXPONENT_FLOAT
    : FLOAT EXPONENT
    ;

fragment EXPONENT_DECIMAL
    : DECIMAL EXPONENT
    ;

fragment EXPONENT
    : [eE] [+-]? [0-9]+
    ;
