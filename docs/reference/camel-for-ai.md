# Camel for AI

One file that says everything an AI needs to write Camel that compiles: the rules, the idioms, the
errors a first draft makes, and the exact name of every diagnostic it will meet. It is written to be
read whole — the whole file is under 100,000 characters, about 28,000 tokens, and the check below
keeps it there.

The language described here is the development version (`develop`), and it is tied to that tree:
`scripts/camel-for-ai.mjs --check` regenerates the four generated regions from their sources, checks
every diagnostic this file names against the registry, and fails when the file and the tree
disagree. Read it as the language you are compiling against, not as a snapshot of one.

## How to read this file

- **Answers first.** Sections 2–3 are what you need before writing a line. Sections 4–10 are the
  rules of each part of the language. Section 11 is how Camel is written when someone who knows it
  writes it. Section 12 is the most valuable one when a compile fails: it is organised as *what you
  wrote — what the compiler says — what to write instead*. Section 13 lists spellings the compiler
  accepts and then rejects, so a plausible-looking guess can be recognised before it is tried.
- **`target`.** A rule that is decided but not implemented yet is marked `**target**` and names the
  design it comes from under `docs/design/` (for example `docs/design/io-concurrency-streams/`).
  Code written against a `target` rule does not compile today. Never treat a `target` rule as
  available.
- **Diagnostics are named in full**: `SemanticDiag::UnresolvedReference`, never bare
  `UnresolvedReference`. Appendix A lists all of them with the exact message template; the compiler
  prints the section-qualified name, so the two can be matched mechanically.
- **Fenced blocks tagged `camel` compile.** Every one of them is compiled by the documentation check
  when the test suite runs, so any block on this page is valid Camel as of this tree. A block that
  is deliberately incomplete is marked `camel-snippet: fragment`, and one that is deliberately wrong
  is marked `camel-snippet: counter-example` and says what the compiler answers.
- **Where a rule comes from.** Hand-written sections name their source in the text — a section of
  `docs/design-principles.cn.md` (normative: when it disagrees with anything, it wins) or a page of
  `docs/language/`. Appendix C collects the map.

## What each part costs

Budgets, in characters: the file as a whole (100,000), the hand-written half (39,000) and each
generated region (61,000 together). `node scripts/camel-for-ai.mjs --report` prints the current
numbers, and `--check` fails when one of them is exceeded.

| Part | Answers | Budget |
|---|---|---|
| 2. The shape of a Camel program | what a program is: values, the graph, effects, passes | 2,000 |
| 3. The smallest program that compiles | entry point, module, output, exit | 1,500 |
| 4. Lexical: names, comments, literals | what the lexer accepts, keywords, operators | 3,500 |
| 5. Expressions, operators, precedence | how an expression is read | 3,500 |
| 6. Statements and declarations | functions, parameters, overloads, generics, `when` | 3,500 |
| 7. Effects and `sync` | what runs in order, and what the compiler folds | 2,000 |
| 8. Streams (`target`) | `T..`, `yield`, `tee` | 800 |
| 9. Modules and the standard library | imports, exports, namespaces, where the library is | 2,000 |
| 10. Passes, the command line, the graph | how to run a program and look at its graph | 1,500 |
| 11. Idioms | how Camel code is written when it is written by someone who knows | 3,000 |
| 12. Common errors | the error, the diagnostic, the fix | 9,000 |
| 13. Accepted and then rejected | reserved spellings and their diagnostics | 1,500 |
| 14. Not implemented yet (`target`) | what to avoid until its design lands | 1,200 |
| Appendix A. Diagnostics | name, severity, message — generated | 27,000 |
| Appendix B. Standard library signatures | every exported name — generated | 27,000 |
| Appendix C. Sources | which normative page each rule comes from | 1,000 |
| — | generated, all four regions | 61,000 |
| — | hand-written, this header and sections 2–14 with Appendix C | 39,000 |

## 2. The shape of a Camel program

*To be written.*

## 3. The smallest program that compiles

*To be written.*

## 4. Lexical: names, comments, literals, keywords

*To be written.*

### Keywords and operators (generated)

<!-- camel-for-ai region: lexical (generated from include/camel/parse/syntax/token_kinds.def; node scripts/camel-for-ai.mjs --write) -->
| Group | Words |
|---|---|
| Type | `as` `const` `constas` |
| Reserved | `is` `enum` `keyof` `typeof` `of` `try` `catch` `finally` |
| Declaration | `let` `var` `from` `func` `type` `with` `module` `import` `export` `using` `namespace` |
| Control | `wait` `return` `if` `then` `else` `match` `case` `for` `in` `throw` `loop` `while` `do` `sync` `static` |
| Literal | `null` `true` `false` |

**Operators and separators** (55): `;` `,` `:` `::` `.` `..` `...` `(` `)` `[` `]` `{` `}` `<` `<` `>` `<=` `<=>` `>=` `==` `!=` `===` `!==` `=` `+=` `-=` `*=` `/=` `%=` `^=` `@=` `&=` `|=` `+` `-` `*` `/` `%` `^` `@` `#` `&` `|` `&&` `||` `!` `!!` `~` `?` `??` `->` `?->` `=>` `\\` `_`
<!-- camel-for-ai end: lexical -->

## 5. Expressions, operators, precedence

*To be written.*

### Precedence (generated)

<!-- camel-for-ai region: precedence (generated from src/parse/syntax/parser_expr.cpp; node scripts/camel-for-ai.mjs --write) -->
```text
 1  dataExpr            -                                  -
 2  waitExpr            wait                               prefix, optional
 3  ctrlExpr            if then else                       construct
 4  assignExpr          = += -= *= /= %= ^= @= &= |=       one
 5  logicalOrExpr       ||                                 repeat, left
 6  logicalAndExpr      &&                                 repeat, left
 7  orExpr              |                                  repeat, left
 8  andExpr             &                                  repeat, left
 9  equalityExpr        === !== == !=                      repeat, left
10  relationalExpr      < > <= >=                          repeat, left
11  additiveExpr        + -                                repeat, left
12  multiplicativeExpr  * / @ %                            repeat, left
13  powerExpr           ^                                  right, one
14  nullableExpr        ?? !!                              right (a whole expression on the right)
15  unaryExpr           as is                              one
16  linkExpr            -> ?->                             repeat, left
17  negExpr             - ! ~                              prefix
18  negPower            - ! ~ ^                            prefix, right
19  compExpr            ..                                 repeat, left
20  annoExpr            !                                  repeat, left
```

The production of each level, as `src/parse/syntax/parser_expr.cpp` writes it above the
function that parses it:

```text
dataExpr           : waitExpr | ctrlExpr | loopExpr
waitExpr           : 'wait'? assignExpr
ctrlExpr           : 'if' logicalOrExpr 'then' blockExpr ('else' blockExpr)?
assignExpr         : logicalOrExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&=' | '|=') logicalOrExpr)?
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
unaryExpr          : linkExpr (('as' | 'is') typeExpr)?
linkExpr           : negExpr (('->' | '?->') compExpr)*
negExpr            : ('-' | '!' | '~') negPower | compExpr
negPower           : ('-' | '!' | '~') negPower | compExpr ('^' negPower)?
compExpr           : annoExpr ('..' annoExpr)*
annoExpr           : accessExpr (indices | parentArgues | structData | angledValues | typeArguments | '!' | memberAccess)*
```
<!-- camel-for-ai end: precedence -->

## 6. Statements and declarations

*To be written.*

## 7. Effects and `sync`

*To be written.*

## 8. Streams (`target`)

*To be written.*

## 9. Modules and the standard library

*To be written.*

## 10. Passes, the command line, the graph

*To be written.*

## 11. Idioms

*To be written.*

## 12. Common errors

*To be written.*

## 13. Accepted and then rejected

*To be written.*

## 14. Not implemented yet (`target`)

*To be written.*

## Appendix A. Diagnostics (generated)

Every diagnostic the compiler, the passes and the run time can report. `Sev` is `E` for an error
(which fails the compile or stops the run) and `W` for a warning. The message is the template the
report fills in, with `{0}`, `{1}` for the values it names; the compiler prints a help line under it
when the message has one, and `camel --explain <name or code>` prints the entry.

Source: `diagnostics/registry.tsv`, the one place a diagnostic is declared. Regenerate with
`node scripts/camel-for-ai.mjs --write`.

<!-- camel-for-ai region: diagnostics (generated from diagnostics/registry.tsv; node scripts/camel-for-ai.mjs --write) -->
| Name | Sev | What the compiler says |
|---|---|---|

**Internal** (`InternalDiag`) — 1 (1 error, 0 warnings)

| `UnknownInternalError` | E | An error has occurred: {0}. |

**Lexical** (`LexicalDiag`) — 5 (4 errors, 1 warning)

| `IllegalCharacter` | E | Illegal character '{0}'. |
| `InvalidNumberFormat` | E | Invalid number format: '{0}'. |
| `UnclosedStringLiteral` | E | This string literal is not closed. |
| `UnterminatedComment` | E | Unterminated comment: {0}. |
| `InvalidEscapeSequence` | W | Invalid escape sequence in string: {0}. |

**Syntax** (`SyntaxDiag`) — 17 (16 errors, 1 warning)

| `MissingClosingDelimiter` | E | Expected '{0}' before {2}, to close the '{1}'. |
| `UnmatchedClosingDelimiter` | E | Unexpected '{0}': no '{1}' is open here. |
| `MissingThen` | E | Expected 'then' after the condition of the 'if', before {0}. |
| `ExtraSemicolon` | E | Unexpected ';': there is no statement between it and the ';' before it. |
| `AssignmentInCondition` | E | '=' assigns; it cannot be used in the condition of an 'if'. |
| `KeywordAsIdentifier` | E | '{0}' is a keyword and cannot be used as a name. |
| `MissingComma` | E | Expected {1} before {0}. |
| `MissingInitializer` | E | The declaration of '{0}' has no value. |
| `MissingToken` | E | Expected {0} before {1}. |
| `UnexpectedToken` | E | Unexpected {0}{1}. |
| `UnexpectedEndOfFile` | E | The file ends in the middle of a construct{0}. |
| `ComparisonNeedsSpace` | E | This '<' touches {0}, so it opens an angle-bracket argument list, which {1} does not continue. |
| `AngleCloseAfterSpace` | E | This '>' follows a space and is read as the comparison operator, so the angle-bracket list it was meant to close is still open at {0}. |
| `LineStartsWithOperator` | E | A statement cannot start with '{0}': the line break before it ended the statement of the line above. |
| `EmptyInterpolation` | E | An interpolation has no expression to format. |
| `WhenAfterBody` | E | A declaration's condition comes before the body it guards, so {0} cannot follow the body. |
| `ComparisonAcrossLines` | W | This '>' is read as a comparison with the next line, which starts with {0}: two statements may have been joined into one. |

**Semantic** (`SemanticDiag`) — 199 (185 errors, 14 warnings)

| `Redeclaration` | E | Redeclaration of reference: '{0}'. |
| `ModuleNotFound` | E | Module '{0}' not found. {1} |
| `ReservedIdentifier` | E | Identifiers starting and ending with double underscores are reserved for internal use. |
| `TupleUnpackingCountMismatch` | E | Tuple unpacking requires the same number of references and data nodes, or exactly one tuple data. |
| `InvalidLiteral` | E | Invalid {0} literal: '{1}'. |
| `DuplicateDictKey` | E | Duplicate key in dict: '{0}'. |
| `DuplicateParameter` | E | Duplicate parameter detected: '{0}'. |
| `UnresolvedTypeReference` | E | Unresolved type reference: '{0}'. |
| `UnresolvedReference` | E | Unresolved reference: '{0}'. |
| `BranchReturnTypeMismatch` | E | Return type mismatch between branches (in function '{0}'): expected '{1}', found '{2}'. |
| `InvalidAccessIndex` | E | Invalid access index: '{0}'. |
| `TypeNotIndexable` | E | Type '{0}' does not support index access. |
| `NoMatchingFunction` | E | No matching function found for argument types: {0}. |
| `ArgumentsMismatch` | E | Function of type '{0}' cannot be called with argument types: {1}. |
| `ReturnTypeMismatch` | E | Return type '{0}' does not match the function's declared return type '{1}'. |
| `ElementTypeMismatch` | E | Cannot insert data of type '{1}' into a(n) {0} with element type '{2}'. |
| `CallingIncompleteFunction` | E | Cannot call function '{0}' with type '{1}' as its type is not fully constructed yet. |
| `ImportNameNotExported` | E | Reference '{0}' is declared as imported but is not exported by its module. |
| `LiteralNotRepresentable` | E | The literal '{0}' cannot be represented as type '{1}': {2}. |
| `DynamicCastForbidden` | E | Dynamic cast from '{0}' to '{1}' is not allowed. |
| `ModuleParseFailed` | E | Failed to parse module file: {0}. |
| `InvalidOperatorURI` | E | Invalid operator URI format: '{0}'. Expected 'protocol:path'. |
| `EntryModuleMissingMain` | E | Entry module '{0}' must define a resolvable 'main' on the module root (__root__). |
| `ArgumentsRejected` | E | Invalid call to '{0}' with argument types {1}: {2}. |
| `ImmutablePassedToVarParam` | E | '{0}' is immutable and cannot be passed to a var parameter of '{1}'. |
| `AssignmentTargetNotVariable` | E | The target of an assignment must be a variable, or a path from one. |
| `ConstructNotSupported` | E | '{0}' is not supported: {1}. |
| `InvalidTypeArgument` | E | The arguments of the type '{0}<...>' are not accepted: {1}. |
| `ArgumentNeedsConversion` | E | The call to '{0}' needs an explicit conversion: {1}. |
| `ReturnNeedsConversion` | E | The returned value needs an explicit conversion to the function's declared return type '{0}': {1}. |
| `ImportNameConflict` | E | The name '{0}' is imported from the modules '{1}' and '{2}' with {3}. |
| `FeatureNotSupported` | E | The feature '{0}' is not supported yet. |
| `TypeVariableRedeclared` | E | The type parameter '{0}' is declared twice at the head of this declaration. |
| `TypeVariableNotBindable` | E | The type variable '{0}' of '{1}' can never be bound: {2}. |
| `GenericArgumentsMismatch` | E | The generic function '{0}' cannot be called with argument types {1}: {2}. |
| `GenericFunctionAsValue` | E | The generic function '{0}' cannot be given its types here: {1}. |
| `GenericInstantiationTooDeep` | E | Instantiating the generic function '{0}' with {1} is refused: {2}. |
| `TypeFunctionArguments` | E | The type function '{0}' cannot be used this way: {1}. |
| `GenericFunctionUnsupported` | E | The generic function '{0}' cannot be {1}. |
| `FunctionNameAsValue` | E | '{0}' is used as a value here, but it names {1}, not one function. |
| `ThrowOutsideFallible` | E | 'throw' needs a function declared to return 'T!': {0}. |
| `ThrowNotAnError` | E | 'throw' takes an error value, not a value of type '{0}'. |
| `TaggedTypeNesting` | E | The type '{0}' marks '{1}' twice. |
| `TaggedValueNotOpened` | E | A value of type '{0}' cannot be used here: {1}. |
| `TryCatchNotInLanguage` | E | 'try'/'catch' is not part of Camel. |
| `TaggedOperandMismatch` | E | '{0}' cannot be applied here: {1}. |
| `TaggedMatchCases` | E | This 'match' on a value of type '{0}' is not well formed: {1}. |
| `ImportedModuleHasErrors` | E | Cannot import module '{0}': it has errors ({1}). |
| `CollectionKeyNotAllowed` | E | The type '{0}' cannot be a key of '{1}': {2}. |
| `DuplicateCollectionKey` | E | The key '{0}' is written twice in this literal converted to '{1}'. |
| `MatchWithoutDefault` | E | This 'match' on a value of type '{0}' has no 'case _'. |
| `IfWithoutElseAsValue` | E | This 'if' has no 'else' and is used as a value. |
| `EmptyArrayWithoutType` | E | '{0}' is bound to an empty array '[]' that has no element type. |
| `PartialApplicationRefused` | E | The context arguments <{0}> cannot be bound to '{1}' as a value: {2}. |
| `ComprehensionSourceRefused` | E | The source of this list comprehension has type '{0}', which has no elements to take. |
| `LogicalOperandNotBool` | E | The {1} side of '{0}' has type '{2}', not 'bool'. |
| `ExportedVariable` | E | The variable '{0}' cannot be exported. |
| `ModuleValueCycle` | E | The value '{0}' depends on itself: {1}. |
| `CaptureAfterRecursiveCall` | E | The function '{1}' uses '{0}' only after it is called in its own recursion, where the call could not pass it. |
| `AmbiguousName` | E | The name '{0}' is ambiguous here: {1}. |
| `ConditionNotBool` | E | The condition of this 'if' has type '{0}'; a condition is a 'bool'. |
| `MatchCaseTypeMismatch` | E | This case of a 'match' on a value of type '{0}' is a value of type '{1}'. |
| `UnusedPureResult` | E | The result of '{0}' is not used, and '{0}' changes nothing: it returns a new value{1}. |
| `OperatorNotOverloadable` | E | '{0}' cannot be overloaded: {1}. |
| `OperatorOverloadInvalid` | E | This overload of operator '{0}' is not valid: {1}. |
| `OperatorOverloadConflict` | E | Operator '{0}' is already defined for ({1}){2}. |
| `AmbiguousCall` | E | The call to '{0}' with ({1}) fits more than one overload. |
| `DeclaredTypeMismatch` | E | This value has type '{0}', which is not the declared type '{1}'. |
| `ModuleTypeCycle` | E | The type '{0}' is part of a cycle of types: {1}. |
| `InvalidAnnotation` | E | The annotation '@{0}' is not accepted: {1}. |
| `LoopSequenceNotIterable` | E | 'for' takes the elements of an array, a string, a map or a tensor; '{0}' has type '{1}'. |
| `LoopBodyTypeMismatch` | E | The body of this loop gives a value of type '{0}'; the state of the loop has type '{1}'. |
| `LoopStateWithoutType` | E | The first value of the loop state '{0}' does not give it a type ('{1}'). |
| `LoopConditionNotBool` | E | The condition after 'while' has type '{0}'; a condition is a 'bool'. |
| `LoopNameRepeated` | E | The name '{0}' is declared twice in this loop head. |
| `DuplicateEnumVariant` | E | The variant '{0}' is declared twice in enum '{1}'. |
| `DuplicateEnumField` | E | The field '{0}' is declared twice in variant '{1}' of enum '{2}'. |
| `EnumVariantFieldCount` | E | The variant '{0}' of enum '{1}' takes {2} field(s), but {3} were given. |
| `EnumPatternMismatch` | E | The pattern '{0}' does not match a value of type '{1}': {2}. |
| `EnumMatchNotExhaustive` | E | The match over '{0}' has no case for {1}, and no 'case _'. |
| `EnumFieldAccess` | E | '{0}' is a value of enum '{1}': its fields are not read by name or index. |
| `EnumTypeArgumentNotInferred` | E | Cannot infer the type argument '{0}' of enum '{1}' from the fields of '{2}'. |
| `EnumFieldTypeMismatch` | E | Field {0} of variant '{1}' has type '{2}', but the value given has type '{3}'. |
| `EnumConstructionMismatch` | E | Variant '{0}' of enum '{1}' cannot be built from this value: {2}. |
| `ConstasNotAllowed` | E | The dimension name '{0}' is bound here, but only a parameter type can bind one. |
| `DimensionNameNotBound` | E | The dimension name '{0}' is not bound by any parameter of this signature. |
| `DimensionNameAlreadyBound` | E | The dimension name '{0}' is already bound; write '{0}' here. |
| `DimensionMismatch` | E | Argument {0} has extent {1}, but the dimensions the earlier arguments bound do not allow it. |
| `InvalidExtentExpression` | E | This is not a shape: the extents of a type written with dimension names are integers, bound names, or linear expressions over them ('f32[n, 3]'). |
| `NotDifferentiable` | E | The function cannot be differentiated: {0}. |
| `TagNotAtPosition` | E | The tag '{0}' may not be written at a {1}: it is declared for {2}. |
| `TagUndeclared` | E | No tag '{0}' is declared. |
| `TagArgumentCount` | E | The tag '{0}' takes {1} parameter(s), but {2} argument(s) are written. |
| `TagArgumentNotConstant` | E | The argument '{0}' of the tag '{1}' is not a compile-time value. |
| `TagOnUnnamedField` | E | A tag on the field '{0}' of an unnamed struct type belongs to no declaration. |
| `TagArgumentNamed` | E | A tag argument is positional: '{0}' is named. |
| `TagArgumentType` | E | The parameter '{0}' of the tag '{1}' is declared '{2}', but the argument is '{3}' ({4}). |
| `TagNotAPermission` | E | The tag '{0}' grants no numeric permission. |
| `RuleTagNotRequires` | E | A rule may be preceded only by '#requires<...>', not by the tag '{0}'. |
| `TagParameterType` | E | The parameter '{0}' of the tag '{1}' is declared '{2}': a tag parameter is an int, a float, a bool, a string or a func. |
| `TagShadowsPermission` | E | The tag '{0}' is declared with the name of a tag of the language. |
| `StaticMatchNotExhaustive` | E | No case of this 'match type' selects the type '{0}'. |
| `StaticMatchUnreachableCase` | E | The case '{0}' selects every type the cases after it could select. |
| `StaticTypeNotConcrete` | E | The type matched here is still '{0}' when the function is instantiated. |
| `StaticMatchTooDeep` | E | The 'match type' bodies are nested {0} deep. |
| `GenericInstantiationCycle` | E | The generic function '{0}' is instantiated with {1} while that same instance is being built, and its return type is not known yet. |
| `StaticForNotComposite` | E | The type '{0}' has no fields for a 'static for' to walk. |
| `StaticAccessNotInternal` | E | A field cannot be taken from '{0}' by a compile-time key. |
| `StaticAccessUnknownField` | E | The struct '{1}' has no field '{0}'. |
| `StaticAccessIndexOutOfRange` | E | The key '{0}' is not an element of the tuple '{1}' ({2} elements). |
| `StaticAccessNotConstant` | E | The key '{0}' of a 't.[...]' access is not known when the function is compiled. |
| `StaticComprehensionVoidElement` | E | The block for the field '{0}' of '{1}' gives no value, so the comprehension has no element for it. |
| `StaticIndicesNotTuple` | E | 'indices' walks the positions of a tuple, but '{0}' has type '{1}'. |
| `RestParameterNotLast` | E | The rest parameter '..{0}' must be the last parameter. |
| `RestParameterTypeNotSupported` | E | The rest parameter '..{0}' cannot write a type. |
| `RestArgumentNotLast` | E | The rest argument '..{0}' must be the last positional argument. |
| `RestArgumentNotTuple` | E | The rest argument '..{0}' is not a compile-time tuple. |
| `RestFunctionNeedsExpansion` | E | The rest function '{0}' cannot be used here: {1}. |
| `RestArgumentsMismatch` | E | The call to '{0}' gives {1} argument(s), but its fixed parameters need {2}. |
| `StaticTupleEscapes` | E | The build-time tuple '{0}' is not a value. |
| `ConstasTypeRequired` | E | The 'constas' parameter '{0}' writes no type. |
| `ConstasValueNotConstant` | E | The value given for the 'constas' parameter '{0}' is not a compile-time constant of its parameter's type ('{1}'). |
| `ConstasFunctionNeedsCall` | E | The declaration '{0}' takes a 'constas' parameter, so it has no function value. |
| `ConstasInNativeDeclaration` | E | The body-less declaration '{0}' cannot take a 'constas' parameter. |
| `ConstasArgumentMissing` | E | The call gives no value for the 'constas' parameter '{0}'. |
| `TagValueConflict` | E | The tag '{0}' gives the fact '{1}' the value {2}, but the node already says {3}. |
| `DimensionAfterAnyRank` | E | A dimension pack ('..') must be the last item of its '[...]', and only one may be written. |
| `UnknownResource` | E | The name '{0}' in the resource clause of '{1}' is neither a parameter nor a resource. |
| `PureAndUses` | E | The signature of '{0}' says both 'pure' and 'uses'. |
| `ResourceGroupCycle` | E | The resource group '{0}' contains itself: {1}. |
| `HandleTypeNotNominal` | E | '{0}' is not a nominal type declaration, so '#resource' says nothing about it. |
| `NativeResourceMismatch` | E | The declaration '{0}' says '{1}', but the operator '{2}' declares '{3}'. |
| `CmlUnsupportedConstruct` | E | std::cml cannot print {0}: {1}. |
| `CmlBigConstant` | E | std::cml refused the constant '{0}' of type {1} in {2}: {3} bytes, over max_literal_bytes ({4}). |
| `WhenConditionNotBool` | E | The 'when' condition of '{0}' is not a bool (it is '{1}'). |
| `WhenConditionNotConstant` | E | The 'when' condition of '{0}' cannot be evaluated when the declaration is instantiated: {1}. |
| `WhenUnknownName` | E | '{0}' is not visible to the 'when' condition of '{1}'. |
| `WhenTypeQueryNotConcrete` | E | The type query '{0}' cannot be answered for '{1}' in the 'when' condition of '{2}': the type still has a type variable. |
| `WhenConditionUnmet` | E | No candidate of '{0}' applies to ({1}): every candidate's 'when' condition is false. Candidates: {2} |
| `WithArgumentUnknown` | E | '{0}' is not a 'with' parameter of '{1}'. |
| `WithArgumentDuplicate` | E | The 'with' parameter '{0}' of '{1}' is given twice. |
| `WithArgumentMissing` | E | The call gives no value for the 'with' parameter '{0}' of '{1}', and it has no default. |
| `WithDefaultNotConstant` | E | The default of the 'with' parameter '{0}' is not a compile-time constant: {1}. |
| `WithDefaultWrongType` | E | The default of the 'with' parameter '{0}' has type '{1}', not '{2}'. |
| `WithParameterAfterDefault` | E | The 'with' parameter '{0}' has no default and follows '{1}', which has one. |
| `WithArgumentCallee` | E | The 'with' argument '{0}' is given by name, and the signature of '{1}' is not known where the binding is written. |
| `WithArgumentInValue` | E | The 'with' argument '{0}' is given by name, and this binding is used as a function value. |
| `WhenPredicateNotImported` | E | '{1}' is declared in module '{2}', but that name is not imported where the 'when' condition of '{0}' is written. |
| `WhenPredicateNotCompileTime` | E | '{1}' is not a compile-time predicate that the 'when' condition of '{0}' can call: {2}. |
| `WhenPredicateAmbiguous` | E | The 'when' condition of '{0}' calls '{1}', and that name is ambiguous: it is imported from {2}. |
| `NativeDeclarationIncomplete` | E | The body-less declaration '{0}' is incomplete: it has no result type. |
| `NativeDeclarationNotLoaded` | E | The declaration '{0}' names the operator '{1}', which no loaded module implements. |
| `NativeDeclarationOverlap` | E | Two declarations of '{0}' can take the same call: '{1}' and '{2}'. |
| `WhenDeclarationOverlap` | E | Two declarations of '{0}' both apply to the type '{1}': '{2}' and '{3}'. |
| `NativeDeclarationForeignOperator` | E | The declaration '{0}' names the operator '{1}', which another module declares. |
| `LambdaParameterWithoutType` | E | The type of the parameter '{0}' of this lambda is not written and cannot be inferred: {1}. |
| `TypeOfNotAValue` | E | typeof {0} has no value type: {1}. |
| `TypeOfNotWholeType` | E | typeof is written inside the larger type {0}. |
| `SameShapeComprehensionNotComposite` | E | {0} is not a struct or a tuple, so it has no shape to fill. |
| `SameShapeComprehensionMismatch` | E | The shape {0} is filled from the fields of {1}. |
| `StaticMatchPatternCannotMatch` | E | The identity pattern '{0}' cannot select a type here: {1}. |
| `FoldCallArgumentNotConstant` | E | The call of '{0}' cannot be evaluated at build time: {1}. |
| `FoldCallHasEffects` | E | The call of '{0}' cannot be evaluated at build time: {1}. |
| `FoldCallTooDeep` | E | The call of '{0}' cannot be evaluated at build time: {1}. |
| `FoldCallResultNotConstant` | E | The call of '{0}' cannot be used as a compile-time constant: {1}. |
| `FoldCallNotEvaluable` | E | The call of '{0}' cannot be evaluated at build time: {1}. |
| `ParamArgumentUnknown` | E | '{0}' is not a parameter of '{1}'. |
| `ParamArgumentDuplicate` | E | The parameter '{0}' of '{1}' is given twice. |
| `ParamArgumentMissing` | E | The call gives no value for the parameter '{0}' of '{1}', and it has no default. |
| `ParamArgumentCallee` | E | The argument '{0}' is given by name, and the signature of '{1}' is not known where the call is written. |
| `OverloadParameterNames` | E | The argument '{0}' is given by name, and the overloads of '{1}' do not name the parameter at that position the same way. |
| `ParamArgumentPositionalAfterNamed` | E | A positional argument follows a named one in the call of '{0}'. |
| `ParamDefaultNotConstant` | E | The default of the parameter '{0}' is not a compile-time constant: {1}. |
| `ParamDefaultWrongType` | E | The default of the parameter '{0}' has type '{1}', not '{2}'. |
| `ParamAfterDefault` | E | The parameter '{0}' has no default and follows '{1}', which has one. |
| `ParamArgumentWrongType` | E | The argument '{0}' of '{1}' has type '{2}', not '{3}'. |
| `CompOperandMismatch` | E | '..' takes two functions or two integers; the left operand is '{0}' and the right one '{1}'. |
| `CompOperandNotUnary` | E | '..' composes a unary function on the left; '{0}' takes {1} parameters. |
| `VarParamInAsyncFunction` | W | Variable parameters are only allowed in sync functions. |
| `IgnoredSideEffect` | W | {0} is neither waited nor in a sync block, and {1}. |
| `CapturedWriteInAsyncFunction` | W | {0} writes the captured variable{1} {2} but is not declared with the 'sync' modifier. |
| `EffectNotExecuted` | W | {0} has side effects but is never executed: nothing depends on it and it is not in a sync block. |
| `ExitInUnusedBranch` | W | This '{0}' gives the value of the '{1}' it is in, and nothing uses that value: it does not leave the function. |
| `UnusedFallibleValue` | W | This value has type '{0}' and nothing uses it: an error it holds is lost{1}. |
| `NullTestNeverNull` | W | {0} has type '{1}', which is never null: this test is always {2}. |
| `KnownNotNull` | W | '{0}' is known not to be null here (it has type '{1}'): the null case of '{2}' is never taken. |
| `EnumVariantCaseRepeated` | W | The variant '{0}' is matched more than once; the later case is never taken. |
| `MatchCaseAfterWildcard` | W | This case follows 'case _': it is never taken. |
| `MatchWildcardRedundant` | W | Every variant of '{0}' is matched before 'case _': the wildcard is never taken. |
| `TagHasNoReader` | W | The tag '{0}' is written, but none of the passes it names ({1}) is in the pipeline. |
| `DimensionNameUsedOnce` | W | The dimension name '{0}' is written once in this signature, so it relates nothing. |
| `TypeVariableUnused` | W | The type parameter '{0}' is declared at the head but never written in the signature. |

**Run time and passes** (`RuntimeDiag`) — 38 (35 errors, 3 warnings)

| `RuntimeError` | E | An error has occurred: {0}. |
| `DivisionByZero` | E | Attempted division by zero. |
| `IndexOutOfRange` | E | Index {0} is out of range for {1} of length {2}. |
| `MaxRecursionDepthExceeded` | E | Maximum recursion depth ({1}) exceeded when evaluating graph '{0}'. |
| `TensorDimensionMismatch` | E | Tensor shape mismatch in '{0}': {1}. |
| `UnrecognizedGraphPass` | E | Unrecognized graph rewriting/scheduling/translation pass '{0}'. |
| `TranslationRefused` | E | Translation pass '{0}' refused the graph: {1}. |
| `PassInternalError` | E | Pass '{0}' failed with an internal error: {1}. |
| `MacroEvaluationFailed` | E | '{0}' could not be evaluated by std::macro: {1}. |
| `EntryArgumentMissing` | E | No value was given for the {0} {1} of 'main'. |
| `EntryArgumentInvalid` | E | Cannot bind the argument '{0}' of 'main': {1}. |
| `EntryResultNotWritten` | E | Cannot write the result of 'main' to '{0}': {1}. |
| `MemoryExhausted` | E | Out of memory{0}: {1}. |
| `InvalidNumberText` | E | Cannot convert the string '{0}' to {1}. |
| `FormatFailed` | E | Cannot format with the format string '{0}': {1}. |
| `OperandTypeUnsupported` | E | '{0}' does not support a value of type '{1}'; it takes {2}. |
| `FieldNotFound` | E | The struct has no field '{0}'. |
| `InvalidArgument` | E | Invalid argument of '{0}': {1}. |
| `FileOpenFailed` | E | '{0}' cannot open the file '{1}': {2}. |
| `FileSystemError` | E | '{0}' failed for '{1}': {2}. |
| `RegexInvalid` | E | '{0}': '{1}' is not a valid regular expression: {2}. |
| `TimeFormatFailed` | E | '{0}' failed: {1}. |
| `GradientElementTypeMismatch` | E | The gradient of a value of type '{0}' has element type '{1}': {2}. A gradient must have the element type of the value it is the gradient of (design principles 9.9); accumulated as it is, the value would be read at the wrong width and answer with a number that is not the derivative. |
| `ValueWithNoDerivativeRule` | E | The operator '{0}' takes a differentiable value through its with parameter {1} (counting from 1), of type '{2}', but {3}. |
| `ConversionOutOfRange` | E | Cannot convert {0} to {1}: {2}. |
| `UnhandledError` | E | An error value reached the exit of 'main' without being handled: {0}: {1}. |
| `SliceOutOfRange` | E | The range {0} to {1} is out of range for {2} of length {3}. |
| `InvalidPassParameter` | E | Invalid pass specification '{0}': {1}. |
| `GraphPassFailed` | E | Pass '{0}' failed: {1}. |
| `PassNeedsExtraction` | E | Pass '{0}' cannot take the program: some values of it have several writings. |
| `PassNotProvidedByModule` | E | Module '{0}' provides no pass '{1}'; it provides {2}. |
| `PassNotExportedByModule` | E | Module '{0}' declares '{1}' but does not export it, so it cannot be named as a pass; it provides {2}. |
| `DeviceUnavailable` | E | CUDA device {0} was named by --devices but is not available: {1}. |
| `GpuScopeUnsupported` | E | `#gpu` scope '{0}': {1} cannot run on the device ({2}). |
| `GpuScopeNoDeviceProgram` | E | `#gpu` scope '{0}': {1} ({2}). |
| `PassesNeverRun` | W | Pass '{0}' consumes the graph, so {1} written after it will not run. |
| `ConstantExpressionFaults` | W | This constant expression faults whenever it is evaluated ({0}); it is not folded. |
| `TranslationLinksRuntime` | W | std::cpp has no standalone translation of {0}: the program it writes calls the Camel runtime there and is built with libcamel. |

**Semantic** (`SemanticDiag`) — 199 (185 errors, 14 warnings)

| `ReducedPrecisionUnsupported` | E | The call of '{0}' cannot take a value of the reduced-precision element type '{1}'. |
| `ImplOnBodylessDeclaration` | E | The body-less declaration '{0}' cannot carry '#impl<{1}>': it has no Camel body. |
| `ImplKernelUnknown` | E | The '#impl' tag of '{0}' names '{1}', which is no body-less declaration. |
| `ImplKernelForeign` | E | The '#impl' tag of '{0}' names '{1}', which another module declares. |
| `ImplSignatureMismatch` | E | The kernel '{0}' of the '#impl' function '{1}' has the signature {2}, but the function has {3}. |
| `ImplKernelNotBodyless` | E | The '#impl' tag of '{0}' names '{1}', which has a body. |
| `NodeOfNotAParameter` | E | '{0}' is not a const parameter of the rule '{1}', so node_of({0}) names no node. |
<!-- camel-for-ai end: diagnostics -->

## Appendix B. Standard library signatures (generated)

Every name the standard library exports, with its signature. `name: <with-parameters> (parameters) =>
result`: the ordinary parameters are the data the function works on, the `with` parameters are what
configures it, and a call chains them — `s->trim->split<','>`, `xs->filter<p>->sort_by<key>`. `int`
is the 64-bit integer and `float` the 64-bit float. A result `T!` is an error to be handled (`!!`,
`?->`, `match`), `T?` a value that may be absent (`??`).

Source: `docs/stdlib-reference.md`, generated from the declarations in `stdlib/*.cml` and
`stdlib/signatures/*.cml` by `scripts/stdlib-reference.mjs --write`. Regenerate this region with
`node scripts/camel-for-ai.mjs --write`; regenerate the page it reads with
`node scripts/stdlib-reference.mjs --write`.

<!-- camel-for-ai region: stdlib-signatures (generated from docs/stdlib-reference.md; node scripts/camel-for-ai.mjs --write) -->
**`Built-in functions`** — no import — 58 name(s)

```text
std::mem::blocks: () => { allocs: int, frees: int, live: int, bytes: int }
int: (x: i32) => int
int: (x: int) => int
int: (x: f32) => int
int: (x: float) => int
int: (x: string) => int
int: (x: bf16) => int
int: (x: f16) => int
i64: (x: i32) => int
i64: (x: int) => int
i64: (x: f32) => int
i64: (x: float) => int
i64: (x: string) => int
i64: (x: bf16) => int
i64: (x: f16) => int
i32: (x: i32) => i32
i32: (x: int) => i32
i32: (x: f32) => i32
i32: (x: float) => i32
i32: (x: string) => i32
i32: (x: bf16) => i32
i32: (x: f16) => i32
float: (x: i32) => float
float: (x: int) => float
float: (x: f32) => float
float: (x: float) => float
float: (x: string) => float
float: (x: bf16) => float
float: (x: f16) => float
f64: (x: i32) => float
f64: (x: int) => float
f64: (x: f32) => float
f64: (x: float) => float
f64: (x: string) => float
f64: (x: bf16) => float
f64: (x: f16) => float
f32: (x: i32) => f32
f32: (x: int) => f32
f32: (x: f32) => f32
f32: (x: float) => f32
f32: (x: string) => f32
f32: (x: bf16) => f32
f32: (x: f16) => f32
bf16: (x: i32) => bf16
bf16: (x: int) => bf16
bf16: (x: f32) => bf16
bf16: (x: float) => bf16
bf16: (x: bf16) => bf16
bf16: (x: string) => bf16
f16: (x: i32) => f16
f16: (x: int) => f16
f16: (x: f32) => f16
f16: (x: float) => f16
f16: (x: f16) => f16
f16: (x: string) => f16
c64: (x: i32) => c64
c64: (x: int) => c64
c64: (x: f32) => c64
c64: (x: float) => c64
c64: (x: c64) => c64
c64: (x: c128) => c64
c128: (x: i32) => c128
c128: (x: int) => c128
c128: (x: f32) => c128
c128: (x: float) => c128
c128: (x: c64) => c128
c128: (x: c128) => c128
string: (x: T) => string
parse_int: (text: string) => int!
parse_float: (text: string) => float!
error: (kind: string, message: string) => Error
print: (x: T, ...rest: U...) => T
println: (x: T, ...rest: U...) => T
eprintln: (x: T, ...rest: U...) => T
input: (prompt: string) => string
sleep: (milliseconds: int) => void
exit: (code: int) => void
format: <...args: T...> (fmt: string) => string
join: <sep: string> (xs: string[]) => string
len: (x: string) => int
len: (x: T[..]) => int
range: (start: int, end: int) => int[]
head: (xs: T[]) => T
last: (xs: T[]) => T
tail: (xs: T[]) => T[]
slice: <start: int, end: int> (xs: T[]) => T[]
concat: (xs: T[], ys: T[]) => T[]
contains: <value: T> (xs: T[]) => bool
contains: <sub: string> (s: string) => bool
zip: (xs: T[], ys: U[]) => (T, U)[]
append: <value: T> (xs: T[]) => T[]
extend: <other: T[]> (xs: T[]) => T[]
map: <f: (item: T) => U> (xs: T[]) => U[]
filter: <f: (item: T) => bool> (xs: T[]) => T[]
reduce: <f: (acc: U, item: T) => U, initial: U> (xs: T[]) => U
foreach: <f: (item: T) => void> (xs: T[]) => void
apply: <f: (item: T) => T> (xs: T[]) => T[]
attempt: (f: (...args: A) => T, ...args: A) => T!
len: (x: Map<K, V>) => int
len: (x: Set<T>) => int
get: <key: K> (m: Map<K, V>) => V?
get_or: <key: K, fallback: V> (m: Map<K, V>) => V
put: <key: K, value: V> (m: Map<K, V>) => Map<K, V>
remove: <key: K> (c: Map<K, V>) => Map<K, V>
remove: (c: Set<K>) => Set<K>
has: <key: K> (c: Map<K, V>) => bool
has: (c: Set<K>) => bool
keys: (m: Map<K, V>) => K[]
values: (m: Map<K, V>) => V[]
items: (m: Map<K, V>) => (K, V)[]
items: (m: Set<T>) => T[]
merge: (m: Map<K, V>, other: Map<K, V>) => Map<K, V>
contains: <value: T> (s: Set<T>) => bool
add: <value: T> (s: Set<T>) => Set<T>
union: (s: Set<T>, other: Set<T>) => Set<T>
intersect: (s: Set<T>, other: Set<T>) => Set<T>
difference: (s: Set<T>, other: Set<T>) => Set<T>
is_subset: (s: Set<T>, other: Set<T>) => bool
```

**`str`** — import { ... } from str — 14 name(s)

```text
split: <sep: string> (s: string) => string[]
trim: (s: string) => string
trim_start: (s: string) => string
trim_end: (s: string) => string
to_upper: (s: string) => string
to_lower: (s: string) => string
replace: <from_: string, to: string> (s: string) => string
find: <sub: string> (s: string) => int?
substr: <start: int, length: int> (s: string) => string
starts_with: <prefix: string> (s: string) => bool
ends_with: <suffix: string> (s: string) => bool
repeat: <count: int> (s: string) => string
codepoints: (s: string) => int[]
from_codepoints: (xs: int[]) => string!
```

**`bytes`** — import { ... } from bytes — 6 name(s)

```text
encode: (text: string) => Bytes
decode: (content: Bytes) => string!
from_array: (values: int[]) => Bytes
to_array: (content: Bytes) => int[]
size: (content: Bytes) => int
concat: (left: Bytes, right: Bytes) => Bytes
```

**`ckpt`** — import { ... } from ckpt — 3 name(s)

```text
tree_leaves: (tree: T) => L
save_tree: (dir: string, tree: T, dataset_manifest_sha256?: string) => void
load_tree: (dir: string, dataset_manifest_sha256?: string) => T!
```

**`tree`** — import { ... } from tree — 4 name(s)

```text
tree_paths: (t: T) => ?
tree_map: <f: F> (t: T, ..ts) => ?
tree_reduce: <f: (acc: R, leaf: A) => R> (t: T, init: R) => R
tree_unzip: (r: R, s: S) => ?
```

**`tensor`** — import { ... } from tensor — 69 name(s)

```text
new: (values: Tensor | number[]) => Tensor
empty: (shape: int[]) => Tensor
zeros: (shape: int[]) => Tensor
ones: (shape: int[]) => Tensor
full: (shape: int[], value: number) => Tensor
eye: (n: int) => Tensor
range: (start: int, stop?: int, step?: int) => Tensor<int64>
random: (shape: int[], low: number, high: number) => Tensor
randn: (shape: int[], mean?: number, std?: number) => Tensor
add: (lhs: Tensor | number, rhs: Tensor | number) => Tensor
subtract: (lhs: Tensor | number, rhs: Tensor | number) => Tensor
multiply: (lhs: Tensor | number, rhs: Tensor | number) => Tensor
divide: (lhs: Tensor | number, rhs: Tensor | number) => Tensor
pow: (lhs: Tensor | number, rhs: Tensor | number) => Tensor
maximum: (lhs: Tensor | number, rhs: Tensor | number) => Tensor
minimum: (lhs: Tensor | number, rhs: Tensor | number) => Tensor
where: (cond: Tensor, lhs: Tensor | number, rhs: Tensor | number) => Tensor
neg: (t: Tensor) => Tensor
abs: (t: Tensor) => Tensor
exp: (t: Tensor) => Tensor
log: (t: Tensor) => Tensor
sqrt: (t: Tensor) => Tensor
rsqrt: (t: Tensor) => Tensor
sigmoid: (t: Tensor) => Tensor
tanh: (t: Tensor) => Tensor
relu: (t: Tensor) => Tensor
gelu: (t: Tensor) => Tensor
erf: (t: Tensor) => Tensor
reshape: (t: Tensor | number[], shape: int[]) => Tensor
flatten: (t: Tensor, axis?: int) => Tensor
unsqueeze: (t: Tensor, axis: int) => Tensor
transpose: (t: Tensor) => Tensor
permute: (t: Tensor, perm: int[]) => Tensor
concat: (a: Tensor | number[], b: Tensor | number[], axis: int) => Tensor
slice: (t: Tensor, axis: int, start: int, end: int, step?: int) => Tensor
take: (t: Tensor, indices: Tensor | number[]) => Tensor
one_hot: (labels: Tensor | number[], classes: int) => Tensor
shape: (t: Tensor) => int[]
numel: (t: Tensor) => int
dim: (t: Tensor, axis: int) => int
dim: (t: Tensor, axis: i32) => int
sum: (t: Tensor) => float
sum: (t: Tensor, axis: int, keepdims?: bool) => Tensor
mean: (t: Tensor) => float
mean: (t: Tensor, axis: int, keepdims?: bool) => Tensor
max: (t: Tensor, axis: int, keepdims?: bool) => Tensor
min: (t: Tensor, axis: int, keepdims?: bool) => Tensor
argmax: (t: Tensor, axis: int, keepdims?: bool) => Tensor
argmin: (t: Tensor, axis: int, keepdims?: bool) => Tensor
softmax: (t: Tensor, axis?: int) => Tensor
log_softmax: (t: Tensor, axis?: int) => Tensor
softmax_grad: (y: Tensor, dy: Tensor, axis?: int) => Tensor
layer_norm: (x: Tensor, gamma: Tensor, beta: Tensor, eps?: number) => Tensor
matmul: (lhs: Tensor, rhs: Tensor) => Tensor
linear: (x: Tensor, weight: Tensor, bias?: Tensor) => Tensor
zeros_like: (t: Tensor) => Tensor
ones_like: (t: Tensor) => Tensor
cast: (t: Tensor | number[], dtype: string) => Tensor
show: (t: Tensor) => void
show: (t: A[]) => void
from_bytes: (bytes: Bytes, shape: int[]) => Tensor
to_bytes: (t: Tensor) => Bytes
load_npy: (path: string) => Tensor
save_npy: (t: Tensor, path: string) => void
num_threads: () => int
set_num_threads: (n: int) => void
set_num_threads: (n: i32) => void
gemm_backend: () => string
gemm_threads: () => int
gemm_library_threads: () => int
gemm_library: () => string
seed: (seed: int) => void
seed: (seed: i32) => void
```

**`types`** — import { ... } from types — 8 name(s)

```text
is_integer: () => bool
is_float: () => bool
is_complex: () => bool
is_numeric: () => bool
is_ordered: () => bool
is_function: () => bool
is_string: () => bool
is_bool: () => bool
```

**`array`** — import { ... } from array — 23 name(s)

```text
argsort: (keys: i32[]) => int[]
argsort: (keys: int[]) => int[]
argsort: (keys: f32[]) => int[]
argsort: (keys: float[]) => int[]
argsort: (keys: bool[]) => int[]
argsort: (keys: string[]) => int[]
sort: (xs: i32[]) => i32[]
sort: (xs: int[]) => int[]
sort: (xs: f32[]) => f32[]
sort: (xs: float[]) => float[]
insert_sort: (xs: i32[]) => i32[]
insert_sort: (xs: int[]) => int[]
insert_sort: (xs: f32[]) => f32[]
insert_sort: (xs: float[]) => float[]
quick_sort: (xs: i32[]) => i32[]
quick_sort: (xs: int[]) => int[]
quick_sort: (xs: f32[]) => f32[]
quick_sort: (xs: float[]) => float[]
merge_sort: (xs: i32[]) => i32[]
merge_sort: (xs: int[]) => int[]
merge_sort: (xs: f32[]) => f32[]
merge_sort: (xs: float[]) => float[]
merge_sorted_arrays: (left: i32[], right: i32[]) => i32[]
merge_sorted_arrays: (left: int[], right: int[]) => int[]
merge_sorted_arrays: (left: f32[], right: f32[]) => f32[]
merge_sorted_arrays: (left: float[], right: float[]) => float[]
reverse: (xs: T[]) => T[]
take: <n: int> (xs: T[]) => T[]
drop: <n: int> (xs: T[]) => T[]
enumerate: (xs: T[]) => (int, T)[]
flatten: (xss: T[][]) => T[]
unique: (xs: T[]) => T[]
index_of: <x: T> (xs: T[]) => int?
find: <pred: (x: T) => bool> (xs: T[]) => T?
any: <pred: (x: T) => bool> (xs: T[]) => bool
all: <pred: (x: T) => bool> (xs: T[]) => bool
count: <pred: (x: T) => bool> (xs: T[]) => int
sum: (xs: T[]) => T when is_numeric::<T>()
min: (xs: T[]) => T? when is_ordered::<T>()
max: (xs: T[]) => T? when is_ordered::<T>()
flat_map: <f: (x: T) => U[]> (xs: T[]) => U[]
sort_by: <key: (x: T) => K> (xs: T[]) => T[]
zip_with: <f: (a: T, b: U) => V> (xs: T[], ys: U[]) => V[]
```

**`math`** — import { ... } from math — 48 name(s)

```text
pi: float
e: float
inf: float
nan: float
sqrt: (x: i32) => f32
sqrt: (x: int) => float
sqrt: (x: f32) => f32
sqrt: (x: float) => float
abs: (x: i32) => i32
abs: (x: int) => int
abs: (x: f32) => f32
abs: (x: float) => float
abs: (x: c64) => f32
abs: (x: c128) => float
abs: (x: bf16) => bf16
abs: (x: f16) => f16
re: (x: c64) => f32
re: (x: c128) => float
im: (x: c64) => f32
im: (x: c128) => float
conj: (x: c64) => c64
conj: (x: c128) => c128
exp: (x: f32) => f32
exp: (x: float) => float
exp: (x: c64) => c64
exp: (x: c128) => c128
expm1: (x: f32) => f32
expm1: (x: float) => float
log: (x: f32) => f32
log: (x: float) => float
log2: (x: f32) => f32
log2: (x: float) => float
log10: (x: f32) => f32
log10: (x: float) => float
log1p: (x: f32) => f32
log1p: (x: float) => float
pow: (x: f32, y: f32) => f32
pow: (x: float, y: float) => float
hypot: (x: f32, y: f32) => f32
hypot: (x: float, y: float) => float
sin: (x: f32) => f32
sin: (x: float) => float
cos: (x: f32) => f32
cos: (x: float) => float
tan: (x: f32) => f32
tan: (x: float) => float
asin: (x: f32) => f32
asin: (x: float) => float
acos: (x: f32) => f32
acos: (x: float) => float
atan: (x: f32) => f32
atan: (x: float) => float
atan2: (y: f32, x: f32) => f32
atan2: (y: float, x: float) => float
sinh: (x: f32) => f32
sinh: (x: float) => float
cosh: (x: f32) => f32
cosh: (x: float) => float
tanh: (x: f32) => f32
tanh: (x: float) => float
floor: (x: f32) => f32
floor: (x: float) => float
ceil: (x: f32) => f32
ceil: (x: float) => float
round: (x: f32) => f32
round: (x: float) => float
trunc: (x: f32) => f32
trunc: (x: float) => float
min: (a: i32, b: i32) => i32
min: (a: int, b: int) => int
min: (a: f32, b: f32) => f32
min: (a: float, b: float) => float
min: (a: string, b: string) => string
min: (a: bool, b: bool) => bool
min: (a: bf16, b: bf16) => bf16
min: (a: f16, b: f16) => f16
max: (a: i32, b: i32) => i32
max: (a: int, b: int) => int
max: (a: f32, b: f32) => f32
max: (a: float, b: float) => float
max: (a: string, b: string) => string
max: (a: bool, b: bool) => bool
max: (a: bf16, b: bf16) => bf16
max: (a: f16, b: f16) => f16
clamp: (x: i32, lo: i32, hi: i32) => i32
clamp: (x: int, lo: int, hi: int) => int
clamp: (x: f32, lo: f32, hi: f32) => f32
clamp: (x: float, lo: float, hi: float) => float
sign: (x: i32) => i32
sign: (x: int) => int
sign: (x: f32) => f32
sign: (x: float) => float
is_nan: (x: f32) => bool
is_nan: (x: float) => bool
is_inf: (x: f32) => bool
is_inf: (x: float) => bool
gcd: (a: i32, b: i32) => i32
gcd: (a: int, b: int) => int
lcm: (a: i32, b: i32) => i32
lcm: (a: int, b: int) => int
bin: (x: i32) => string
bin: (x: int) => string
oct: (x: i32) => string
oct: (x: int) => string
hex: (x: i32) => string
hex: (x: int) => string
shl: (x: i32, count: i32) => i32
shl: (x: i32, count: int) => i32
shl: (x: int, count: i32) => int
shl: (x: int, count: int) => int
shr: (x: i32, count: i32) => i32
shr: (x: i32, count: int) => i32
shr: (x: int, count: i32) => int
shr: (x: int, count: int) => int
bit_and: (a: i32, b: i32) => i32
bit_and: (a: int, b: int) => int
bit_or: (a: i32, b: i32) => i32
bit_or: (a: int, b: int) => int
bit_xor: (a: i32, b: i32) => i32
bit_xor: (a: int, b: int) => int
bit_not: (x: i32) => i32
bit_not: (x: int) => int
```

**`statistics`** — import { ... } from statistics — 5 name(s)

```text
mean: (xs: i32[]) => float
mean: (xs: int[]) => float
mean: (xs: f32[]) => float
mean: (xs: float[]) => float
stdev: (xs: i32[]) => float
stdev: (xs: int[]) => float
stdev: (xs: f32[]) => float
stdev: (xs: float[]) => float
variance: (xs: i32[]) => float
variance: (xs: int[]) => float
variance: (xs: f32[]) => float
variance: (xs: float[]) => float
median: (xs: i32[]) => float?
median: (xs: int[]) => float?
median: (xs: f32[]) => float?
median: (xs: float[]) => float?
quantile: <q: float> (xs: i32[]) => float?
quantile: <q: float> (xs: int[]) => float?
quantile: <q: float> (xs: f32[]) => float?
quantile: <q: float> (xs: float[]) => float?
```

**`random`** — import { ... } from random — 10 name(s)

```text
seed: (value: int) => void
rand: () => float
randn: () => float
randint: (low: int, high: int) => int
choice: (xs: T[]) => T
sample: (xs: T[], n: int) => T[]
shuffle: (xs: T[]) => T[]
key: (seed: int) => { hi: int, lo: int }
split: (key: { hi: int, lo: int }) => ({ hi: int, lo: int }, { hi: int, lo: int })
permutation: (key: { hi: int, lo: int }, n: int) => int[]
```

**`time`** — import { ... } from time — 3 name(s)

```text
now: () => float
strftime: (timestamp: float, format: string) => string
strptime: (text: string, format: string) => float
```

**`fs`** — import { ... } from fs — 15 name(s)

```text
read_text: (path: string) => string
try_read_text: (path: string) => string!
read_chunk: (path: string, offset: int, size: int) => Bytes
read_bytes: (path: string) => Bytes
write_bytes: (path: string, content: Bytes) => void
replace: (path: string, content: Bytes) => void
write_text: (path: string, content: string) => void
exists: (path: string) => bool
is_file: (path: string) => bool
is_dir: (path: string) => bool
file_size: (path: string) => int
mkdir: (path: string) => bool
mkdirs: (path: string) => bool
remove: (path: string) => void!
remove_dir: (path: string) => void!
```

**`io`** — import { ... } from io — 5 name(s)

```text
set_terminal_raw_mode: (enabled: bool) => void
has_input: () => bool
get_char: () => string
get_chars: (max: int) => string
clear_input_buffer: () => void
```

**`process`** — import { ... } from process — 4 name(s)

```text
sleep: (milliseconds: int) => void
whoami: () => string
cpu_count: () => int
thread_count: () => int
```

**`env`** — import { ... } from env — 2 name(s)

```text
get_env: (name: string) => string
get_cwd: () => string
```

**`json`** — import { ... } from json — 14 name(s)

```text
encode: (value: T) => string
encode: (value: T, indent: int) => string
decode_as: <shape: T> (text: string) => T!
decode_as: <shape: T> (text: Json) => T!
decode: (text: string) => Json!
kind: (j: Json) => string
is_null: (j: Json) => bool
get: <key: string> (j: Json) => Json?
get: <key: string> (j: Json?) => Json?
get: <key: string> (j: Json!) => Json!?
get: <key: string> (j: Json!?) => Json!?
get: <key: int> (j: Json) => Json?
get: <key: int> (j: Json?) => Json?
get: <key: int> (j: Json!) => Json!?
get: <key: int> (j: Json!?) => Json!?
as_bool: (j: Json) => bool?
as_bool: (j: Json?) => bool?
as_bool: (j: Json!) => bool!?
as_bool: (j: Json!?) => bool!?
as_int: (j: Json) => int?
as_int: (j: Json?) => int?
as_int: (j: Json!) => int!?
as_int: (j: Json!?) => int!?
as_float: (j: Json) => float?
as_float: (j: Json?) => float?
as_float: (j: Json!) => float!?
as_float: (j: Json!?) => float!?
as_string: (j: Json) => string?
as_string: (j: Json?) => string?
as_string: (j: Json!) => string!?
as_string: (j: Json!?) => string!?
as_array: (j: Json) => Json[]?
as_array: (j: Json?) => Json[]?
as_array: (j: Json!) => Json[]!?
as_array: (j: Json!?) => Json[]!?
as_object: (j: Json) => Map<string, Json>?
as_object: (j: Json?) => Map<string, Json>?
as_object: (j: Json!) => Map<string, Json>!?
as_object: (j: Json!?) => Map<string, Json>!?
```

**`hash`** — import { ... } from hash — 3 name(s)

```text
sha256: (s: string) => string
sha256: (s: Bytes) => string
sha256_rounds: (s: string, rounds: int) => string
```

**`re`** — import { ... } from re — 1 name(s)

```text
test: (pattern: string, text: string) => bool
```

**`para`** — import { ... } from para — 3 name(s)

```text
rendezvous: (parties: int, timeout_ms: int) => int
unordered_foreach: <f: (item: T) => R> (xs: T[]) => void
unordered_reduce: <f: (acc: T, item: T) => T, initial: T> (xs: T[]) => T
```

**`this`** — import { ... } from this — 1 name(s)

```text
zen: () => string
```

**`graph`** — import { ... } from graph — 42 name(s)

```text
current: () => Program
graphs: (p: Program) => Graph[]
root: (p: Program) => Graph
name: (g: Graph) => string
nodes: (g: Graph) => Node[]
inputs: (n: Node) => Node[]
with_inputs: (n: Node) => Node[]
ctrl_inputs: (n: Node) => Node[]
users: (n: Node) => Node[]
value_users: (n: Node) => Node[]
kind: (n: Node) => NodeKind
op: (n: Node) => Op?
op_name: (o: Op) => string
is_pure: (n: Node) => bool
is_anchor: (n: Node) => bool
is_written_slot: (n: Node) => bool
is_constant: (n: Node) => bool
type_of: (n: Node) => TypeRef?
type_name: (t: TypeRef) => string
callee: (n: Node) => Graph?
tags: (n: Node) => string
origin: (n: Node) => int
constant: (n: Node, sample: i32) => i32?
constant: (n: Node, sample: int) => int?
constant: (n: Node, sample: f32) => f32?
constant: (n: Node, sample: float) => float?
constant: (n: Node, sample: bool) => bool?
constant: (n: Node, sample: string) => string?
region_of: (n: Node) => Region
region_kind: (r: Region) => RegionKind
region_parent: (r: Region) => Region?
show: (n: Node) => string
id: (h: Graph) => int
id: (h: Node) => int
id: (h: Op) => int
effects: (g: Graph) => Effects
shape: (n: Node) => string?
permissions: (n: Node) => int
program_of: (f: F) => Program
graph_of: (f: F) => Graph
open_function: (f: F) => Txn
to_function: (t: Txn, sample: F) => F!
open: (p: Program) => Txn
replace: (t: Txn, old: Node, new: Node) => void
erase: (t: Txn, n: Node) => void
eval: (t: Txn, n: Node) => Node?
commit: (t: Txn) => Program!
abort: (t: Txn) => void
add_constant: (t: Txn, g: Graph, value: i32) => Node
add_constant: (t: Txn, g: Graph, value: int) => Node
add_constant: (t: Txn, g: Graph, value: f32) => Node
add_constant: (t: Txn, g: Graph, value: float) => Node
add_constant: (t: Txn, g: Graph, value: bool) => Node
add_constant: (t: Txn, g: Graph, value: string) => Node
```

**`rewrite`** — import { ... } from rewrite — 12 name(s)

```text
rewrite: (p: Program, rules: Rule[]) => Program!
rewrite: (p: Program, rules: Rule[], strict: bool) => Program!
saturate: (p: Program, rules: Rule[], rounds: int) => Program!
saturate: (p: Program, rules: Rule[], rounds: int, strict: bool) => Program!
extract: (p: Program, cost: string) => Program!
check: (rules: Rule[], samples: int, seed: int) => string
run: <rules: Rule[], rounds: int = 16, strict: bool = false, explain: bool = false> (p: Program) => Run<Program>!
explain: (r: Run<Program>, a: Node, b: Node) => Proof!
explain_all: (r: Run<Program>) => Proof!
why_not: (r: Run<Program>, rule: Rule, v: Node) => string!
rule_name: (r: Rule) => string
rule_two_way: (r: Rule) => bool
rule_needs: (r: Rule) => int
rule_consts: (r: Rule) => int
```

**`tiles`** — import { ... } from tiles — 9 name(s)

```text
upload: (value: Tensor<f32>) => Device<Tensor<f32>>
download: (value: Device<Tensor<f32>>) => Tensor<f32>
run_preferred_resident: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => Device<f32>
source: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => string
inspect: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => string
benchmark: <fn: (a: f32, b: f32) => f32> (inputs: (f32, f32)) => string
regions: <fn: (a: f32, b: f32) => f32> () => string
available: () => bool
device: () => string
```

**`pyplot`** — import { ... } from pyplot — 1 name(s)

```text
plot: (data: i32[]) => void
plot: (data: int[]) => void
plot: (data: f32[]) => void
plot: (data: float[]) => void
plot: (data: i32[], filename: string) => void
plot: (data: int[], filename: string) => void
plot: (data: f32[], filename: string) => void
plot: (data: float[], filename: string) => void
```

**`python`** — import { ... } from python — 12 name(s)

```text
py_call: (fn: string | PyObject, ...args: PyObject[]) => PyObject
py_call: (fn: string | PyObject, args?: (...PyObject) | PyObject[], kwargs?: Struct<...PyObject>) => PyObject
py_print: (...objs: PyObject[]) => PyObject?
py_println: (...objs: PyObject[]) => PyObject?
py_wrap: (x: T) => PyObject<T>
py_unwrap: (obj: PyObject<T>) => T
py_exec: (code: string) => PyObject
py_eval: (expr: string) => PyObject
py_run: (file_path: string) => PyObject
py_attr: (obj: PyObject, attr: string) => PyObject
py_import: (module_name: string) => PyObject
py_dispose: (obj: PyObject) => void
```

**`nn`** — import { ... } from nn — 19 name(s)

```text
conv2d: (input: Tensor, kernel: Tensor, bias: Tensor, stride?: int, padding?: int) => Tensor
conv2d_input_grad: (input: Tensor, kernel: Tensor, dy: Tensor, stride?: int, padding?: int) => Tensor
conv2d_kernel_grad: (input: Tensor, kernel: Tensor, dy: Tensor, stride?: int, padding?: int) => Tensor
conv2d_bias_grad: (bias: Tensor, dy: Tensor) => Tensor
max_pool2d: (input: Tensor, kernel: int, stride?: int, padding?: int) => Tensor
avg_pool2d: (input: Tensor, kernel: int, stride?: int, padding?: int) => Tensor
max_pool2d_grad: (input: Tensor, dy: Tensor, kernel: int, stride?: int, padding?: int) => Tensor
avg_pool2d_grad: (input: Tensor, dy: Tensor, kernel: int, stride?: int, padding?: int) => Tensor
batch_norm: (input: Tensor, mean: Tensor, var: Tensor, gamma: Tensor, beta: Tensor, eps?: number) => Tensor
batch_norm_train: (input: Tensor, gamma: Tensor, beta: Tensor, running_mean: Tensor, running_var: Tensor, momentum?: number, eps?: number) => (Tensor, Tensor, Tensor)
batch_norm_train_grad: (input: Tensor, gamma: Tensor, dy: Tensor, eps?: number) => (Tensor, Tensor, Tensor)
embedding: (table: Tensor, indices: Tensor) => Tensor
embedding_table_grad: (table: Tensor, indices: Tensor, dy: Tensor) => Tensor
softmax_cross_entropy: (logits: Tensor, target: Tensor) => float
softmax_cross_entropy: (logits: Tensor, target: Tensor | number[]) => float
softmax_cross_entropy_grad: (logits: Tensor, target: Tensor, dy: number) => Tensor
softmax_cross_entropy_grad: (logits: Tensor, target: Tensor | number[], dy: number) => Tensor
sgd: (params: P, grads: P, lr: float) => P
adam_state: (params: P) => OptimizerState
adam: (params: P, grads: P, state: OptimizerState, lr: float, beta1?: number, beta2?: number, eps?: number) => (P, OptimizerState)
save_params: (params: P, dir: string) => void
```

**`optim`** — import { ... } from optim — 10 name(s)

```text
constant: (rate: float) => (step: int) => float
linear_decay: (peak: float, total: int, floor: float) => (step: int) => float
cosine_decay: (peak: float, total: int, floor: float) => (step: int) => float
sgd: (schedule: (step: int) => float) => Sgd
adam: (schedule: (step: int) => float, beta1: float = 0.9, beta2: float = 0.999, eps: float = 0.00000001, decay: float = 0.0) => Adam
adamw: (schedule: (step: int) => float, beta1: float = 0.9, beta2: float = 0.999, eps: float = 0.00000001, decay: float = 0.01) => Adam
init: (opt: Sgd, params: P) => SgdState
init: (opt: Adam, params: P) => AdamState<P>
update: (opt: Sgd, params: P, grads: G, state: SgdState) => (P, SgdState)
update: (opt: Adam, params: P, grads: G, state: AdamState<P>) => (P, AdamState<P>)
```

**`autodiff`** — import { ... } from autodiff — 3 name(s)

```text
grad: (f: (x: float) => float) => (x: float) => float
with_grad: (f: (x: float) => float) => (x: float) => (float, float)
stop_gradient: (x: T) => T
```

**`sim`** — import { ... } from sim — 2 name(s)

```text
pi_hits: (samples: int, seed: int) => int
asian_call_price: (paths: int, steps: int, seed: int) => float
```

**`onnx`** — import { ... } from onnx — 2 name(s)

```text
export_model: (fn: (x: Tensor<f32>) => void, example: Tensor<f32>, path: string) => void
supported_operators: () => string[]
```
<!-- camel-for-ai end: stdlib-signatures -->

## Appendix C. Sources

*To be written.*