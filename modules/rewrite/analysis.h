// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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