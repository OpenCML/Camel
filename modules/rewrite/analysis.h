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

/*
 * `analysis` (rewrite design 4, design 8.7): a fact computed for every value, read by name in
 * a rule condition.
 *
 * An analysis declaration is compiled by the front end to functions and one call of the
 * operator `__analysis` (see src/parse/ast/builder.cpp, visitAnalysisDecl):
 *
 *   __analysis('range', initial, join, widen, n, on0, on0_fact, constMask0, ...)
 *
 * The operator builds a description of the analysis (the patterns are read from the graphs of
 * the `on` functions, never run), evaluates the fact of `n` over the values it depends on, and
 * answers it. The same description is registered as the fact kind `user.analysis.<name>`, so a
 * fact computed for a frozen graph can be read again by name.
 */

#pragma once

#include "camel/core/operator.h"

namespace camel::core::context {
class Context;
}

namespace camel::rewritemod {

/// The kernel of the operator `__analysis` (rewrite module).
slot_t analysisKernel(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

} // namespace camel::rewritemod