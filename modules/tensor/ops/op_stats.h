// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
