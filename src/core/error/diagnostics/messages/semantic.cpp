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
 * Updated: Apr. 06, 2026
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
                "Avoid using names like '__example__'; choose different identifiers.",
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
            SemanticDiag::ClosureCaptureAfterSelfCall,
            {
                "ClosureCaptureAfterSelfCall",
                "Closure variable '{0}' declared after the function '{1}' has already called "
                "itself.",
                "All closure captures must be declared before the first call to the function. "
                "If the function is recursive, ensure closure captures are declared before any "
                "self-call to avoid changes in its calling convention.",
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
                "A type variable is bound where its type first appears among the parameters of "
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
                "A type variable takes the type found at the position of 'typeas {0}' in the "
                "type of an argument. That position must be reachable through arrays, tuples, "
                "structs and function types.",
            },
        },
        {
            SemanticDiag::GenericArgumentsMismatch,
            {
                "GenericArgumentsMismatch",
                "The generic function '{0}' cannot be called with argument types {1}: {2}.",
                "A type variable is bound once, at its 'typeas' position, from the type of the "
                "argument found there; every other parameter is then checked against its type "
                "with the variables replaced. Nothing is converted implicitly. Signature: {3}",
            },
        },
        {
            SemanticDiag::GenericFunctionAsValue,
            {
                "GenericFunctionAsValue",
                "The generic function '{0}' is used as a value, but it is only a template until "
                "it is called with arguments that fix its type variables.",
                "Call it directly, or wrap the call in a function whose parameter types are "
                "written out, e.g. '(x: int) => {0}(x)'.",
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
                "'?" "?', '!!', '?->' and by 'match' with 'case null' / 'case error e'.",
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
                "'a ?? b' needs 'a' of type 'T?' and 'b' of type 'T'. 'a !! b' needs 'a' of "
                "type 'T!' and 'b' of type 'T'. 'x ?-> f' needs 'x' of type 'T!' and a "
                "function 'f' that takes a 'T'.",
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
            SemanticDiag::MatchWithoutDefault,
            {
                "MatchWithoutDefault",
                "This 'match' on a value of type '{0}' has no 'case _': a value that none of "
                "its cases matches stops the program.",
                "Add 'case _ => ...' for the values the cases do not list. Without it, meeting "
                "such a value is a fault at run time (RuntimeDiag::MatchNotExhaustive). A "
                "'match' on a bool that has both 'case true' and 'case false' is complete and "
                "is not reported.",
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
    };

    return infoMap;
}

} // namespace camel::core::error
