// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The operator half of `vjp`: `vjp(f)` as a native operator, `vjp:vjp`.
 *
 * `vjp` is the reverse transform written in the standard library as `transpose(split(jvp(f)))`
 * -- three steps that already exist as operators of this module -- and the library writes it once
 * per arity, in source, because a body has to name its parameters. The declaration the author
 * asks for is one for every function type (design 6.1):
 *
 *     type Vjp<F> = (..ParamsOf<F>) => (ResultOf<F>, (ct: float) => (..Tangent<ParamsOf<F>>))
 *     export func vjp::<F>(f: F): Vjp<F> when is_function::<F>()
 *
 * A declaration without a body stands for an operator of a loaded module -- that is what
 * `NativeDeclarationNotLoaded` says when none does -- and its module half is the name of the file
 * it is written in, so the operator this file registers is `vjp:vjp` and not `transforms:vjp`.
 * The operator table is keyed by URI over every loaded module, so a module called `vjp` exists
 * nowhere in C++: the entry alone answers the question.
 *
 * This step is the first of two. It registers the name and refuses, with a reading that says
 * exactly that, so the declaration stops being an unresolved operator and starts being a step that
 * says what it does not do yet. The graph construction (`jvp` -> `split` -> `transpose`, then the
 * closure the pullback lives in) is the second step, and until it lands the library's bodies stay:
 * a call that a body can answer is still answered by it, and only a call no body can take (a
 * function of parameters the bodies' arities do not cover) reaches this kernel.
 */

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>

namespace camel::transforms {

std::unordered_map<std::string, operator_t> vjpOperatorKernels();
/// Publishes the purity and the graph-reading traits of `vjp:vjp`.
void registerVjpOperatorMetadata();

} // namespace camel::transforms