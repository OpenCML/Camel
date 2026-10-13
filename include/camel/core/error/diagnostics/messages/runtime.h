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

#pragma once

#include <cstdint>
#include <unordered_map>

#include "../base.h"

namespace camel::core::error {

enum class RuntimeDiag : uint32_t {
    RuntimeError              = 0x000000,
    DivisionByZero            = 0x000001,
    IndexOutOfRange           = 0x000002,
    KeyNotFound               = 0x000003,
    MaxRecursionDepthExceeded = 0x000004,
    MissingMainFunction       = 0x000005,
    UnrecognizedOperatorURI   = 0x000006,
    UnsupportedBytecode       = 0x000007,
    GraphInliningFailed       = 0x000008,
    TensorDimensionMismatch   = 0x000009,
    UnrecognizedGraphPass     = 0x00000A,
    TranslationRefused        = 0x00000B,
    PassInternalError         = 0x00000C,
    MacroEvaluationFailed     = 0x00000E,
    EntryArgumentMissing      = 0x000010,
    EntryArgumentInvalid      = 0x000011,
    EntryResultNotWritten     = 0x000012,
    GraphTooLarge             = 0x000013,
    MemoryExhausted           = 0x000014,
    // Faults of the built-in operators and of the general-purpose modules, each with the
    // values that caused it. They took the place of RuntimeError at those sites; the numbers
    // start at 0x30 to stay clear of the codes other work adds after GraphTooLarge.
    InvalidNumberText      = 0x000030,
    FormatFailed           = 0x000031,
    OperandTypeUnsupported = 0x000032,
    FieldNotFound          = 0x000033,
    InvalidArgument        = 0x000034,
    FileOpenFailed         = 0x000035,
    FileSystemError        = 0x000036,
    JsonDecodeFailed       = 0x000037,
    JsonEncodeUnsupported  = 0x000038,
    RegexInvalid           = 0x000039,
    TimeFormatFailed       = 0x00003A,
    OperatorNotImplemented = 0x00003B,
    // Faults and reports of the rulings of 2026-10-12 (design principles 2.10).
    ConversionOutOfRange   = 0x000050,
    UnhandledError         = 0x000051,
    SliceOutOfRange        = 0x000052,
    // Warnings (severity bits = Warn).
    PassesNeverRun = 0x10000F,
    ConstantExpressionFaults = 0x100015,
};

constexpr DiagType diagTypeOf(RuntimeDiag) { return DiagType::RuntimeDiag; }

const std::unordered_map<RuntimeDiag, DiagInfo> &getRuntimeDiagInfoMap();

} // namespace camel::core::error
