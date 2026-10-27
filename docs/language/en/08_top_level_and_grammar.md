# 8. Program and grammar

English | [中文](../08_top_level_and_grammar.md)

This chapter describes the top-level structure of a program and gives a summary of the grammar. The complete grammar is in [8.5](#85-the-complete-grammar); it is generated from the source of the parser.

---

## 8.1 Program

```
program : (decl ';'?)* EOF
decl    : moduleDecl | importDecl | exportDecl | namespaceDecl | usingNamespaceDecl
        | dataDecl | funcDecl | typeDecl
```

The module top level contains only declarations and no executable statements; execution starts from `main` ([4.1](04_execution_model.md#41-program-and-entry)).

| Declaration | Grammar | Section |
|---|---|---|
| Module | a file is a module, named after the file | [5.1](05_modules.md#51-a-modules-name) |
| Import | `import (module \| (name \| { name, ... }) from module)` | [5.2](05_modules.md#52-imports) |
| Export | `export (dataDecl \| typeDecl \| { name, ... })`; the `export` of a function is written before `func` | [5.3](05_modules.md#53-exports) |
| Namespace | `namespace qualified-name { ... }` | [5.4](05_modules.md#54-namespaces) |
| Opening a namespace | `using namespace qualified-name` | [5.2](05_modules.md#52-imports) |
| Data | `(let \| var) name (: type)? = expression` | [7.2](07_statements.md#72-let-and-var) |
| Function | `annotation* (with <parameters>)? export? modifier* func name(parameters) (: type)? block` | [7.4](07_statements.md#74-functions) |
| Type | `type name (<parameter, ...>)? = type` | [7.7](07_statements.md#77-type-declarations) |

## 8.2 Statements and blocks

```
stmt      : dataDecl | funcDecl | typeDecl | dataExpr | usingNamespaceDecl | retStmt | blockStmt
retStmt   : (return | yield) dataList
stmtBlock : sync? '{' (stmt ';'?)* '}'
blockExpr : stmtBlock | dataExpr            // branch arm, lambda body
```

## 8.3 Expressions

From low precedence to high (see [6.12](06_expressions.md#612-operator-precedence)):

```
dataExpr   : ctrlExpr
ctrlExpr   : if logicalOr then blockExpr (else blockExpr)?
           | match name '{' (case pattern ('|' pattern)* '=>' blockExpr)+ '}'
assignExpr : logicalOr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&=' | '|=') logicalOr)?
logicalOr  : logicalAnd ('||' logicalAnd)*
logicalAnd : or ('&&' or)*
or         : and ('|' and)*
and        : equality ('&' equality)*
equality   : relational (('===' | '!==' | '==' | '!=') relational)*
relational : additive (('<' | '>' | '<=' | '>=') additive)*
additive   : multiplicative (('+' | '-') multiplicative)*
multiplicative : power (('*' | '/' | '@' | '%') power)*
power      : nullable ('^' power)?
nullable   : unary (('??' | '!!') dataExpr)?
unary      : neg (as type)?
neg        : ('-' | '!' | '~') negPower | link
negPower   : ('-' | '!' | '~') negPower | link ('^' negPower)?
link       : comp ('->' comp)*
comp       : anno ('..' anno)*
anno       : primary ( '.'field | '[' subscript ']' | '(' arguments ')' | '<' with arguments '>' )*
primary    : name | literal | '[' element, ... ']' | '{' key: value, ... '}' | '(' expression, ... ')' | lambda
lambda     : modifier* ('<' parameters '>')? '(' parameters ')' (':' type)? '=>' blockExpr
pattern    : literal | '_' | null | error name | name
```

Subscripts, arguments and `with` arguments must directly follow the expression before them. `(e)` is a parenthesised expression, `(e,)` a one-element tuple.

## 8.4 Types

```
typeDecl  : 'type' name ('<' parameters, ... '>')? '=' (type | enumBody)
typeExpr  : arrayType ('?' | '!')*
arrayType : specType ('[' ']')*
specType  : primaryType ('<' (type | const data), ... '>')?
primaryType : name | '(' type ')' | '(' type, ... ')' | '{' name: type, ... '}'
            | modifier* '(' parameters ')' '=>' type
enumBody  : 'enum' '{' variant, ... '}'
variant   : name | name '(' type, ... ')' | name '{' name: type, ... '}'
```

- `T?`, `T!`: [3.4](03_data_model.md#34-may-be-null-t-and-may-fail-t).
- `::<T>`: a function, `rule` or `fact` declares its type parameters after its name ([7.5](07_statements.md#75-generic-functions)).
- `constas n`: two uses, both compile-time names ([design principles 5.4](../../design-principles.en.md)): it names a dimension in a `const` argument -- the name is bound by `constas` where it first appears in a parameter type, and written as plain `n` afterwards -- or it is a **value parameter** of a function declaration (`func f(constas n: int, ...)`), bound by the constant at the call ([7.5](07_statements.md#75-generic-functions)). A value parameter may be written at any position in the parameter list, and there may be any number of them; each value is its own part of the instance key. A dimension name in a shape does not write `constas`: the name binds where it first appears in a parameter type and is written as plain `n` afterwards (see the array row of [3.1](03_data_model.md)).
- `enum`: [3.5](03_data_model.md#35-enums).
- Angle brackets: the arguments of a type function ([7.7](07_statements.md#77-type-declarations)) or the arguments of a module type.

## 8.5 The complete grammar

Every production of the language, one for one with the parser: each production is written above the function that implements it (`src/parse/syntax/parser*.cpp`), this section is generated from there by `node scripts/check-grammar.mjs --write`, and the test `grammar.reference_in_step` keeps the two in step. Notation: `'x'` is a literal token; `IDENTIFIER`, `INTEGER`, `REAL`, `STRING`, `MULTI_STR`, `FSTRING` are token classes (see [chapter 2](02_lexical.md)); `OPERATOR` is any operator token (used only in `operatorName`, whose `IDENTIFIER` is the name `operator`; the two `IDENTIFIER`s of `ruleDecl` are the names `rule` and `when`, and the `IDENTIFIER`s of `factDecl`/`factBody`/`factCase`/`factClause` are the names `fact`, `join`, `meet`, `widen` and `narrow` (the `match` of `factBody` and the `case` of `factCase` keep their meaning from [6.8.2](06_expressions.md#682-match)), which mean something only there); `SEP` is `;`; `*` `+` `?` `|` and parentheses have their usual meaning.

<!-- grammar:begin (generated by scripts/check-grammar.mjs --write) -->

```
program            : SEP? (decl SEP?)* EOF
decl               : importDecl | exportDecl | namespaceDecl | usingNamespaceDecl
                     | tagDecl | resourceDecl | dataDecl | funcDecl | typeDecl | ruleDecl | factDecl
stmt               : dataDecl | funcDecl | typeDecl | dataExpr | usingNamespaceDecl | retStmt
                     | blockStmt | staticForExpr
blockStmt          : stmtBlock | funcAnno+ stmtBlock
stmtList           : stmt (SEP? stmt)* SEP?
stmtBlock          : 'sync'? '{' stmtList? '}'
blockExpr          : stmtBlock | dataExpr
moduleName         : ('.' | '..' | '...')? IDENTIFIER ('.' IDENTIFIER)* ('::' IDENTIFIER)*
importDecl         : 'import' (moduleName ('as' identDef)? | (importItem | bracedImports) 'from'
bracedImports      : '{' (importItem (',' importItem)*)? ','? '}'
importItem         : identDef ('as' identDef)?
exportDecl         : 'export' (dataDecl | typeDecl | bracedIdents)
usingNamespaceDecl : 'using' 'namespace' identRef
namespaceDecl      : 'namespace' identRef '{' SEP? (namespaceItem SEP?)* '}'
namespaceItem      : namespaceDecl | importDecl | exportDecl | usingNamespaceDecl | funcDecl
                     | typeDecl | dataDecl | resourceDecl
funcAnno           : '@' identRef (angledValues | parentIdents)?
frontMatter        : (funcAnno | tagUse)*
funcDecl           : frontMatter ('with' angledParams)? 'export'?
                     'func' (identRef | operatorName) typeParams? parentParams (':' typeExpr)?
                     resourceClause? whenClause? stmtBlock?
tagDecl            : 'export'? 'tag' identDef typeParams? parentParams? 'on' tagPositions
                     ('read_by' tagReaders)? whenClause?
                     | 'export'? 'tag' identDef '=' bracketIdents
tagPositions       : tagPositionName (',' tagPositionName)* ','?
tagPositionName    : IDENTIFIER
tagReaders         : identRef (',' identRef)* ','?
tagUse             : '#' (tagRef | '[' (tagRef (',' tagRef)* ','?)? ']')
tagRef             : identRef parentArgues?
whenClause         : IDENTIFIER logicalOrExpr
typeParams         : '::' '<' identDef (',' identDef)* ','? '>'
operatorName       : IDENTIFIER (OPERATOR | '[' ']')
dataDecl           : ('let' | 'var') carrier (':' typeList)? '=' dataList
                     | 'let' '_' (':' typeList)? '=' dataList
carrier            : identList | parentIdents
typeDecl           : frontMatter? 'type' identRef
                     ('=' | '<' identDef (',' identDef)* ','? ('>' '=' | '>='))
                     (typeExpr | STRING)
resourceDecl       : 'export'? 'resource' identDef ('=' bracketIdents | 'unordered')?
retStmt            : ('return' | 'yield') dataList
modifiers          : 'sync'+
identDef           : IDENTIFIER
identRef           : (IDENTIFIER '::')* IDENTIFIER
identList          : identDef (',' identDef)*
parentIdents       : '(' identList? ','? ')'
bracedIdents       : '{' identList? ','? '}'
bracketIdents      : '[' identList? ','? ']'
keyParamPair       : ('var' | 'const')? identDef (':' typeExpr ('=' dataExpr)?)?
                     | '..' identDef (':' typeExpr)?
                     | 'constas' identDef (':' typeExpr)?
pairedParams       : keyParamPair (',' keyParamPair)*
parentParams       : '(' pairedParams? ','? ')'
angledParams       : '<' pairedParams? ','? '>'
dataExpr           : ctrlExpr | loopExpr
ctrlExpr           : 'if' logicalOrExpr 'then' blockExpr ('else' blockExpr)?
                     | 'match' identRef '{' matchCase+ '}'
                     | 'match' 'type' typeExpr '{' typeCase* '}'
assignExpr         : logicalOrExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&='
                     | '|=') logicalOrExpr)?
logicalOrExpr      : logicalAndExpr ('||' logicalAndExpr)*
logicalAndExpr     : orExpr ('&&' orExpr)*
orExpr             : andExpr ('|' andExpr)*
andExpr            : equalityExpr ('&' equalityExpr)*
equalityExpr       : relationalExpr (('===' | '!==' | '==' | '!=') relationalExpr)*
relationalExpr     : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
additiveExpr       : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
multiplicativeExpr : powerExpr (('*' | '/' | '@' | '%') powerExpr)*
powerExpr          : nullableExpr ('^' powerExpr)?
nullableExpr       : unaryExpr (('??' | '!!') dataExpr)?
unaryExpr          : negExpr ('as' typeExpr)?
linkExpr           : compExpr ('->' compExpr)*
negExpr            : ('-' | '!' | '~') negPower | linkExpr
negPower           : ('-' | '!' | '~') negPower | linkExpr ('^' negPower)?
compExpr           : annoExpr ('..' annoExpr)*
annoExpr           : accessExpr (indices | parentArgues | structData | angledValues | typeArguments
memberAccess       : '.' (IDENTIFIER | INTEGER | REAL | '[' dataExpr ']')
accessExpr         : primaryData ('.' (IDENTIFIER | INTEGER | REAL | '[' dataExpr ']'))*
shapeComp          : typeExpr '{' staticForExpr '}'
primaryData        : identRef | constasData | literal | arrayData | structData | tupleData
                     | funcData
constasData        : 'constas' identDef
literal            : ('+' | '-')? INTEGER | ('+' | '-')? REAL | STRING | MULTI_STR | FSTRING
                     | 'true' | 'false' | 'null'
indexValue         : '..'? dataExpr
keyValuePair       : identDef ':' dataExpr
pairedValues       : keyValuePair (',' keyValuePair)*
argumentValues     : (keyValuePair | indexValue) (',' (keyValuePair | indexValue))*
dataList           : dataExpr (',' dataExpr)*
argumentList       : indexValues (',' pairedValues)? | pairedValues
indexValues        : indexValue (',' indexValue)*
indices            : '[' (sliceIndices | dataExpr) (',' (sliceIndices | dataExpr))* ']'
sliceIndices       : dataExpr? ':' dataExpr? (':' dataExpr?)?
parentArgues       : '(' argumentList? ','? ')'
angledValues       : '<' argumentList? ','? '>'
typeArguments      : '::' '<' (typeList | namedTypeArgument (',' namedTypeArgument)*) ','? '>'
namedTypeArgument  : IDENTIFIER '=' typeExpr
arrayData          : '[' ((indexValues ','?) | dataExpr 'for' identRef 'in' dataExpr ('if' dataExpr)?)?
                     ']'
structData         : '{' (pairedValues ','?)? '}' | '{' staticForExpr '}'
tupleData          : '(' dataList? ','? ')' | '(' staticForExpr ')'
funcData           : angledParams? parentParams (':' typeExpr)? '=>' blockExpr
matchTypeExpr      : 'match' 'type' typeExpr '{' typeCase* '}'
typeCase           : 'case' typeCasePattern ('|' typeCasePattern)* '=>' blockExpr
staticForExpr      : 'static' staticForHeader '=>' blockExpr
staticForHeader    : 'for' (identDef | '_') (',' (identDef | '_'))* 'in'
                     ('fields' typeExpr | 'indices' dataExpr)
staticLoopExpr     : 'static' 'loop' loopState (',' loopState)* staticLoopHeader 'do' blockExpr
staticLoopHeader   : 'for' (identDef | '_') (',' (identDef | '_'))* 'in'
                     ('fields' typeExpr | 'indices' dataExpr | dataExpr)
typeCasePattern    : 'struct' | 'tuple' | 'func' | '#' 'leaf' | '_' | typeExpr
matchCase          : 'case' pattern ('|' pattern)* whenClause? '=>' blockExpr
pattern            : 'error' identDef
                     | identRef (enumTuplePattern | enumStructPattern)?
                     | literal
                     | '_'
fieldPattern       : identRef | '_'
enumTuplePattern   : '(' (fieldPattern (',' fieldPattern)*)? ','? ')'
enumStructPattern  : '{' (IDENTIFIER (':' fieldPattern)? ','?)* '}'
loopExpr           : 'loop' loopState (',' loopState)* loopFor? loopWhile? 'do' blockExpr
loopState          : identDef '=' logicalOrExpr
loopFor            : 'for' identDef 'in' logicalOrExpr
loopWhile          : 'while' logicalOrExpr
typeExpr           : arrayType ('?' | '!' | '..')*
arrayType          : specType arrayDim+
arrayDim           : '[' (']' | arrayDimItem (',' arrayDimItem)* ','?)? ']'
arrayDimItem       : '?' | '..' identDef? | dataExpr
specType           : primaryType ('<' typeOrData (',' typeOrData)* '>')?
typeOrData         : typeExpr | arrayDim | 'const' dataExpr
primaryType        : structType | enumType | identRef | '(' typeExpr ')' | tupleType | funcType
                     | 'typeof' dataExpr
tupleType          : '(' typeList? ','? ')'
structType         : '{' (keyTypePair (',' keyTypePair)*)? ','? '}'
keyTypePair        : tagUse* identDef ':' typeExpr
funcType           : angledParams? parentParams '=>' typeExpr resourceClause?
resourceClause     : ('uses' identRef (',' identRef)* | 'pure')+
typeList           : typeExpr (',' typeExpr)*
enumType           : 'enum' '{' (enumVariant (',' enumVariant)*)? ','? '}'
enumVariant        : identDef ('(' typeList? ','? ')' | '{' (enumField (',' enumField)*)? ','? '}')?
enumField          : identDef ':' typeExpr
ruleDecl           : frontMatter? ('with' angledParams)? 'export'? IDENTIFIER identDef typeParams?
                     parentParams whenClause? '{' ruleBody '}'
ruleBody           : dataExpr ('=>' | '<=>') dataExpr
factDecl           : frontMatter? ('with' angledParams)? 'export'? IDENTIFIER identDef typeParams?
                     parentParams? ':' typeExpr factBody factClause*
factBody           : 'match' logicalOrExpr '{' factCase* '}'
factCase           : 'case' factPattern '=>' dataExpr
factPattern        : 'const' identDef (':' typeExpr)? | '_' | dataExpr
factClause         : IDENTIFIER dataExpr
```

<!-- grammar:end -->

Two layout rules outside the grammar ([2.1](02_lexical.md#21-lines-whitespace-and-separators)) decide where a production applies:

- The `indices`, `parentArgues`, `angledValues` and postfix `!` of `annoExpr`, and the `<` of `specType`, touch the token before them; the `<` of `relationalExpr` follows white space. Inside an angle-bracket list a `>` that touches the token before it, or starts its line, closes the list.
- An expression does not go on across a line break unless the operator after the break can only be binary; inside round, square and angle brackets and the braces of a struct a line break means nothing.

Where there is more than one reading, the following rules decide, each on a bounded number of tokens:

| Place | Decision |
|---|---|
| `(`: lambda, or tuple / parenthesized expression | a lambda for `(name:`, `(var`, for `(name)` or `(name, ...)` followed by `=>` or `:`, or for an empty list followed by `=>` or `:` |
| `(` in a type | a function type for `(name:`, `(var`, an empty list followed by `=>`; `(T)` is the parenthesized type; anything else a tuple type |
| `{`: struct or block | a struct for `{ name :`; `{}` is the empty block as a branch arm or a lambda body and the empty struct as a statement |
| Arguments: positional or named | named from the first `name :` on |
| `+` `-` `^` after a type | part of the expression: a cast ends at the type, so `x as T + U` is `(x as T) + U` |
| Unary `-` | always an operator; looser than `^` and `->`; applied directly to a number it gives the negative literal |

Related files:

| File | Contents |
|---|---|
| `include/camel/parse/syntax/token_kinds.def` | Tokens and keywords |
| `include/camel/parse/syntax/node_kinds.def` | The node kinds of the syntax tree, one per production |
| `src/parse/syntax/` | Lexer and parser |
| `docs/Camel.asdl` | The structure of the abstract syntax tree |
| `docs/diagnostics.md` | The catalog of all diagnostics (generated by a script) |
