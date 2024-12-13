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
ENUM        : 'enum';
WITH        : 'with';
WAIT        : 'wait';
RETURN      : 'return';
MODULE      : 'module';
IMPORT      : 'import';
EXPORT      : 'export';
TYPEAS      : 'typeas';
TYPEOF      : 'typeof';

IF          : 'if';
THEN        : 'then';
ELSE        : 'else';
MATCH       : 'match';
CASE        : 'case';
FOR         : 'for';
IN          : 'in';

SYNC        : 'sync';
MACRO       : 'macro';
INNER       : 'inner';
OUTER       : 'outer';
ATOMIC      : 'atomic';
SHARED      : 'shared';

NULL        : 'null';
TRUE        : 'true';
FALSE       : 'false';

INNER_ATOM_TYPE
    : 'Int'
    | 'Int32'
    | 'Int64'
    | 'Real'
    | 'Float'
    | 'Double'
    | 'Number'
    | 'String'
    | 'Bool'
    | 'Char'
    | 'Any'
    | 'Void'
    ;

BLANK
    : [ \r\n\t\u000C]+ -> skip
    ;

COMMAND
    : '#' ~[\r\n\f]* -> channel(2)
    ;

LIN_CMT
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

STRING
    : '"' ('\\"' | '\\\\' | .)*? '"'
    | '\'' ('\\\'' | '\\\\' | .)*? '\''
    ;

FSTRING
 : 'f' STRING
 ;

INTEGER
    : (DEC | OCT | HEX | BIN) EXPONENT?
    ;

REAL
    : FLOAT EXPONENT?
    ;

fragment DEC
    : [+-]? ([1-9] [0-9]* | '0')
    ;

fragment OCT
    : [+-]? '0' [0-7]+
    ;

fragment HEX
    : [+-]? '0x' [0-9a-fA-F]+
    ;

fragment BIN
    : [+-]? '0b' [01]+
    ;

fragment FLOAT
    : [+-]? [0-9]+ '.' [0-9]+
    ;

fragment EXPONENT
    : [eE] [+-]? [0-9]+
    ;
