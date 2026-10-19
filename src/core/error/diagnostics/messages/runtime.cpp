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
 * Updated: Oct. 12, 2026
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
                "Index {0} is out of range for {1} of length {2}.",
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
                "Check for infinite recursion. A call in tail position takes no frame, so write "
                "a deep recursion as a tail call; or raise the limit with --max-recursion-depth "
                "<n> or CAMEL_MAX_RECURSION_DEPTH=<n>.",
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
                "ahead of time, and a failed evaluation is a compile-time error. The program "
                "is rejected even if the call would never be reached at "
                "run time. Without std::macro in the pipeline the call runs with the program.",
            },
        },
        {
            RuntimeDiag::EntryArgumentMissing,
            {
                "EntryArgumentMissing",
                "No value was given for the {0} {1} of 'main'.",
                "The parameters of 'main' are the inputs of the program. To run it, give each "
                "one a value on the command line: --arg name=value (a number, true/false or "
                "text for a scalar, a .npy file for a tensor, a directory of .npy files for a "
                "struct of tensors). No default is assumed.",
            },
        },
        {
            RuntimeDiag::EntryArgumentInvalid,
            {
                "EntryArgumentInvalid",
                "Cannot bind the argument '{0}' of 'main': {1}.",
                "An argument is written --arg name=value, where name is a parameter of 'main' "
                "and value is read according to the parameter's declared type.",
            },
        },
        {
            RuntimeDiag::EntryResultNotWritten,
            {
                "EntryResultNotWritten",
                "Cannot write the result of 'main' to '{0}': {1}.",
                "--output <file> (-o) writes the value 'main' returns: a number, bool or text as "
                "a text file, a tensor as a .npy file, a struct or tuple as a directory with one "
                "file per member. Without it the value is printed on standard output.",
            },
        },
        {
            RuntimeDiag::InvalidPassParameter,
            {
                "InvalidPassParameter",
                "Invalid pass specification '{0}': {1}.",
                "A pass is written on the pipeline as `name` or as `name(key=value, ...)`; the "
                "values are constant expressions (bool, int, float or string). The message names "
                "the parameter, the type it has and the parameters the pass declares.",
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
            RuntimeDiag::TranslationLinksRuntime,
            {
                "TranslationLinksRuntime",
                "std::cpp has no standalone translation of {0}: the program it writes calls the "
                "Camel runtime there and is built with libcamel.",
                "Every other operator is translated into C++ that needs only the standard "
                "library (docs/cpp_export_build.md). The comment at the top of the C++ file "
                "gives the command that builds it with the runtime.",
            },
        },
        {
            RuntimeDiag::ConstantExpressionFaults,
            {
                "ConstantExpressionFaults",
                "This constant expression faults whenever it is evaluated ({0}); it is not "
                "folded.",
                "All its operands are known and computing it fails, so there is no value to "
                "put in its place. It is left as written: the program behaves the same with "
                "and without the pass, and the fault is reported with its position if the run "
                "reaches the expression. If it is never reached (an arm that is not taken), "
                "nothing happens.",
            },
        },
        {
            RuntimeDiag::GraphTooLarge,
            {
                "GraphTooLarge",
                "Pass '{0}' cannot build graph '{1}': it needs {2} {3}, and one graph holds at "
                "most {4}.",
                "A graph addresses its nodes and its edges with 16 bits. "
                "The function is not too large as written: the pass inlined or unrolled more "
                "code into this one graph than fits (a recursion unrolled over many steps, a "
                "function inlined at many call sites). Keep part of the computation out of "
                "the graph (a smaller constant depth, fewer unrolled steps), or run the "
                "pipeline without this pass.",
            },
        },
        {
            RuntimeDiag::MemoryExhausted,
            {
                "MemoryExhausted",
                "Out of memory{0}: {1}.",
                "The graph, permanent and meta spaces hold the compiled program (its runtime "
                "graphs, its types, the constants of the graphs being compiled) and grow with "
                "it; each has a total limit that only guards against a compiler that does not "
                "stop allocating. If the program is this large, raise the limit with the "
                "environment variable the message names. If a pass is named, the growth may be "
                "its doing (an inlining or unrolling that does not end): run the pipeline "
                "without it. The auto space holds run-time values: it is bounded by the memory of "
                "the host, or by CAMEL_GC_HEAP_LIMIT_MB when that is set, in which case the "
                "values the program keeps alive do not fit the limit.",
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

        {
            RuntimeDiag::InvalidNumberText,
            {
                "InvalidNumberText",
                "Cannot convert the string '{0}' to {1}.",
                "The whole string must be {2}: no blanks before or after it and nothing else. "
                "A conversion that fails is a fault and ends the program; parse_int and "
                "parse_float return an error value (T!) instead.",
            },
        },
        {
            RuntimeDiag::ConversionOutOfRange,
            {
                "ConversionOutOfRange",
                "Cannot convert {0} to {1}: {2}.",
                "A conversion whose value does not fit the target type is a fault (design "
                "principles 5.1): a float that is NaN, infinite or beyond the integer's range, "
                "or an integer beyond the range of a narrower integer type. Check the value "
                "before converting it, or clamp it (min, max) to the range of the target.",
            },
        },
        {
            RuntimeDiag::UnhandledError,
            {
                "UnhandledError",
                "An error value reached the exit of 'main' without being handled: {0}: {1}.",
                "A value of type T! that has failed must be opened before the program ends: "
                "give a replacement with '!!', pass it on with '?->', or take it apart with "
                "'match ... case error e'. The position is where the error was made (the "
                "definition of 'main' when the error records none).",
            },
        },
        {
            RuntimeDiag::SliceOutOfRange,
            {
                "SliceOutOfRange",
                "The range {0} to {1} is out of range for {2} of length {3}.",
                "A bound of 'slice' is a position from 0 to the length; the end is one past "
                "the last element taken. A range that leaves the sequence is a fault, as an "
                "index out of range is. To take or leave out at most n elements, use 'take' "
                "and 'drop' of the module 'array', which cut by definition.",
            },
        },
        {
            RuntimeDiag::FormatFailed,
            {
                "FormatFailed",
                "Cannot format with the format string '{0}': {1}.",
                "A replacement field is a pair of braces, optionally holding a format "
                "specification after a colon. There must be one argument per field, and a "
                "literal brace is written twice.",
            },
        },
        {
            RuntimeDiag::OperandTypeUnsupported,
            {
                "OperandTypeUnsupported",
                "'{0}' does not support a value of type '{1}'; it takes {2}.",
                "Convert the value to a supported type before the call.",
            },
        },
        {
            RuntimeDiag::FieldNotFound,
            {
                "FieldNotFound",
                "The struct has no field '{0}'.",
                "A struct is indexed by the name of one of its fields.",
            },
        },
        {
            RuntimeDiag::InvalidArgument,
            {
                "InvalidArgument",
                "Invalid argument of '{0}': {1}.",
                "",
            },
        },
        {
            RuntimeDiag::FileOpenFailed,
            {
                "FileOpenFailed",
                "'{0}' cannot open the file '{1}': {2}.",
                "Check that the path exists and that the program may access it; a relative path "
                "is taken from the directory the program is run in. Where a missing file is "
                "expected, fs.try_read_text returns an error value instead of ending the "
                "program.",
            },
        },
        {
            RuntimeDiag::FileSystemError,
            {
                "FileSystemError",
                "'{0}' failed for '{1}': {2}.",
                "Check the path and the permissions of the directories on it.",
            },
        },
        {
            RuntimeDiag::JsonEncodeUnsupported,
            {
                "JsonEncodeUnsupported",
                "json.encode cannot encode a value of type '{0}'.",
                "JSON has null, booleans, numbers, strings, arrays and objects: encode values "
                "made of bool, int, float, string, arrays, tuples and structs.",
            },
        },
        {
            RuntimeDiag::RegexInvalid,
            {
                "RegexInvalid",
                "'{0}': '{1}' is not a valid regular expression: {2}.",
                "Patterns use the ECMAScript syntax. A character with a special meaning is "
                "matched literally when it is written with a backslash.",
            },
        },
        {
            RuntimeDiag::TimeFormatFailed,
            {
                "TimeFormatFailed",
                "'{0}' failed: {1}.",
                "Check the format string against the strftime conversion specifiers (%Y, %m, "
                "%d, %H, %M, %S, ...).",
            },
        },
        {
            RuntimeDiag::OperatorNotImplemented,
            {
                "OperatorNotImplemented",
                "The operator '{0}' is declared but not implemented.",
                "The name exists so that programs using it are recognised; calling it is not "
                "possible yet.",
            },
        },
    };

    return infoMap;
}

} // namespace camel::core::error
