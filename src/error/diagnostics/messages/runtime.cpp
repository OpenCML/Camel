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
 * Updated: Sep. 27, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "runtime.h"

const std::unordered_map<RuntimeDiag, DiagInfo> getRuntimeDiagInfoMap() {
    return {
        {
            RuntimeDiag::RuntimeError,
            {
                "RuntimeError",
                "An error has occurred: {0}.",
                "Check the runtime environment and input data for unexpected issues.",
            },
        },
        {
            RuntimeDiag::DivisionByZero,
            {
                "DivisionByZero",
                "Attempted division by zero.",
                "Ensure the denominator is not zero before performing division.",
            },
        },
        {
            RuntimeDiag::IndexOutOfRange,
            {
                "IndexOutOfRange",
                "Index out of range: {0}.",
                "Ensure the index is within the valid range of the collection.",
            },
        },
        {
            RuntimeDiag::KeyNotFound,
            {
                "KeyNotFound",
                "Key not found in map or dictionary: '{0}'.",
                "Verify the key exists before accessing it.",
            },
        },
        {
            RuntimeDiag::IncompatibleArgType,
            {
                "IncompatibleArgType",
                "Argument {0} to function/operator {1} has incompatible type: expected '{2}', but "
                "got '{3}'.",
                "Check that the argument types match the function or operator signature.",
            },
        },
        {
            RuntimeDiag::IncorrectArgsCount,
            {
                "IncorrectArgsCount",
                "Incorrect number of arguments passed to function/operator {0}: expected {1}, but "
                "got {2}.",
                "Ensure the number of arguments matches the function definition.",
            },
        },
        {
            RuntimeDiag::MaxRecursionDepthExceeded,
            {
                "MaxRecursionDepthExceeded",
                "Maximum recursion depth exceeded when evaluating graph '{0}'.",
                "Check for infinite recursion or excessively deep recursive calls.",
            },
        },
        {
            RuntimeDiag::MismatchedOperandTypes,
            {
                "MismatchedOperandTypes",
                "Operand types do not match in function/operator '{0}', "
                "trying to operate '{1}' with '{2}'.",
                "Ensure that all operands are of compatible types for the operation.",
            },
        },
    };
}
