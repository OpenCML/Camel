# Camel for AI — diagnostics

Every diagnostic the compiler, the passes and the run time can report. This is Appendix A of
[Camel for AI](camel-for-ai.md), and it is a document of its own because the language keeps gaining
diagnostics: the page an AI reads whole must not grow with the registry, and this catalogue is
looked up rather than read.

The blocks are the registry's sections, split by severity: an **errors** block fails the compile or
stops the run, a **warnings** block does not. The message is the template the report fills in, with
`{0}`, `{1}` for the values it names; the compiler prints a help line under it when the message has
one, and `camel --explain <name or code>` prints the entry. A diagnostic is named in full
(`SemanticDiag::UnresolvedReference`); the table gives the name after the section prefix, and the
heading over a block gives the prefix.

Source: `diagnostics/registry.tsv`, the one place a diagnostic is declared. Regenerate with
`node scripts/camel-for-ai.mjs --write`.

<!-- camel-for-ai region: diagnostics (generated from diagnostics/registry.tsv; node scripts/camel-for-ai.mjs --write) -->
| Name | What the compiler says |
|---|---|

**Internal — errors** (`InternalDiag`) — 1

| `UnknownInternalError` | An error has occurred: {0}. |

**Lexical — errors** (`LexicalDiag`) — 4

| `IllegalCharacter` | Illegal character '{0}'. |
| `InvalidNumberFormat` | Invalid number format: '{0}'. |
| `UnclosedStringLiteral` | This string literal is not closed. |
| `UnterminatedComment` | Unterminated comment: {0}. |

**Lexical — warnings** (`LexicalDiag`) — 1

| `InvalidEscapeSequence` | Invalid escape sequence in string: {0}. |

**Syntax — errors** (`SyntaxDiag`) — 18

| `MissingClosingDelimiter` | Expected '{0}' before {2}, to close the '{1}'. |
| `UnmatchedClosingDelimiter` | Unexpected '{0}': no '{1}' is open here. |
| `MissingThen` | Expected 'then' after the condition of the 'if', before {0}. |
| `ExtraSemicolon` | Unexpected ';': there is no statement between it and the ';' before it. |
| `AssignmentInCondition` | '=' assigns; it cannot be used in the condition of an 'if'. |
| `KeywordAsIdentifier` | '{0}' is a keyword and cannot be used as a name. |
| `MissingComma` | Expected {1} before {0}. |
| `MissingInitializer` | The declaration of '{0}' has no value. |
| `MissingToken` | Expected {0} before {1}. |
| `UnexpectedToken` | Unexpected {0}{1}. |
| `UnexpectedEndOfFile` | The file ends in the middle of a construct{0}. |
| `ComparisonNeedsSpace` | This '<' touches {0}, so it opens an angle-bracket argument list, which {1} does not continue. |
| `AngleCloseAfterSpace` | This '>' follows a space and is read as the comparison operator, so the angle-bracket list it was meant to close is still open at {0}. |
| `LineStartsWithOperator` | A statement cannot start with '{0}': the line break before it ended the statement of the line above. |
| `EmptyInterpolation` | An interpolation has no expression to format. |
| `AsRightSideIsType` | The right side of '{0}' is a type; write the interval as '(a..b)'. |
| `WhenAfterBody` | A 'when' condition comes before what it guards, so {0} cannot follow it. |
| `LambdaParametersNeedParentheses` | A lambda's parameters need their parentheses: write '(x) => e', not 'x => e'. |

**Syntax — warnings** (`SyntaxDiag`) — 1

| `ComparisonAcrossLines` | This '>' is read as a comparison with the next line, which starts with {0}: two statements may have been joined into one. |

**Semantic — errors** (`SemanticDiag`) — 210

| `Redeclaration` | Redeclaration of reference: '{0}'. |
| `ModuleNotFound` | Module '{0}' not found. {1} |
| `ReservedIdentifier` | Identifiers starting and ending with double underscores are reserved for internal use. |
| `TupleUnpackingCountMismatch` | Tuple unpacking requires the same number of references and data nodes, or exactly one tuple data. |
| `InvalidLiteral` | Invalid {0} literal: '{1}'. |
| `DuplicateDictKey` | Duplicate key in dict: '{0}'. |
| `DuplicateParameter` | Duplicate parameter detected: '{0}'. |
| `UnresolvedTypeReference` | Unresolved type reference: '{0}'. |
| `UnresolvedReference` | Unresolved reference: '{0}'. |
| `BranchReturnTypeMismatch` | Return type mismatch between branches (in function '{0}'): expected '{1}', found '{2}'. |
| `InvalidAccessIndex` | Invalid access index: '{0}'. |
| `TypeNotIndexable` | Type '{0}' does not support index access. |
| `NoMatchingFunction` | No matching function found for argument types: {0}. |
| `ArgumentsMismatch` | Function of type '{0}' cannot be called with argument types: {1}. |
| `ReturnTypeMismatch` | Return type '{0}' does not match the function's declared return type '{1}'. |
| `ElementTypeMismatch` | Cannot insert data of type '{1}' into a(n) {0} with element type '{2}'. |
| `CallingIncompleteFunction` | Cannot call function '{0}' with type '{1}' as its type is not fully constructed yet. |
| `ImportNameNotExported` | Reference '{0}' is declared as imported but is not exported by its module. |
| `LiteralNotRepresentable` | The literal '{0}' cannot be represented as type '{1}': {2}. |
| `DynamicCastForbidden` | Dynamic cast from '{0}' to '{1}' is not allowed. |
| `ModuleParseFailed` | Failed to parse module file: {0}. |
| `InvalidOperatorURI` | Invalid operator URI format: '{0}'. Expected 'protocol:path'. |
| `EntryModuleMissingMain` | Entry module '{0}' must define a resolvable 'main' on the module root (__root__). |
| `ArgumentsRejected` | Invalid call to '{0}' with argument types {1}: {2}. |
| `ImmutablePassedToVarParam` | '{0}' is immutable and cannot be passed to a var parameter of '{1}'. |
| `AssignmentTargetNotVariable` | The target of an assignment must be a variable, or a path from one. |
| `ConstructNotSupported` | '{0}' is not supported: {1}. |
| `InvalidTypeArgument` | The arguments of the type '{0}<...>' are not accepted: {1}. |
| `ArgumentNeedsConversion` | The call to '{0}' needs an explicit conversion: {1}. |
| `ReturnNeedsConversion` | The returned value needs an explicit conversion to the function's declared return type '{0}': {1}. |
| `ImportNameConflict` | The name '{0}' is imported from the modules '{1}' and '{2}' with {3}. |
| `FeatureNotSupported` | The feature '{0}' is not supported yet. |
| `TypeVariableRedeclared` | The type parameter '{0}' is declared twice at the head of this declaration. |
| `TypeVariableNotBindable` | The type variable '{0}' of '{1}' can never be bound: {2}. |
| `GenericArgumentsMismatch` | The generic function '{0}' cannot be called with argument types {1}: {2}. |
| `GenericFunctionAsValue` | The generic function '{0}' cannot be given its types here: {1}. |
| `GenericInstantiationTooDeep` | Instantiating the generic function '{0}' with {1} is refused: {2}. |
| `TypeFunctionArguments` | The type function '{0}' cannot be used this way: {1}. |
| `GenericFunctionUnsupported` | The generic function '{0}' cannot be {1}. |
| `FunctionNameAsValue` | '{0}' is used as a value here, but it names {1}, not one function. |
| `ThrowOutsideFallible` | 'throw' needs a function declared to return 'T!': {0}. |
| `ThrowNotAnError` | 'throw' takes an error value, not a value of type '{0}'. |
| `TaggedTypeNesting` | The type '{0}' marks '{1}' twice. |
| `TaggedValueNotOpened` | A value of type '{0}' cannot be used here: {1}. |
| `TaggedOperandMismatch` | '{0}' cannot be applied here: {1}. |
| `TaggedMatchCases` | This 'match' on a value of type '{0}' is not well formed: {1}. |
| `ImportedModuleHasErrors` | Cannot import module '{0}': it has errors ({1}). |
| `CollectionKeyNotAllowed` | The type '{0}' cannot be a key of '{1}': {2}. |
| `DuplicateCollectionKey` | The key '{0}' is written twice in this literal converted to '{1}'. |
| `MatchWithoutDefault` | This 'match' on a value of type '{0}' has no 'case _'. |
| `IfWithoutElseAsValue` | This 'if' has no 'else' and is used as a value. |
| `EmptyArrayWithoutType` | '{0}' is bound to an empty array '[]' that has no element type. |
| `PartialApplicationRefused` | The context arguments <{0}> cannot be bound to '{1}' as a value: {2}. |
| `ComprehensionSourceRefused` | The source of this list comprehension has type '{0}', which has no elements to take. |
| `LogicalOperandNotBool` | The {1} side of '{0}' has type '{2}', not 'bool'. |
| `ExportedVariable` | The variable '{0}' cannot be exported. |
| `ModuleValueCycle` | The value '{0}' depends on itself: {1}. |
| `CaptureAfterRecursiveCall` | The function '{1}' uses '{0}' only after it is called in its own recursion, where the call could not pass it. |
| `AmbiguousName` | The name '{0}' is ambiguous here: {1}. |
| `ConditionNotBool` | The condition of this 'if' has type '{0}'; a condition is a 'bool'. |
| `MatchCaseTypeMismatch` | This case of a 'match' on a value of type '{0}' is a value of type '{1}'. |
| `UnusedPureResult` | The result of '{0}' is not used, and '{0}' changes nothing: it returns a new value{1}. |
| `OperatorNotOverloadable` | '{0}' cannot be overloaded: {1}. |
| `OperatorOverloadInvalid` | This overload of operator '{0}' is not valid: {1}. |
| `OperatorOverloadConflict` | Operator '{0}' is already defined for ({1}){2}. |
| `AmbiguousCall` | The call to '{0}' with ({1}) fits more than one overload. |
| `DeclaredTypeMismatch` | This value has type '{0}', which is not the declared type '{1}'. |
| `ModuleTypeCycle` | The type '{0}' is part of a cycle of types: {1}. |
| `InvalidAnnotation` | The annotation '@{0}' is not accepted: {1}. |
| `LoopBodyTypeMismatch` | The body of this loop gives a value of type '{0}'; the state of the loop has type '{1}'. |
| `LoopStateWithoutType` | The first value of the loop state '{0}' does not give it a type ('{1}'). |
| `LoopConditionNotBool` | The condition after 'while' has type '{0}'; a condition is a 'bool'. |
| `LoopNameRepeated` | The name '{0}' is declared twice in this loop head. |
| `LoopSequenceNotIterable` | 'for' takes the elements of an array, a string, a map, a tensor or a stream; '{0}' has type '{1}'. |
| `DuplicateEnumVariant` | The variant '{0}' is declared twice in enum '{1}'. |
| `DuplicateEnumField` | The field '{0}' is declared twice in variant '{1}' of enum '{2}'. |
| `EnumVariantFieldCount` | The variant '{0}' of enum '{1}' takes {2} field(s), but {3} were given. |
| `EnumPatternMismatch` | The pattern '{0}' does not match a value of type '{1}': {2}. |
| `EnumMatchNotExhaustive` | The match over '{0}' has no case for {1}, and no 'case _'. |
| `EnumFieldAccess` | '{0}' is a value of enum '{1}': its fields are not read by name or index. |
| `EnumTypeArgumentNotInferred` | Cannot infer the type argument '{0}' of enum '{1}' from the fields of '{2}'. |
| `FunctionTypeArgumentNotInferred` | Cannot infer the type argument '{0}' of '{1}': the empty literal '[]' states no element type. |
| `EnumFieldTypeMismatch` | Field {0} of variant '{1}' has type '{2}', but the value given has type '{3}'. |
| `EnumConstructionMismatch` | Variant '{0}' of enum '{1}' cannot be built from this value: {2}. |
| `ConstasNotAllowed` | The dimension name '{0}' is bound here, but only a parameter type can bind one. |
| `DimensionNameNotBound` | The dimension name '{0}' is not bound by any parameter of this signature. |
| `DimensionNameAlreadyBound` | The dimension name '{0}' is already bound; write '{0}' here. |
| `DimensionMismatch` | {0} has extent {1}, but {2}. |
| `InvalidExtentExpression` | This is not a shape: the extents of a type written with dimension names are integers, bound names, or linear expressions over them ('f32[n, 3]'). |
| `NotDifferentiable` | The function cannot be differentiated: {0}. |
| `TagNotAtPosition` | The tag '{0}' may not be written at a {1}: it is declared for {2}. |
| `TagUndeclared` | No tag '{0}' is declared. |
| `TagArgumentCount` | The tag '{0}' takes {1} parameter(s), but {2} argument(s) are written. |
| `TagArgumentNotConstant` | The argument '{0}' of the tag '{1}' is not a compile-time value. |
| `TagOnUnnamedField` | A tag on the field '{0}' of an unnamed struct type belongs to no declaration. |
| `TagArgumentNamed` | A tag argument is positional: '{0}' is named. |
| `TagArgumentType` | The parameter '{0}' of the tag '{1}' is declared '{2}', but the argument is '{3}' ({4}). |
| `TagNotAPermission` | The tag '{0}' grants no numeric permission. |
| `RuleTagNotRequires` | A rule may be preceded only by '#requires(...)', not by the tag '{0}'. |
| `TagParameterType` | The parameter '{0}' of the tag '{1}' is declared '{2}': a tag parameter is an int, a float, a bool, a string, a concrete function type, or a type parameter the condition selects with is_function. |
| `TagShadowsPermission` | The tag '{0}' is declared with the name of a tag of the language. |
| `TagFieldNameAmbiguous` | Two tags on this field are written '{0}': '{1}' and '{2}'. |
| `TagFunctionArgumentNotReadable` | The tag '{0}' takes a function for its parameter '{1}', and a 'static' head cannot give a function value yet. |
| `StaticMatchNotExhaustive` | No case of this 'match type' selects the type '{0}'. |
| `StaticMatchUnreachableCase` | The case '{0}' selects every type the cases after it could select. |
| `StaticTypeNotConcrete` | The type matched here is still '{0}' when the function is instantiated. |
| `StaticMatchTooDeep` | The 'match type' bodies are nested {0} deep. |
| `GenericInstantiationCycle` | The generic function '{0}' is instantiated with {1} while that same instance is being built, and its return type is not known yet. |
| `GenericTypeArgumentGrows` | The recursive call makes the type argument '{0}' of the generic function '{1}' grow from '{2}' to '{3}': every instance's type argument would contain the one before it, so specialization never stops. |
| `StaticForNotComposite` | The type '{0}' has no fields for a 'static for' to walk. |
| `StaticAccessNotInternal` | A field cannot be taken from '{0}' by a compile-time key. |
| `StaticAccessUnknownField` | The struct '{1}' has no field '{0}'. |
| `StaticAccessIndexOutOfRange` | The key '{0}' is not an element of the tuple '{1}' ({2} elements). |
| `StaticAccessNotConstant` | The key '{0}' of a 't.[...]' access is not known when the function is compiled. |
| `StaticComprehensionVoidElement` | The block for the field '{0}' of '{1}' gives no value, so the comprehension has no element for it. |
| `StaticIndicesNotTuple` | 'indices' walks the positions of a tuple, but '{0}' has type '{1}'. |
| `StaticLoopSequenceNotKnown` | The sequence of a 'static loop' must be known when the function is compiled: '{0}' is not. |
| `StaticHeadNameCount` | The head of this loop names {0} things; '{1}' gives {2} for each round. |
| `RestParameterNotLast` | The rest parameter '..{0}' must be the last parameter. |
| `RestParameterTypeNotSupported` | The rest parameter '..{0}' cannot write a type. |
| `RestArgumentNotLast` | The rest argument '..{0}' must be the last positional argument. |
| `RestArgumentNotTuple` | The rest argument '..{0}' is not a compile-time tuple. |
| `RestFunctionNeedsExpansion` | The rest function '{0}' cannot be used here: {1}. |
| `RestArgumentsMismatch` | The call to '{0}' gives {1} argument(s), but its fixed parameters need {2}. |
| `StaticTupleEscapes` | The build-time tuple '{0}' is not a value. |
| `ConstasTypeRequired` | The 'constas' parameter '{0}' writes no type. |
| `ConstasValueNotConstant` | The value given for the 'constas' parameter '{0}' is not a compile-time constant of its parameter's type ('{1}'). |
| `ConstasFunctionNeedsCall` | The declaration '{0}' takes a 'constas' parameter, so it has no function value. |
| `ConstasInNativeDeclaration` | The body-less declaration '{0}' cannot take a 'constas' parameter. |
| `ConstasArgumentMissing` | The call gives no value for the 'constas' parameter '{0}'. |
| `TagValueConflict` | The tag '{0}' gives the fact '{1}' the value {2}, but the node already says {3}. |
| `DimensionAfterAnyRank` | A dimension pack ('..') must be the last item of its '[...]', and only one may be written. |
| `UnknownResource` | The name '{0}' in the resource clause of '{1}' is neither a parameter nor a resource. |
| `PureAndUses` | The signature of '{0}' says both 'pure' and 'uses'. |
| `ResourceGroupCycle` | The resource group '{0}' contains itself: {1}. |
| `HandleTypeNotNominal` | '{0}' is not a nominal type declaration, so '#resource' says nothing about it. |
| `NativeResourceMismatch` | The declaration '{0}' says '{1}', but the operator '{2}' declares '{3}'. |
| `CmlUnsupportedConstruct` | std::cml cannot print {0}: {1}. |
| `CmlBigConstant` | std::cml refused the constant '{0}' of type {1} in {2}: {3} bytes, over max_literal_bytes ({4}). |
| `WhenConditionNotBool` | The 'when' condition of '{0}' is not a bool (it is '{1}'). |
| `WhenConditionNotConstant` | The 'when' condition of '{0}' cannot be evaluated when the declaration is instantiated: {1}. |
| `WhenUnknownName` | '{0}' is not visible to the 'when' condition of '{1}'. |
| `WhenTypeQueryNotConcrete` | The type query '{0}' cannot be answered for '{1}' in the 'when' condition of '{2}': the type still has a type variable. |
| `WhenConditionUnmet` | No candidate of '{0}' applies to ({1}): every candidate's 'when' condition is false. Candidates: {2} |
| `EffectInRuleCondition` | {0} has an effect, and it is inside the condition of the rule '{1}'. |
| `WithArgumentUnknown` | '{0}' is not a 'with' parameter of '{1}'. |
| `WithArgumentDuplicate` | The 'with' parameter '{0}' of '{1}' is given twice. |
| `WithArgumentMissing` | The call gives no value for the 'with' parameter '{0}' of '{1}', and it has no default. |
| `WithDefaultNotConstant` | The default of the 'with' parameter '{0}' is not a compile-time constant: {1}. |
| `WithDefaultWrongType` | The default of the 'with' parameter '{0}' has type '{1}', not '{2}'. |
| `WithParameterAfterDefault` | The 'with' parameter '{0}' has no default and follows '{1}', which has one. |
| `WithArgumentCallee` | The 'with' argument '{0}' is given by name, and the signature of '{1}' is not known where the binding is written. |
| `WithArgumentInValue` | The 'with' argument '{0}' is given by name, and this binding is used as a function value. |
| `WhenPredicateNotImported` | '{1}' is declared in module '{2}', but that name is not imported where the 'when' condition of '{0}' is written. |
| `WhenPredicateNotCompileTime` | '{1}' is not a compile-time predicate that the 'when' condition of '{0}' can call: {2}. |
| `WhenPredicateAmbiguous` | The 'when' condition of '{0}' calls '{1}', and that name is ambiguous: it is imported from {2}. |
| `NativeDeclarationIncomplete` | The body-less declaration '{0}' is incomplete: it has no result type. |
| `NativeDeclarationNotLoaded` | The declaration '{0}' names the operator '{1}', which no loaded module implements. |
| `NativeDeclarationOverlap` | Two declarations of '{0}' can take the same call: '{1}' and '{2}'. |
| `WhenDeclarationOverlap` | Two declarations of '{0}' both apply to the type '{1}': '{2}' and '{3}'. |
| `NativeDeclarationForeignOperator` | The declaration '{0}' names the operator '{1}', which another module declares. |
| `LambdaParameterWithoutType` | The type of the parameter '{0}' of this lambda is not written and cannot be inferred: {1}. |
| `TypeOfNotAValue` | typeof {0} has no value type: {1}. |
| `TypeOfNotWholeType` | typeof is written inside the larger type {0}. |
| `SameShapeComprehensionNotComposite` | {0} is not a struct or a tuple, so it has no shape to fill. |
| `SameShapeComprehensionMismatch` | The shape {0} is filled from the fields of {1}. |
| `StaticMatchPatternCannotMatch` | The identity pattern '{0}' cannot select a type here: {1}. |
| `FactArgumentNotConstant` | The argument '{0}' of a fact is not a compile-time constant of type '{1}'. |
| `FactCaseDefaultMissing` | The fact '{0}' has no 'case _', so nothing says what it is for a value no other case matched. |
| `FoldCallArgumentNotConstant` | The call of '{0}' cannot be evaluated at build time: {1}. |
| `FoldCallHasEffects` | The call of '{0}' cannot be evaluated at build time: {1}. |
| `FoldCallTooDeep` | The call of '{0}' cannot be evaluated at build time: {1}. |
| `FoldCallResultNotConstant` | The call of '{0}' cannot be used as a compile-time constant: {1}. |
| `FoldCallNotEvaluable` | The call of '{0}' cannot be evaluated at build time: {1}. |
| `ParamArgumentUnknown` | '{0}' is not a parameter of '{1}'. |
| `ParamArgumentDuplicate` | The parameter '{0}' of '{1}' is given twice. |
| `ParamArgumentMissing` | The call gives no value for the parameter '{0}' of '{1}', and it has no default. |
| `ParamArgumentCallee` | The argument '{0}' is given by name, and the signature of '{1}' is not known where the call is written. |
| `OverloadParameterNames` | The argument '{0}' is given by name, and the overloads of '{1}' do not name the parameter at that position the same way. |
| `ParamArgumentPositionalAfterNamed` | A positional argument follows a named one in the call of '{0}'. |
| `ParamDefaultNotConstant` | The default of the parameter '{0}' is not a compile-time constant: {1}. |
| `ParamDefaultWrongType` | The default of the parameter '{0}' has type '{1}', not '{2}'. |
| `ParamAfterDefault` | The parameter '{0}' has no default and follows '{1}', which has one. |
| `ParamArgumentWrongType` | The argument '{0}' of '{1}' has type '{2}', not '{3}'. |
| `CompOperandMismatch` | '..' takes two functions or two integers; the left operand is '{0}' and the right one '{1}'. |
| `CompOperandNotUnary` | '..' composes a unary function on the left; '{0}' takes {1} parameters. |
| `CompOperandGenericWithoutType` | '..' composes two functions, and its {0} operand, the generic function '{1}', has no instance here: write its type arguments ('{1}::<int>'), or give the whole expression an expected function type. |
| `RangeStepZero` | The range 'range({0}, {1}, 0)' has a step of 0, so it is no sequence of integers. |
| `SliceStepZero` | The slice 't[a:b:s]' has a step of 0, so it takes no element at all, whatever its bounds are. |
| `ReducedPrecisionUnsupported` | The call of '{0}' cannot take a value of the reduced-precision element type '{1}'. |
| `ImplOnBodylessDeclaration` | The body-less declaration '{0}' cannot carry '#impl({1})': it has no Camel body. |
| `ImplKernelUnknown` | The '#impl' tag of '{0}' names '{1}', which is no body-less declaration. |
| `ImplKernelForeign` | The '#impl' tag of '{0}' names '{1}', which another module declares. |
| `ImplSignatureMismatch` | The kernel '{0}' of the '#impl' function '{1}' has the signature {2}, but the function has {3}. |
| `ImplKernelNotBodyless` | The '#impl' tag of '{0}' names '{1}', which has a body. |
| `StreamInContainer` | '{0}' cannot be the element of {1}: a stream is consumed once, while the elements of a container are read more than once. |
| `StreamConsumedTwice` | '{0}' is consumed twice: a stream is read once on each execution path. |
| `NodeOfNotAParameter` | '{0}' is not a const parameter of the rule '{1}', so node_of({0}) names no node. |
| `BranchElementTypeMismatch` | The two arms of the branch have different element types: '{0}' and '{1}'. An array's element type is part of its type, and a number of one element type does not become a number of another by itself. |
| `RuleConstParamNotMatched` | '{0}' is a `const` parameter of the rule '{1}', and the left side matches no constant for it. |
| `RuleWithParameterUnbound` | '{0}' is a `with` parameter of the rule '{1}', it has no default, and the rule value here binds nothing for it. |
| `RuleWithArgumentNotConstant` | The value bound to the `with` parameter '{0}' of the rule '{1}' is not a compile-time constant of the type '{2}'. |
| `UsesPromiseViolated` | '{0}' does not keep its resource promise: {1}. |
| `TypeAliasRecursionUnrepresented` | The type '{0}' mentions itself through a collection: recursion through an array, a map, a set or an enum is admitted by the language, and this compiler does not have that representation. |
| `EnumWrittenWhereATypeIsExpected` | An enum is declared in a type declaration ('type Shape = enum ...') and its type is the name that declaration gives it: the variants are written there and nowhere else. |
| `StructComprehensionWithoutFields` | A struct comprehension takes its field names from 'fields T', and 'indices' walks positions, which have no names: write a tuple comprehension '( static for ... in indices ... )', or walk 'fields T'. |
| `IdentifierPatternNeedsNullableOrErrorType` | The pattern '{0}' takes a value of type 'T?' or 'T!' apart, and this value has type '{1}': write '??' or '!!', or a 'match' pattern that is a literal or '_'. |
| `FactJoinRequired` | The fact '{0}' writes no 'join'. |
| `FactConstCaseTypeRequired` | The 'const' case '{0}' of the fact '{1}' writes no type. |
| `FactCaseBindsNoVariable` | This case of the fact '{0}' names no variable. |

**Semantic — warnings** (`SemanticDiag`) — 12

| `IgnoredSideEffect` | {0} is not in a sync block, and {1}. |
| `EffectNotExecuted` | {0} has side effects but is never executed: {1}. |
| `ExitInUnusedBranch` | This '{0}' gives the value of the '{1}' it is in, and nothing uses that value: it does not leave the function. |
| `UnusedFallibleValue` | This value has type '{0}' and nothing uses it: an error it holds is lost{1}. |
| `NullTestNeverNull` | {0} has type '{1}', which is never null: this test is always {2}. |
| `KnownNotNull` | '{0}' is known not to be null here (it has type '{1}'): the null case of '{2}' is never taken. |
| `EnumVariantCaseRepeated` | The variant '{0}' is matched more than once; the later case is never taken. |
| `MatchCaseAfterWildcard` | This case follows 'case _': it is never taken. |
| `MatchWildcardRedundant` | Every variant of '{0}' is matched before 'case _': the wildcard is never taken. |
| `TagHasNoReader` | The tag '{0}' is written, but none of the passes it names ({1}) is in the pipeline. |
| `DimensionNameUsedOnce` | The dimension name '{0}' is written once in this signature, so it relates nothing. |
| `TypeVariableUnused` | The type parameter '{0}' is declared at the head but never written in the signature. |

**Run time and passes — errors** (`RuntimeDiag`) — 38

| `RuntimeError` | An error has occurred: {0}. |
| `DivisionByZero` | Attempted division by zero. |
| `IndexOutOfRange` | Index {0} is out of range for {1} of length {2}. |
| `MaxRecursionDepthExceeded` | Maximum recursion depth ({1}) exceeded when evaluating graph '{0}'. |
| `TensorDimensionMismatch` | Array shape mismatch in '{0}': {1}. |
| `UnrecognizedGraphPass` | Unrecognized graph rewriting/scheduling/translation pass '{0}'. |
| `TranslationRefused` | Translation pass '{0}' refused the graph: {1}. |
| `PassInternalError` | Pass '{0}' failed with an internal error: {1}. |
| `MacroEvaluationFailed` | '{0}' could not be evaluated by std::macro: {1}. |
| `EntryArgumentMissing` | No value was given for the {0} {1} of 'main'. |
| `EntryArgumentInvalid` | Cannot bind the argument '{0}' of 'main': {1}. |
| `EntryResultNotWritten` | Cannot write the result of 'main' to '{0}': {1}. |
| `MemoryExhausted` | Out of memory{0}: {1}. |
| `InvalidNumberText` | Cannot convert the string '{0}' to {1}. |
| `FormatFailed` | Cannot format with the format string '{0}': {1}. |
| `OperandTypeUnsupported` | '{0}' does not support a value of type '{1}'; it takes {2}. |
| `FieldNotFound` | The struct has no field '{0}'. |
| `InvalidArgument` | Invalid argument of '{0}': {1}. |
| `FileOpenFailed` | '{0}' cannot open the file '{1}': {2}. |
| `FileSystemError` | '{0}' failed for '{1}': {2}. |
| `RegexInvalid` | '{0}': '{1}' is not a valid regular expression: {2}. |
| `TimeFormatFailed` | '{0}' failed: {1}. |
| `GradientElementTypeMismatch` | The gradient of a value of type '{0}' has element type '{1}': {2}. A gradient must have the element type of the value it is the gradient of (design principles 9.9); accumulated as it is, the value would be read at the wrong width and answer with a number that is not the derivative. |
| `ValueWithNoDerivativeRule` | The operator '{0}' takes a differentiable value through its with parameter {1} (counting from 1), of type '{2}', but {3}. |
| `ConversionOutOfRange` | Cannot convert {0} to {1}: {2}. |
| `UnhandledError` | An error value reached the exit of 'main' without being handled: {0}: {1}. |
| `SliceOutOfRange` | The range {0} to {1} is out of range for {2} of length {3}. |
| `InvalidPassParameter` | Invalid pass specification '{0}': {1}. |
| `GraphPassFailed` | Pass '{0}' failed: {1}. |
| `PassNeedsExtraction` | Pass '{0}' cannot take the program: some values of it have several writings. |
| `PassNotProvidedByModule` | Module '{0}' provides no pass '{1}'; it provides {2}. |
| `PassNotExportedByModule` | Module '{0}' declares '{1}' but does not export it, so it cannot be named as a pass; it provides {2}. |
| `DeviceUnavailable` | CUDA device {0} was named by --devices but is not available: {1}. |
| `GpuScopeUnsupported` | `#gpu` scope '{0}': {1} cannot run on the device ({2}). |
| `GpuScopeNoDeviceProgram` | `#gpu` scope '{0}': {1} ({2}). |
| `GraphNoDeviceProgram` | '{0}' has no device program for '{1}': {2}. |
| `DeviceNotSelected` | No device to run '{0}' on: {1}. |
| `GpuScopeMacroNotExpanded` | `#gpu` scope '{0}': {1} is a macro operator no pass has expanded, so the derived graph does not exist when the scope is compiled; put the pass '{2}' before this one. |

**Run time and passes — warnings** (`RuntimeDiag`) — 3

| `PassesNeverRun` | Pass '{0}' consumes the graph, so {1} written after it will not run. |
| `ConstantExpressionFaults` | This constant expression faults whenever it is evaluated ({0}); it is not folded. |
| `TranslationLinksRuntime` | std::cpp has no standalone translation of {0}: the program it writes calls the Camel runtime there and is built with libcamel. |
<!-- camel-for-ai end: diagnostics -->