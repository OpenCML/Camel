lexer grammar OpenCMLLex;

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
KEYOF       : 'keyof';
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
OF          : 'of';
TRY         : 'try';
CATCH       : 'catch';
FINALLY     : 'finally';
THROW       : 'throw';
RAISE       : 'raise';

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
    : 'int'
    | 'i32'
    | 'i64'
    | 'float'
    | 'f32'
    | 'f64'
    | 'string'
    | 'bool'
    | 'char'
    | 'any'
    | 'void'
    | 'functor'
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
    : '`' ('\\`' | '\\\\' | .)*? '`'
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
