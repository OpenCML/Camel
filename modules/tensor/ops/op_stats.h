/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Per-operator kernel timing. When CAMEL_TENSOR_OPSTATS is set, the kernel
 * map serves timing trampolines instead of the kernels, and a table of calls
 * and time per operator is printed to stderr at exit. Comparing the total
 * with the program's own time separates kernel time from interpretation.
 */

#pragma once

#include "camel/core/operator.h"

#include <string>

namespace camel::tensor::ops {

/// True when CAMEL_TENSOR_OPSTATS is set.
bool opStatsEnabled();

/// A kernel that times `kernel` under `name`, or `kernel` itself when no trampoline is left.
operator_t timedKernel(const std::string &name, operator_t kernel);

} // namespace camel::tensor::ops
