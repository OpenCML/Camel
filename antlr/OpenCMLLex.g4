lexer grammar OpenCMLLex;
// 纯词法的语法声明
// PSL的词法规则定义，它定义了一些词法规则来识别PSL源码中的不同类型的词法单元。

SEP         : ';';
AS          : 'as';
LET         : 'let';
USE         : 'use';
FUNC        : 'func';
TYPE        : 'type';
ENUM        : 'enum';
WITH        : 'with';
RETURN      : 'return';

INNER       : 'inner';
OUTER       : 'outer';
SYNC        : 'sync';
SCOPED      : 'scoped';
STATIC      : 'static';
ATOMIC      : 'atomic';

NULL        : 'null';
TRUE        : 'true';
FALSE       : 'false';

ANY_TYPE    : 'any';

NUMBER_TYPE     : 'number';
STRING_TYPE     : 'string';
BOOLEAN_TYPE    : 'bool';
FUNCTOR_TYPE    : 'functor';
BLOCK_TYPE      : 'block';

INTEGER_TYPE    : 'int';
REAL_TYPE       : 'real';
COMPLEX_TYPE    : 'complex';

ARRAY_TYPE      : 'array';
MATRIX_TYPE     : 'matrix';
LIST_TYPE       : 'list';
DICT_TYPE       : 'dict';

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
 | '```' .*? '```'
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
