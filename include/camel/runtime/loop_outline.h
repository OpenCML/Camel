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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/* The loops of a program as graphs of their own (src/runtime/loop_outline.cpp). */
#pragma once
#include "camel/core/context/context.h"
#include "camel/runtime/graph.h"

namespace camel::runtime {

/// A private program of the graphs reachable from `root`, in which every LOOP node names its
/// condition and its body as graphs and passes the values they read from outside as With inputs,
/// as the front end builds loops (the form LoopView calls graph form). The program itself when no
/// graph reachable from `root` has a loop in region form.
const Program *outlineLoopRegions(
    const camel::core::context::context_ptr_t &context, const Program *program, GCGraph *root);

} // namespace camel::runtime
