// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `fact` (rewrite design 7.5, design 8.7): a fact computed for every value, read by name in a
 * rule condition.
 *
 * A fact declaration is compiled by the front end to functions and one call of the operator
 * `__fact` (see src/parse/ast/builder.cpp, visitFactDecl):
 *
 *   __fact('range', initial, join, meet, widen, narrow, 0, n, case0, case0_fact, 1, ...)
 *
 * The operator builds a description of the fact (the patterns are read from the graphs of the
 * pattern functions, never run), evaluates the fact of `n` over the values it depends on, and
 * answers it. The same description is registered as the fact kind `user.fact.<key>`, so a fact
 * computed for a frozen graph can be read again by name.
 *
 * A declaration's `with` arguments are part of what a fact is: they are written out at the
 * query (`range<5>(x)`), and the description, the registered kind and the stored value all
 * carry them (`user.fact.range<5>`), so two queries of the same name with different arguments
 * are two facts, computed and kept apart.
 */

#pragma once

#include "camel/core/operator.h"

namespace camel::core::context {
class Context;
}

namespace camel::rewritemod {

/// The kernel of the operator `__fact` (rewrite module). The symbol keeps the name a design page
/// cites (`docs/design/rewrite/plan-verification.cn.md`, outside this change's pen); the operator,
/// its URI and the fact kind it registers are all `fact` now.
slot_t factKernel(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

} // namespace camel::rewritemod