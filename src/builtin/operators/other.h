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
 * Created: Jul. 29, 2025
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/type.h"

// Stands in for an operator that has no implementation yet. It faults in every build: an
// assertion alone is compiled out of a release build, where the operator then returned null
// (a comparison was false, an assignment did nothing) without a word.
[[noreturn]] slot_t
__not_implemented__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    camel::core::error::throwRuntimeFault(
        camel::core::error::RuntimeDiag::OperatorNotImplemented,
        "this operator");
}
