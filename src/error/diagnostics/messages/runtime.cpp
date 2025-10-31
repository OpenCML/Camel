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
 * Updated: Oct. 31, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "runtime.h"

const std::unordered_map<RuntimeDiag, DiagInfo> &getRuntimeDiagInfoMap() {
    static const std::unordered_map<RuntimeDiag, DiagInfo> infoMap = {
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
            RuntimeDiag::MaxRecursionDepthExceeded,
            {
                "MaxRecursionDepthExceeded",
                "Maximum recursion depth exceeded when evaluating graph '{0}'.",
                "Check for infinite recursion or excessively deep recursive calls.",
            },
        },
        {
            RuntimeDiag::MissingMainFunction,
            {
                "MissingMainFunction",
                "No 'main' function found in module '{0}'.",
                "Define a 'main' function in module '{0}' to serve as the program entry point.",
            },
        },
        {
            RuntimeDiag::UnrecognizedOperatorURI,
            {
                "UnrecognizedOperatorURI",
                "Unrecognized operator URI: '{0}'.",
                "Ensure the operator URI is correctly specified and supported by the runtime.",
            },
        },
        {
            RuntimeDiag::UnsupportedBytecode,
            {
                "UnsupportedBytecode",
                "Encountered unsupported bytecode: '{0}'.",
                "Refer to the documentation and try using a graph scheduler that supports this "
                "bytecode.",
            },
        },
        {
            RuntimeDiag::GraphInliningFailed,
            {
                "GraphInliningFailed",
                "Failed to inline graph of node '{0}' to graph '{1}'.",
                "Refer to the documentation for possible causes and try adjusting the graph "
                "translator or optimization settings.",
            },
        },
        {
            RuntimeDiag::TensorDimensionMismatch,
            {
                "TensorDimensionMismatch",
                "Tensor dimension mismatched: expected shape '{0}', got '{1}'.",
                "Check the shapes of all operands involved in the operation and ensure they are "
                "compatible for broadcasting or direct computation.",
            },
        },

    };

    return infoMap;
}
