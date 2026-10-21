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
 * Author: Camel Contributors
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/* The arms of the branches of a program as graphs of their own (src/runtime/arm_raise.cpp). */
#pragma once
#include "camel/core/context/context.h"
#include "camel/runtime/graph.h"

namespace camel::runtime {

/// A private program of the graphs reachable from `root`, in which every Arm region is a graph
/// called by a FUNC node placed after the BRCH, as the front end builds branches. The program
/// itself when no graph reachable from `root` has an Arm region.
const Program *raiseBranchArms(
    const camel::core::context::context_ptr_t &context, const Program *program, GCGraph *root);

} // namespace camel::runtime
