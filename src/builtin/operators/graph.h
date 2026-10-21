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
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Operators on the graphs of function values that the runtime itself inserts; a program cannot
 * name them.
 *
 * `:graph/original` (one Norm operand, a function value; the result has its type): the same
 * function over the program's own graph. A back end that runs a private, rewritten copy of the
 * program (loops lowered to tail recursion, camel/runtime/loop_lowering.h) passes each operand
 * of function type of an operator that reads graphs (OperatorTraits::inspectsGraphs) through
 * it, so that such an operator sees the graph as the program wrote it, LOOP nodes included
 * (docs/design/op-semantics 11.4). A function value whose graph is not a copy is returned as it
 * is; otherwise the result is a new function value over the graph of Program::original() with the
 * same captured values (the copy has the original's ports and closure: a copy only rewrites nodes
 * inside graphs).
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"

slot_t __graph_original__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
