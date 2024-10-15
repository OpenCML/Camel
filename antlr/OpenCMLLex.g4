lexer grammar OpenCMLLex;
// 纯词法的语法声明
// PSL的词法规则定义，它定义了一些词法规则来识别PSL源码中的不同类型的词法单元。

SEP         : ';';
AS          : 'as';
IS          : 'is';
LET         : 'let';
VAR         : 'var';
USE         : 'use';
FROM        : 'from';
FUNC        : 'func';
TYPE        : 'type';
WITH        : 'with';
WAIT        : 'wait';
RETURN      : 'return';

INNER       : 'inner';
OUTER       : 'outer';
ATOMIC      : 'atomic';
SHARED      : 'shared';
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
ARRAY_TYPE      : 'Array';
TUPLE_TYPE      : 'Tuple';
UNION_TYPE      : 'Union';
VECTOR_TYPE     : 'Vector';
TENSOR_TYPE     : 'Tensor';

ANY_TYPE      : 'Any';
VOID_TYPE     : 'Void';
FUNCTOR_TYPE  : 'Functor';

BLANK
    : [ \r\n\t\u000C]+ -> skip
    ;

LIN_CMT_HASH
    : '#' ~[\r\n\f]* -> channel(2)
    ;

LIN_CMT_SLASH
    : '//' ~[\r\n]* -> channel(3)
    ;

BLK_CMT
    : '/*' .*? '*/' -> channel(4)
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
