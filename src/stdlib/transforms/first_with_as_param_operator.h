// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The operator half of `first_with_as_param`: `reinterp<first_with_as_param>(f)` as a native
 * operator of the `transforms` module.
 *
 * This is a signature reinterpretation and nothing else: `f`'s body is copied unchanged, and the
 * one thing that differs is the kind of a port. `f`'s first *declared* `with` parameter stops being
 * a `with` parameter and becomes the ordinary parameter in front of `f`'s own, so a call that wrote
 * `f<w>(x)` writes `first_with_as_param(f)(w, x)`. The `with` ports after it keep their order and
 * their kinds, one place earlier.
 *
 * Why the port kind is the whole of it: a `with` value is read by the body through a port like any
 * other value (design principles 9.9 reads a captured weight that way), so a `with` port and an
 * ordinary port differ in one fact -- which list the call site fills. Moving the parameter from the
 * angle brackets into the parentheses changes where its value arrives and nothing about what the
 * body does with it, and it is what lets a transform differentiate a weight: `jvp` and `vjp`
 * differentiate the ordinary parameters, and a weight that stays a `with` parameter has nowhere to
 * hand its direction in.
 *
 * The order matters and is the first ordinary position, not the last: `grad(f)` is
 * `vjp(reinterp<first_with_as_param>(f))` read at the first cotangent (`stdlib/vjp.cml`), so the
 * weight has to be the first parameter of the reinterpreted function. The rest of the list is
 * `FirstOf<WithOf<F>>` followed by `ParamsOf<F>`, which is the vocabulary `Gradient<F>` is written
 * with (`stdlib/types.cml`).
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type/composite/func.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace camel::transforms {

/// The type of `f` with its first `with` parameter moved onto the front of the ordinary list, or
/// nullptr when there is no such parameter to move. One law, asked by the operator's resolver (what
/// the call is typed as) and by its kernel (what the graph it hands back is typed as), so the two
/// cannot drift apart.
camel::core::type::FunctionType *firstWithAsParamSignature(
    camel::core::type::FunctionType *function);

/// The operator group of the reinterpretation (`transforms:first_with_as_param`), beside the
/// module's others.
const std::vector<oper_group_ptr_t> &firstWithAsParamOperatorGroups();
std::unordered_map<std::string, operator_t> firstWithAsParamOperatorKernels();
/// Publishes the purity and the graph-reading traits of `first_with_as_param`.
void registerFirstWithAsParamOperatorMetadata();

} // namespace camel::transforms