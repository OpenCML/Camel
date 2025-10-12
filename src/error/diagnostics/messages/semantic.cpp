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
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "semantic.h"

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
                "Module not found at path: {0}.",
                "Check if the module path is correct.",
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
                "Return type mismatch between branches: expected '{0}', found '{1}'.",
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
                "Check the type and ensure it is an array, a tuple, or a dict, etc. that supports "
                "indexing.",
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
                "the function signature: {2}.",
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

        // Warnings
        {
            SemanticDiag::FeatureNotSupported,
            {
                "FeatureNotSupported",
                "The feature '{0}' is not supported yet.",
                "Check the documentation for supported features.",
            },
        },
        {
            SemanticDiag::VarParamInAsyncFunction,
            {
                "VarParamInAsyncFunction",
                "Variable parameters are only allowed in sync functions.",
                "Remove the variable parameter or make the function synchronous.",
            },
        },
        {
            SemanticDiag::IgnoredSideEffect,
            {
                "IgnoredSideEffect",
                "Function with side effects is called but not waited.",
                "Consider waiting the function using the 'wait' keyword.",
            },
        },
    };

    return infoMap;
};
