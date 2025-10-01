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

#pragma once

#include <cstdint>
#include <unordered_map>

#include "../base.h"

enum class RuntimeDiag : uint32_t {
    RuntimeError = 0x000000,
    DivisionByZero = 0x000001,
    IndexOutOfRange = 0x000002,
    KeyNotFound = 0x000003,
    IncompatibleArgType = 0x000004,
    IncorrectArgsCount = 0x000005,
    MaxRecursionDepthExceeded = 0x000006,
    MismatchedOperandTypes = 0x000007,
};

constexpr DiagType diagTypeOf(RuntimeDiag) { return DiagType::RuntimeDiag; }

const std::unordered_map<RuntimeDiag, DiagInfo> getRuntimeDiagInfoMap();
