/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Sep. 11, 2025
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/error/diagnostics/messages/semantic.h"

namespace camel::core::error {

const std::unordered_map<SemanticDiag, DiagInfo> &getSemanticDiagInfoMap() {
    static const std::unordered_map<SemanticDiag, DiagInfo> infoMap = {
        {
            SemanticDiag::UnknownSemanticError,
            {
                "UnknownSemanticError",
                "An error has occurred: {0}.",
                "",
            },
        },
        {
            SemanticDiag::Redeclaration,
            {
                "Redeclaration",
                "Redeclaration of reference: '{0}'.",
                "Each identifier must be declared only once in its scope.",
            },
        },
        {
            SemanticDiag::ModuleNotFound,
            {
                "ModuleNotFound",
                "Module '{0}' not found. {1}",
                "Check if the module path is correct or add the directory to search paths.",
            },
        },
        {
            SemanticDiag::ModuleParseFailed,
            {
                "ModuleParseFailed",
                "Failed to parse module file: {0}.",
                "Check the file for syntax errors.",
            },
        },
        {
            SemanticDiag::ReservedIdentifier,
            {
                "ReservedIdentifier",
                "Identifiers starting and ending with double underscores are reserved for internal "
                "use.",
                "Avoid using names like '__example__'; choose different identifiers. To "
                "overload an operator, write 'func operator +(a: V, b: V): V'.",
            },
        },
        {
            SemanticDiag::TupleUnpackingCountMismatch,
            {
                "TupleUnpackingCountMismatch",
                "Tuple unpacking requires the same number of references and data nodes, or exactly "
                "one tuple data.",
                "Ensure the number of variables matches the number of unpacked data elements, or "
                "use a single tuple value.",
            },
        },
        {
            SemanticDiag::InvalidLiteral,
            {
                "InvalidLiteral",
                "Invalid {0} literal: '{1}'.",
                "Ensure the literal '{1}' is valid and supported by the language.",
            },
        },
        {
            SemanticDiag::DuplicateDictKey,
            {
                "DuplicateDictKey",
                "Duplicate key in dict: '{0}'.",
                "Make sure all keys in a dictionary are unique.",
            },
        },
        {
            SemanticDiag::ParamRefMustBeUnqualified,
            {
                "ParamRefMustBeUnqualified",
                "Parameter reference must be unqualified: '{0}'.",
                "Use the parameter name without namespace or scope qualifiers.",
            },
        },
        {
            SemanticDiag::ParamDataMustBeStatic,
            {
                "ParamDataMustBeStatic",
                "Data for parameter '{0}' must be static.",
                "Ensure the parameter data is a compile-time constant or literal.",
            },
        },
        {
            SemanticDiag::DuplicateParameter,
            {
                "DuplicateParameter",
                "Duplicate parameter detected: '{0}'.",
                "Remove or rename the duplicated parameter.",
            },
        },
        {
            SemanticDiag::UnresolvedTypeReference,
            {
                "UnresolvedTypeReference",
                "Unresolved type reference: '{0}'.",
                "Ensure the type is defined and visible in the current scope.",
            },
        },
        {
            SemanticDiag::UnresolvedReference,
            {
                "UnresolvedReference",
                "Unresolved reference: '{0}'.",
                "Make sure the name is declared and in scope.",
            },
        },
        {
            SemanticDiag::BranchReturnTypeMismatch,
            {
                "BranchReturnTypeMismatch",
                "Return type mismatch between branches (in function '{0}'): "
                "expected '{1}', found '{2}'.",
                "Ensure all branches of if-else or match-case expressions return the same type.",
            },
        },
        {
            SemanticDiag::InvalidAccessIndex,
            {
                "InvalidAccessIndex",
                "Invalid access index: '{0}'.",
                "Ensure the index is within bounds for arrays or refers to a valid field in "
                "structs.",
            },
        },
        {
            SemanticDiag::TypeNotIndexable,
            {
                "TypeNotIndexable",
                "Type '{0}' does not support index access.",
                "Check the type and ensure it is a tuple or a struct that supports indexing.",
            },
        },
        {
            SemanticDiag::NoMatchingFunction,
            {
                "NoMatchingFunction",
                "No matching function found for argument types: {0}.",
                "Check the number and types of arguments to ensure they match one of the available "
                "overloaded function signatures. Available overloads: {1}",
            },
        },
        {
            SemanticDiag::ArgumentsMismatch,
            {
                "ArgumentsMismatch",
                "Function of type '{0}' cannot be called with argument types: {1}.",
                "Check that the function type '{0}' matches the number and types of arguments "
                "provided ({1}).",
            },
        },
        {
            SemanticDiag::ReturnTypeMismatch,
            {
                "ReturnTypeMismatch",
                "Return type '{0}' does not match the function's declared return type '{1}'.",
                "Ensure that the value returned from the function matches the type specified in "
                "the function signature: {2}. No implicit conversion is applied: convert "
                "explicitly, for example float(x) for a number or 'x as T' for a module type.",
            },
        },
        {
            SemanticDiag::ElementTypeMismatch,
            {
                "ElementTypeMismatch",
                "Cannot insert data of type '{1}' into a(n) {0} with element type '{2}'.",
                "Ensure the data type matches the {0}'s element type, or convert the data to "
                "the appropriate type before inserting.",
            },
        },
        {
            SemanticDiag::CallingIncompleteFunction,
            {
                "CallingIncompleteFunction",
                "Cannot call function '{0}' with type '{1}' as its type is not fully constructed "
                "yet.",
                "This often occurs when a function without an explicit return type calls itself, "
                "preventing the compiler from inferring the actual return type. "
                "Consider explicitly specifying the function's return type to avoid this issue.",
            },
        },
        {
            SemanticDiag::ImportNameNotExported,
            {
                "ImportNameNotExported",
                "Reference '{0}' is declared as imported but is not exported by its module.",
                "Check that the module exports the name '{0}' or remove the import declaration.",
            },
        },
        {
            SemanticDiag::LiteralStaticCastFailed,
            {
                "LiteralStaticCastFailed",
                "Failed to static cast literal data '{0}' to type '{1}'.",
                "Ensure the literal value is compatible with the target type or use an appropriate "
                "conversion.",
            },
        },
        {
            SemanticDiag::DynamicCastForbidden,
            {
                "DynamicCastForbidden",
                "Dynamic cast from '{0}' to '{1}' is not allowed.",
                "Use types that allow safe conversion, or ensure the conversion is valid.",
            },
        },
        {
            SemanticDiag::InvalidOperatorURI,
            {
                "InvalidOperatorURI",
                "Invalid operator URI format: '{0}'. Expected 'protocol:path'.",
                "Use a valid operator URI in the form protocol:path (e.g. std:add).",
            },
        },
        {
            SemanticDiag::UnrecognizedExecutorProtocol,
            {
                "UnrecognizedExecutorProtocol",
                "Executor protocol '{0}' not found.",
                "Check that the protocol is registered or refer to the documentation for supported "
                "operator protocols.",
            },
        },
        {
            SemanticDiag::EntryModuleMissingMain,
            {
                "EntryModuleMissingMain",
                "Entry module '{0}' must define a resolvable 'main' on the module root (__root__).",
                "Add a top-level main function or ensure it is registered as a direct root "
                "subgraph.",
            },
        },
        {
            SemanticDiag::ArgumentsRejected,
            {
                "ArgumentsRejected",
                "Invalid call to '{0}' with argument types {1}: {2}.",
                "The argument kinds match the operator's signature, but their statically known "
                "properties (such as tensor shapes or dtypes) are incompatible. Fix the arguments "
                "or relax their static types.",
            },
        },
        {
            SemanticDiag::AssignmentTargetNotVariable,
            {
                "AssignmentTargetNotVariable",
                "The target of an assignment must be a variable.",
                "A field, an element or any other expression cannot be assigned: the store "
                "would go to a temporary. Assign a new value to the whole variable instead.",
            },
        },
        {
            SemanticDiag::InvalidAnnotation,
            {
                "InvalidAnnotation",
                "The annotation '@{0}' is not accepted: {1}.",
                "A line that starts with '@' annotates the function declaration or the block "
                "that follows. '@name' and '@name<args>' are decorators of a function or of a "
                "block; '@numeric(...)' grants numeric permissions to a function or a block "
                "(design principles 7.5).",
            },
        },
        {
            SemanticDiag::ConstructNotSupported,
            {
                "ConstructNotSupported",
                "'{0}' is not supported: {1}.",
                "The grammar accepts this construct, but the compiler does not implement it. "
                "Remove it; nothing in the language replaces it yet.",
            },
        },
        {
            SemanticDiag::InvalidTypeArgument,
            {
                "InvalidTypeArgument",
                "The arguments of the type '{0}<...>' are not accepted: {1}.",
                "What a type takes between '<' and '>' is defined by the type itself (a type "
                "argument, or a constant written 'const <data>'). An argument a type does not "
                "accept is an error; it is never ignored.",
            },
        },
        {
            SemanticDiag::ArgumentNeedsConversion,
            {
                "ArgumentNeedsConversion",
                "The call to '{0}' needs an explicit conversion: {1}.",
                "A type that leaves open what a parameter fixes is never accepted implicitly, "
                "and nothing is checked on its behalf. {2} A conversion written with 'as' is "
                "checked when it runs; a value that does not fit stops the program at that "
                "position.",
            },
        },
        {
            SemanticDiag::ReturnNeedsConversion,
            {
                "ReturnNeedsConversion",
                "The returned value needs an explicit conversion to the function's declared "
                "return type '{0}': {1}.",
                "A type that leaves open what the declared return type fixes is never accepted "
                "implicitly, and nothing is checked on its behalf (function signature: {3}). "
                "Where the value is known to fit, write the conversion: {2}. A conversion "
                "written with 'as' is checked when it runs; a value that does not fit stops the "
                "program at that position.",
            },
        },
        {
            SemanticDiag::LiteralOutOfRange,
            {
                "LiteralOutOfRange",
                "The literal '{0}' is out of the range of type '{1}' ({2}).",
                "A literal that its type cannot hold is not wrapped around or cut off. Write a "
                "value in the range, or give the literal a wider type.",
            },
        },
        {
            SemanticDiag::ImportNameConflict,
            {
                "ImportNameConflict",
                "The name '{0}' is imported from the modules '{1}' and '{2}' with {3}.",
                "Two imports that bring the same name with the same parameter types conflict, "
                "and so do two that bring a value of the same name; neither is chosen silently. "
                "Give one of them another name where it is imported "
                "('import {{ name as other_name }} from m'), or import the name from one of the "
                "modules only and reach the other through its module ('import m', then "
                "'m::name').",
            },
        },
        {
            SemanticDiag::ImmutablePassedToVarParam,
            {
                "ImmutablePassedToVarParam",
                "'{0}' is immutable and cannot be passed to a var parameter of '{1}'.",
                "Only a variable declared with 'var' (or a var parameter) can be modified. "
                "Declare it with 'var', or bind the new value to a new name.",
            },
        },
        {
            SemanticDiag::TypeVariableNotAllowed,
            {
                "TypeVariableNotAllowed",
                "'typeas {0}' cannot bind a type variable here: {1}.",
                "A type variable is declared where it first appears in the parameters or result of "
                "a function declaration, e.g. 'func first(xs: (typeas T)[]): T', and is "
                "written as plain '{0}' everywhere after that.",
            },
        },
        {
            SemanticDiag::TypeVariableRedeclared,
            {
                "TypeVariableRedeclared",
                "'typeas {0}' cannot bind '{0}': the name already denotes a type here.",
                "Write 'typeas' only at the first occurrence of a type variable and plain "
                "'{0}' afterwards, or choose a name that is not a type in scope.",
            },
        },
        {
            SemanticDiag::TypeVariableNotBindable,
            {
                "TypeVariableNotBindable",
                "The type variable '{0}' of '{1}' can never be bound: {2}.",
                "A type variable must remain present in a parameter or result type. Its position "
                "must be reachable through arrays, tuples, "
                "structs and function types.",
            },
        },
        {
            SemanticDiag::GenericArgumentsMismatch,
            {
                "GenericArgumentsMismatch",
                "The generic function '{0}' cannot be called with argument types {1}: {2}.",
                "A type variable takes the type of the argument found at its 'typeas' position, "
                "or, when that argument says nothing (a 'null', a generic function that is "
                "itself waiting for its types), at another occurrence; every parameter is then "
                "checked against its type with the variables replaced. Result-only variables take "
                "their types from the expected result or explicit f::<T> arguments. A value enters "
                "a 'T?' "
                "or 'T!' parameter by itself; nothing else is converted. Signature: {3}",
            },
        },
        {
            SemanticDiag::GenericFunctionAsValue,
            {
                "GenericFunctionAsValue",
                "The generic function '{0}' cannot be given its types here: {1}.",
                "A generic function may stand wherever a function is expected, also as an "
                "argument of another generic function. It is a template: the compiler builds "
                "the instance once the position fixes its type variables (a parameter of "
                "function type, a declared type, the other arguments of the call). Here they "
                "are not fixed. Signature: {2}",
            },
        },
        {
            SemanticDiag::GenericInstantiationTooDeep,
            {
                "GenericInstantiationTooDeep",
                "Instantiating the generic function '{0}' with {1} is refused: {2}.",
                "Each call of a generic function with a new set of types builds a new "
                "subgraph. A generic function that calls itself with ever larger types never "
                "stops doing so.",
            },
        },
        {
            SemanticDiag::TypeFunctionArguments,
            {
                "TypeFunctionArguments",
                "The type function '{0}' cannot be used this way: {1}.",
                "A type declared with parameters, e.g. 'type Pair<T> = (T, T)', is used with "
                "one type argument per parameter between '<' and '>', e.g. 'Pair<int>'.",
            },
        },
        {
            SemanticDiag::GenericFunctionUnsupported,
            {
                "GenericFunctionUnsupported",
                "The generic function '{0}' cannot be {1}.",
                "This use of a generic function is not supported.",
            },
        },
        {
            SemanticDiag::FunctionNameAsValue,
            {
                "FunctionNameAsValue",
                "'{0}' is used as a value here, but it names {1}, not one function.",
                "Only a name that stands for exactly one function has a function value. Wrap "
                "the call in a function whose parameter types are written out, e.g. "
                "'(x: int) => {0}(x)'.",
            },
        },

        {
            SemanticDiag::FeatureNotSupported,
            {
                "FeatureNotSupported",
                "The feature '{0}' is not supported yet.",
                "Check the documentation for supported features.",
            },
        },

        // Warnings
        {
            SemanticDiag::VarParamInAsyncFunction,
            {
                "VarParamInAsyncFunction",
                "Variable parameters are only allowed in sync functions.",
                "Remove the variable parameter or make the function synchronous.",
            },
        },
        {
            SemanticDiag::CapturedWriteInAsyncFunction,
            {
                "CapturedWriteInAsyncFunction",
                "{0} writes the captured variable{1} {2} but is not declared with the 'sync' "
                "modifier.",
                "Write 'sync' before the function ('sync func f(...)', 'sync (...) => ...'): the "
                "modifier declares that the function has side effects, as it does for a "
                "function with a var parameter. It does not make the body a sync block.",
            },
        },
        {
            SemanticDiag::ThrowOutsideFallible,
            {
                "ThrowOutsideFallible",
                "'throw' needs a function declared to return 'T!': {0}.",
                "'throw e' returns an error value, as 'return' returns a value, so the "
                "function it stands in must declare a fallible return type, for example "
                "'func f(): int! {{ ... }}'. It does not unwind to a caller.",
            },
        },
        {
            SemanticDiag::ThrowNotAnError,
            {
                "ThrowNotAnError",
                "'throw' takes an error value, not a value of type '{0}'.",
                "Make one with 'error(kind, message)', for example "
                "'throw error(\"ParseError\", \"not a number\")', or hand on the one a "
                "'case error e' bound.",
            },
        },
        {
            SemanticDiag::TaggedTypeNesting,
            {
                "TaggedTypeNesting",
                "The type '{0}' marks '{1}' twice.",
                "'T?' may be null and 'T!' may have failed; a second mark of the same kind "
                "adds nothing a program could tell apart. 'T?!' and 'T!?' are allowed.",
            },
        },
        {
            SemanticDiag::TaggedValueNotOpened,
            {
                "TaggedValueNotOpened",
                "A value of type '{0}' cannot be used here: {1}.",
                "A value that may be null ('T?') or may have failed ('T!') is opened only by "
                "'?"
                "?', '!!', '?->' and by 'match' with 'case null' / 'case error e'.",
            },
        },
        {
            SemanticDiag::TryCatchNotInLanguage,
            {
                "TryCatchNotInLanguage",
                "'try'/'catch' is not part of Camel.",
                "An operation that can fail returns a value of type 'T!'. Give a fallback "
                "with 'a !! b', hand an error on with 'x ?-> f', or inspect it with 'match r "
                "{{ case error e => ... case v => ... }}'. Faults (division by zero, an index "
                "out of range) are not errors and end the program; isolating them is the "
                "job of the fault-isolation operator.",
            },
        },
        {
            SemanticDiag::TaggedOperandMismatch,
            {
                "TaggedOperandMismatch",
                "'{0}' cannot be applied here: {1}.",
                "'a ?? b' needs 'a' of type 'T?' and 'b' of type 'T' (the result is a 'T') or "
                "'T?' (the result is a 'T?', so that 'a ?? b ?? c' tries each in turn). "
                "'a !! b' needs 'a' of type 'T!' and 'b' of type 'T' or 'T!'. 'x ?-> f' needs "
                "'x' of type 'T!' and a function 'f' that takes a 'T'.",
            },
        },
        {
            SemanticDiag::TaggedMatchCases,
            {
                "TaggedMatchCases",
                "This 'match' on a value of type '{0}' is not well formed: {1}.",
                "A 'match' on a 'T?' has exactly the cases 'case null' and one other "
                "('case v' binds the value, 'case _' ignores it). A 'match' on a 'T!' has "
                "exactly 'case error e' and one other.",
            },
        },
        {
            SemanticDiag::CollectionKeyNotAllowed,
            {
                "CollectionKeyNotAllowed",
                "The type '{0}' cannot be a key of '{1}': {2}.",
                "A key of a Map and an element of a Set must have an order and an equality "
                "that cannot change: 'int', 'i32', 'bool', 'string', and tuples of those. The "
                "value type of a Map is not restricted.",
            },
        },
        {
            SemanticDiag::DuplicateCollectionKey,
            {
                "DuplicateCollectionKey",
                "The key '{0}' is written twice in this literal converted to '{1}'.",
                "A map holds one value per key. In an array computed when the program runs "
                "the later entry replaces the earlier one; in a literal a repeated key is "
                "taken for a mistake. Remove one of the two entries.",
            },
        },
        {
            SemanticDiag::EffectNotExecuted,
            {
                "EffectNotExecuted",
                "{0} has side effects but is never executed: nothing depends on it and it is "
                "not in a sync block.",
                "Evaluation is driven by dependencies: outside a sync block an expression "
                "whose result (or the variable it writes) nothing needs does not run. This is "
                "legal. To run it, put it in a sync block ('sync {{ ... }}', or a function body "
                "'func f(): T sync {{ ... }}'), or use its result.",
            },
        },
        {
            SemanticDiag::ExitInUnusedBranch,
            {
                "ExitInUnusedBranch",
                "This '{0}' gives the value of the '{1}' it is in, and nothing uses that "
                "value: it does not leave the function.",
                "An arm of an 'if' or 'match' is a graph of its own: '{0}' in an arm ends the "
                "arm and gives the value of the branch, as it does in any block. Here the "
                "branch is a statement whose value nothing takes, so the '{0}' has no effect "
                "and the function goes on with the next statement. To return from the function "
                "through a branch, make the '{1}' the last expression of the function body, or "
                "write 'return' in front of it ('return if ... then ... else ...').",
            },
        },
        {
            SemanticDiag::IfWithoutElseAsValue,
            {
                "IfWithoutElseAsValue",
                "This 'if' has no 'else' and is used as a value.",
                "An 'if' without 'else' has no value: its arm may be of any type and what the "
                "arm gives is dropped. It can only stand as a statement. Where a value is "
                "wanted, add an 'else' arm of the same type ('if c then 1 else 0'), or make "
                "the result nullable ('if c then 1 else null').",
            },
        },
        {
            SemanticDiag::EmptyArrayWithoutType,
            {
                "EmptyArrayWithoutType",
                "'{0}' is bound to an empty array '[]' that has no element type.",
                "The empty array literal takes its element type from where it stands: a "
                "declared type, a parameter, the other arm of a branch, an 'as'. Here there "
                "is none. Write the type: 'let {0}: int[] = []'.",
            },
        },
        {
            SemanticDiag::ExportedVariable,
            {
                "ExportedVariable",
                "The variable '{0}' cannot be exported.",
                "A 'var' of the top level of a module is read and written only by the module's own "
                "functions: export the functions that read and write it "
                "('export sync func bump(): int sync {{ wait cnt += 1; return cnt }}'). A 'let' "
                "can be exported.",
            },
        },
        {
            SemanticDiag::ModuleValueCycle,
            {
                "ModuleValueCycle",
                "The value '{0}' depends on itself: {1}.",
                "The values of the top level of modules are computed before 'main' starts, each "
                "from the values it uses: directly, through the "
                "functions its initializer calls, and through the values those use, in any "
                "module. A value cannot be computed from itself. Compute one of the values in "
                "the chain without the other, or turn it into a function.",
            },
        },
        {
            SemanticDiag::AmbiguousName,
            {
                "AmbiguousName",
                "The name '{0}' is ambiguous here: {1}.",
                "Two 'using namespace' in force bring something of this name that cannot be told "
                "apart: a value or a type from each, or functions of the "
                "same parameter types. Write the qualified name of the one meant.",
            },
        },
        {
            SemanticDiag::CaptureAfterRecursiveCall,
            {
                "CaptureAfterRecursiveCall",
                "The function '{1}' uses '{0}' only after it is called in its own recursion, "
                "where the call could not pass it.",
                "A function that calls itself, directly or through other functions, must use a "
                "variable it captures, or be complete, before the recursive call that would "
                "pass it: a call is ordered among the uses of the variables it was known to "
                "use, and a function value holds what the function captured when it was made. "
                "Use '{0}' before the recursive call, or pass it as a parameter.",
            },
        },
        {
            SemanticDiag::ComprehensionSourceRefused,
            {
                "ComprehensionSourceRefused",
                "The source of this list comprehension has type '{0}', which has no elements "
                "to take.",
                "A list comprehension '[e for x in xs if c]' is "
                "'xs->filter<(x) => c>->map<(x) => e>': 'xs' must be an array (or another value "
                "'map' and 'filter' accept), and 'x' has the type of its elements.",
            },
        },
        {
            SemanticDiag::AnyNotAllowed,
            {
                "AnyNotAllowed",
                "'any' is not a type a program can write: {0}.",
                "Every value has a definite type after compilation. A type parameter ('typeas T') "
                "takes values of several types, a tuple or "
                "a struct holds values of different types side by side, and 'decode_as' of "
                "the json module reads JSON of a known shape.",
            },
        },
        {
            SemanticDiag::ConditionNotBool,
            {
                "ConditionNotBool",
                "The condition of this 'if' has type '{0}'; a condition is a 'bool'.",
                "There is no truth-value conversion: compare explicitly ('n != 0', "
                "'len(s) > 0', 'x != null').",
            },
        },
        {
            SemanticDiag::MatchCaseTypeMismatch,
            {
                "MatchCaseTypeMismatch",
                "This case of a 'match' on a value of type '{0}' is a value of type '{1}'.",
                "A case is compared with the value matched, so the type of that value must "
                "accept it. Write the case as a value of that type, or "
                "convert the value matched first.",
            },
        },
        {
            SemanticDiag::PartialApplicationRefused,
            {
                "PartialApplicationRefused",
                "The context arguments <{0}> cannot be bound to '{1}' as a value: {2}.",
                "'f<w>' used as a value is a partial application: a function that already has "
                "its context ('with') arguments, 'let g = inc<1>' then 'g(2)'. It needs 'f' "
                "to be one function that takes these arguments. Called in place, "
                "'f<w>(x)', the arguments of the call choose the function.",
            },
        },
        {
            SemanticDiag::MatchWithoutDefault,
            {
                "MatchWithoutDefault",
                "This 'match' on a value of type '{0}' has no 'case _'.",
                "A 'match' must have a default case: add 'case _ => ...' for the values the "
                "cases do not list. A 'match' on a bool that has both 'case true' and "
                "'case false' is complete, and so is one over 'T?' or 'T!' with its two fixed "
                "patterns.",
            },
        },
        {
            SemanticDiag::LogicalOperandNotBool,
            {
                "LogicalOperandNotBool",
                "The {1} side of '{0}' has type '{2}', not 'bool'.",
                "'&&' and '||' are branches on a 'bool': 'a && b' is 'if a then b else false', "
                "'a || b' is 'if a then true else b'. The right side is evaluated only when "
                "the left side does not decide, both sides are 'bool', and neither operator "
                "can be overloaded. For logic that evaluates both sides (element-wise logic "
                "on tensors, an operator of a type of your own) use '&' and '|'.",
            },
        },
        {
            SemanticDiag::UnusedPureResult,
            {
                "UnusedPureResult",
                "The result of '{0}' is not used, and '{0}' changes nothing: it returns a new "
                "value{1}.",
                "The operations of the standard library return new values and leave their "
                "arguments as they were: after 'let ys = xs->append<5>', 'xs' is unchanged. "
                "To change a variable, bind it to the result, 'xs = xs->append<5>', as "
                "'x = x + 1' does for a number.",
            },
        },
        {
            SemanticDiag::OperatorNotOverloadable,
            {
                "OperatorNotOverloadable",
                "'{0}' cannot be overloaded: {1}.",
                "The operators that can be overloaded are the binary '+', '-', '*', '/', '%', "
                "'^', '@', '==', '<', '<=', '>', '>=', '&', '|', indexing '[]', and the "
                "unary '-', '!', '~'; '!=' follows '=='. '&&', '||', '?\?', '!!', '->', '?->', "
                "'..', 'as', assignment and compound assignment are constructs of the language.",
            },
        },
        {
            SemanticDiag::OperatorOverloadInvalid,
            {
                "OperatorOverloadInvalid",
                "This overload of operator '{0}' is not valid: {1}.",
                "A binary operator takes two parameters and a unary one takes one ('-' may "
                "take one or two); indexing takes the indexed value and at least one index. An "
                "operator has no with-parameters and no var parameters.",
            },
        },
        {
            SemanticDiag::OperatorOverloadConflict,
            {
                "OperatorOverloadConflict",
                "Operator '{0}' is already defined for ({1}){2}.",
                "The overloads of an operator from every module, the built-in ones included, "
                "are one set: each must take parameter types that no other one takes.",
            },
        },
        {
            SemanticDiag::AmbiguousOperatorCall,
            {
                "AmbiguousOperatorCall",
                "Operator '{0}' applies to ({1}) in more than one way.",
                "More than one overload of the operator takes these arguments; make the "
                "parameter types of one of them narrower, or convert an argument with 'as'. "
                "The overloads that take them: {2}",
            },
        },
        {
            SemanticDiag::UnusedFallibleValue,
            {
                "UnusedFallibleValue",
                "This value has type '{0}' and nothing uses it: an error it holds is lost{1}.",
                "A function that returns a 'T!' reports its failure through that value. Take "
                "the value apart with '!!', '?->' or 'match', return it from a function that "
                "declares a 'T!', or drop it on purpose with 'let _ = ...'.",
            },
        },
        {
            SemanticDiag::DeclaredTypeMismatch,
            {
                "DeclaredTypeMismatch",
                "This value has type '{0}', which is not the declared type '{1}'.",
                "A declaration written with a type takes a value of that type; nothing is "
                "converted on the way (a number does not become a float by itself). Convert "
                "the value explicitly, e.g. 'float(n)' or 'v as T', or change the declared "
                "type.",
            },
        },
        {
            SemanticDiag::LoopSequenceNotIterable,
            {
                "LoopSequenceNotIterable",
                "'for' takes the elements of an array, a string, a map or a tensor; '{0}' has "
                "type '{1}'.",
                "A loop's 'for x in xs' visits the elements of xs in order (design principles "
                "3.10): the elements of an array; the code points of a string, each a "
                "one-character string; the (key, value) pairs of a map, in key order (11.3); "
                "and the sub-tensors of a tensor along its first dimension. 'range(a, b)' is "
                "the array of the integers from a to b.",
            },
        },
        {
            SemanticDiag::LoopBodyTypeMismatch,
            {
                "LoopBodyTypeMismatch",
                "The body of this loop gives a value of type '{0}'; the state of the loop has type "
                "'{1}'.",
                "The initial state fixes the state type. Return that type after 'do', or "
                "change the initial value (for example, '0.0' instead of '0' for a float state). "
                "With multiple state names, return a tuple in the same order.",
            },
        },
        {
            SemanticDiag::LoopStateWithoutType,
            {
                "LoopStateWithoutType",
                "The first value of the loop state '{0}' does not give it a type ('{1}').",
                "The state of a loop has the type of its first value. 'null' and the empty "
                "array '[]' have none of their own: write 'null as T?', or bind the empty "
                "array with its type first ('let none: int[] = []') and start from that name.",
            },
        },
        {
            SemanticDiag::LoopConditionNotBool,
            {
                "LoopConditionNotBool",
                "The condition after 'while' has type '{0}'; a condition is a 'bool'.",
                "There is no truth-value conversion: compare explicitly ('n != 0', "
                "'len(s) > 0', 'x != null').",
            },
        },
        {
            SemanticDiag::LoopNameRepeated,
            {
                "LoopNameRepeated",
                "The name '{0}' is declared twice in this loop head.",
                "The names of a loop's state and the name 'for' binds to the element are "
                "declared together, for the condition and the body; each is given once.",
            },
        },
        {
            SemanticDiag::ModuleTypeCycle,
            {
                "ModuleTypeCycle",
                "The type '{0}' is part of a cycle of types across modules: {1}.",
                "Each of these types needs the next one to be declared first, and the last needs "
                "the first: none of them can be. Remove one of the references that close the "
                "cycle.",
            },
        },
        {
            SemanticDiag::NullTestNeverNull,
            {
                "NullTestNeverNull",
                "{0} has type '{1}', which is never null: this test is always {2}.",
                "Only a value of type 'T?' can be null. In the arm of 'if x != null' the name "
                "'x' is already a 'T'. Remove the test, or test a value that may be null.",
            },
        },
        {
            SemanticDiag::KnownNotNull,
            {
                "KnownNotNull",
                "'{0}' is known not to be null here (it has type '{1}'): the null case of "
                "'{2}' is never taken.",
                "An 'if' around this narrowed the name: in the arm where it is not null it is a "
                "'T'. Use the name directly.",
            },
        },
        {
            SemanticDiag::IntegerLiteralTruncated,
            {
                "IntegerLiteralTruncated",
                "The integer literal '{0}' is not a whole number: its value is {1}, the "
                "fraction is dropped.",
                "A number written without a decimal point is an 'int', with an exponent too. "
                "For the floating-point number write the decimal point: '{2}'.",
            },
        },
        {
            SemanticDiag::IgnoredSideEffect,
            {
                "IgnoredSideEffect",
                "{0} is neither waited nor in a sync block, and {1}.",
                "Write 'wait' before the expression or put it in a sync block: only then is the "
                "write ordered after the earlier uses of the variable. Without either, the "
                "write runs only if the variable is read afterwards.",
            },
        },
        {
            SemanticDiag::ImportedModuleHasErrors,
            {
                "ImportedModuleHasErrors",
                "Cannot import module '{0}': it has errors ({1}).",
                "The errors are reported for that module. This one follows from them and goes "
                "away when they are fixed.",
            },
        },
        {
            SemanticDiag::DuplicateEnumVariant,
            {
                "DuplicateEnumVariant",
                "The variant '{0}' is declared twice in enum '{1}'.",
                "A variant is one of the values of its enum and is written by its name; each "
                "name occurs once.",
            },
        },
        {
            SemanticDiag::DuplicateEnumField,
            {
                "DuplicateEnumField",
                "The field '{0}' is declared twice in variant '{1}' of enum '{2}'.",
                "A field of a struct variant is written by its name; each name occurs once.",
            },
        },
        {
            SemanticDiag::EnumVariantNeedsFields,
            {
                "EnumVariantNeedsFields",
                "The variant '{0}' of enum '{1}' carries {2} field(s): write '{3}' to build one.",
                "A variant without a field list is a single value; one with fields is built by "
                "writing the fields after its name, 'Shape::Circle(1.0)' or "
                "'Shape::Rect {{ w: 1.0, h: 2.0 }}'.",
            },
        },
        {
            SemanticDiag::EnumVariantFieldCount,
            {
                "EnumVariantFieldCount",
                "The variant '{0}' of enum '{1}' takes {2} field(s), but {3} were given.",
                "The fields of a variant are those its declaration writes: as many, and in the "
                "same order for a variant written with '(...)'.",
            },
        },
        {
            SemanticDiag::EnumPatternMismatch,
            {
                "EnumPatternMismatch",
                "The pattern '{0}' does not match a value of type '{1}': {2}.",
                "A pattern names a variant of the enum it matches and, if that variant has "
                "fields, binds each of them by its position ('Shape::Circle(r)') or by the name "
                "its declaration gives ('Shape::Rect {{ w, h }}').",
            },
        },
        {
            SemanticDiag::EnumMatchNotExhaustive,
            {
                "EnumMatchNotExhaustive",
                "The match over '{0}' has no case for {1}, and no 'case _'.",
                "Every variant of an enum is a value of it, so a match that names variants must "
                "name all of them or end with 'case _'. The compiler adds no arm the program does "
                "not show.",
            },
        },
        {
            SemanticDiag::EnumFieldAccess,
            {
                "EnumFieldAccess",
                "'{0}' is a value of enum '{1}': its fields are not read by name or index.",
                "A field of a variant is taken out by matching the value and binding it in the "
                "arm of that variant: 'match v {{ case {2} => ... }}'.",
            },
        },
        {
            SemanticDiag::UnionTypeRetired,
            {
                "UnionTypeRetired",
                "The union type is retired: a value has one type, and no value enters "
                "'A | B'.",
                "When a value is one of several named cases, declare an enum: "
                "'type T = enum {{ A(i32), B(string) }}'. '|' remains the logical or.",
            },
        },
        {
            SemanticDiag::ReservedTypeQuestionRetired,
            {
                "ReservedTypeQuestionRetired",
                "The reserved form 'T ? U' is retired: '?' is only the nullable mark, as in "
                "'T?'.",
                "Write 'T?' for a value that may be null and 'T!' for one that may have "
                "failed.",
            },
        },
        {
            SemanticDiag::EnumFieldTypeMismatch,
            {
                "EnumFieldTypeMismatch",
                "Field {0} of variant '{1}' has type '{2}', but the value given has type "
                "'{3}'.",
                "The fields of a variant are those its declaration writes.",
            },
        },
        {
            SemanticDiag::EnumTypeArgumentNotInferred,
            {
                "EnumTypeArgumentNotInferred",
                "Cannot infer the type argument '{0}' of enum '{1}' from the fields of "
                "'{2}'.",
                "The fields of the variant do not say what the argument is; where the value is "
                "used the expected type does: 'let x: Option<int> = Option::None'.",
            },
        },
        {
            SemanticDiag::EnumConstructionMismatch,
            {
                "EnumConstructionMismatch",
                "Variant '{0}' of enum '{1}' cannot be built from this value: {2}.",
                "A variant written with '(...)' is built from its fields in order; one written "
                "with '{{ }}' from a value that has a field of each name it declares; and one "
                "without fields is the value itself.",
            },
        },
        {
            SemanticDiag::ConstasNotAllowed,
            {
                "ConstasNotAllowed",
                "The dimension name '{0}' is bound here, but only a parameter type can bind one.",
                "Write 'constas n' where the dimension first appears in a parameter type, "
                "'x: Tensor<f32, const [constas n, 3]>'. Every later occurrence, including one in "
                "the return type, writes 'n'.",
            },
        },
        {
            SemanticDiag::DimensionNameNotBound,
            {
                "DimensionNameNotBound",
                "The dimension name '{0}' is not bound by any parameter of this signature.",
                "A name in a type's extents is bound by 'constas name' where it first appears in "
                "a parameter type. A name that appears only in the return type is an error: the "
                "caller has no argument that could say what it is.",
            },
        },
        {
            SemanticDiag::DimensionNameAlreadyBound,
            {
                "DimensionNameAlreadyBound",
                "The dimension name '{0}' is already bound; write '{0}' here instead of "
                "'constas {0}'.",
                "Only the first occurrence of a dimension name in the parameter types binds it "
                "('constas n'). Writing 'constas' again would ask for a second binding of the "
                "same name.",
            },
        },
        {
            SemanticDiag::DimensionMismatch,
            {
                "DimensionMismatch",
                "Argument {0} has extent {1}, but the dimensions the earlier arguments bound do "
                "not "
                "allow it.",
                "The dimensions a signature names are one value for the whole call: every "
                "argument whose type writes the same name must have the extent the name was "
                "bound to.",
            },
        },
        {
            SemanticDiag::InvalidExtentExpression,
            {
                "InvalidExtentExpression",
                "This is not a shape: the extents of a type written with dimension names are "
                "integers, bound names, or linear expressions over them ('const [n, 3]').",
                "Each extent is an integer, a dimension name bound by the signature, or a linear "
                "expression over such names: 'n', 'n + 1', '2 * n', 'n - k'. A dimension that is "
                "not linear is unknown.",
            },
        },
        {
            SemanticDiag::NotDifferentiable,
            {
                "NotDifferentiable",
                "The function cannot be differentiated: {0}.",
                "grad and with_grad differentiate the first `with` parameter of a function "
                "that returns a float (design principles 9.9). Weights are `with` parameters; "
                "the remaining `with` parameters are constants and the ordinary parameters are "
                "data. Give the function a `with` parameter of a differentiable type, or wrap "
                "the value to differentiate in a lambda that makes it the first `with` "
                "parameter.",
            },
        },
        {
            SemanticDiag::EnumVariantCaseRepeated,
            {
                "EnumVariantCaseRepeated",
                "The variant '{0}' is matched more than once; the later case is never taken.",
                "A branch takes the first case that matches. Remove the later case, or the "
                "earlier one.",
            },
        },
        {
            SemanticDiag::MatchCaseAfterWildcard,
            {
                "MatchCaseAfterWildcard",
                "This case follows 'case _': it is never taken.",
                "'case _' matches every value, so nothing after it can. Remove it, or move it "
                "before the wildcard.",
            },
        },
        {
            SemanticDiag::MatchWildcardRedundant,
            {
                "MatchWildcardRedundant",
                "Every variant of '{0}' is matched before 'case _': the wildcard is never taken.",
                "Naming every variant already covers every value of the enum. Remove 'case _', or "
                "remove the case it was meant to stand in for.",
            },
        },
    };

    return infoMap;
}

} // namespace camel::core::error
