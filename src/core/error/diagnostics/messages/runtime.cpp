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
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/error/diagnostics/messages/runtime.h"

namespace camel::core::error {

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
                "Maximum recursion depth ({1}) exceeded when evaluating graph '{0}'.",
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
                "Tensor shape mismatch in '{0}': {1}.",
                "Check the shapes of all operands involved in the operation and ensure they are "
                "compatible for broadcasting or direct computation.",
            },
        },
        {
            RuntimeDiag::UnrecognizedGraphPass,
            {
                "UnrecognizedGraphPass",
                "Unrecognized graph rewriting/scheduling/translation pass '{0}'.",
                "Check the pass name for possible typos, "
                "or refer to the documentation to see the passes "
                "supported in the current version.",
            },
        },
        {
            RuntimeDiag::TranslationRefused,
            {
                "TranslationRefused",
                "Translation pass '{0}' refused the graph: {1}.",
                "A translation pass only translates the graph the preceding passes left. "
                "Run the passes named in the message before it, "
                "or remove the construct it cannot express.",
            },
        },
        {
            RuntimeDiag::MacroEvaluationFailed,
            {
                "MacroEvaluationFailed",
                "'{0}' could not be evaluated by std::macro: {1}.",
                "Every argument of this macro call is a constant, so std::macro evaluates it "
                "ahead of time, and a failed evaluation is a compile-time error (provisional "
                "rule). The program is rejected even if the call would never be reached at "
                "run time. Without std::macro in the pipeline the call runs with the program.",
            },
        },
        {
            RuntimeDiag::PassesNeverRun,
            {
                "PassesNeverRun",
                "Pass '{0}' consumes the graph, so {1} written after it will not run.",
                "A scheduling pass executes the program and a translation pass translates it; "
                "neither hands a graph on, and the pipeline ends there. Remove the passes "
                "written after it, or move them before it.",
            },
        },
        {
            RuntimeDiag::PassInternalError,
            {
                "PassInternalError",
                "Pass '{0}' failed with an internal error: {1}.",
                "The pass gave up without a diagnostic of its own: it built or was given a graph "
                "it cannot handle, which is a defect of the compiler rather than of the program. "
                "Try the pipeline without this pass or with the passes in another order, and "
                "report the program together with the pass list.",
            },
        },

    };

    return infoMap;
}

} // namespace camel::core::error
