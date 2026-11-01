// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The operator half of `reinterp<jvp>(f)`: `jvp(f)` as a native operator of the `transforms`
 * module.
 *
 * The transform is a value-level one, not a pass (jvp.h says why: a `RuntimeGraphRewritePass` has
 * to answer with the program it was given, and this transform answers with a new function). Its
 * two halves are therefore the ones `camel::autodiff`'s `grad` has:
 *
 *   `JvpResolver`  the type of `jvp(f)`: the forward transform of the function type it is given;
 *   `jvpKernel`    a call of `jvp(f)` at run time: builds the new graph and answers with a
 *                  function value naming it.
 *
 * The resolver's type is `jvpSignature(f)` (jvp.h) wrapped as `(f: F) => lifted`, so
 * `jvp(f)(2.0, 1.0)` type-checks as calling a function of two floats whose result is the pair
 * `(value, tangent)`.
 */

#pragma once

#include "camel/core/operator.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace camel::transforms {

const std::vector<oper_group_ptr_t> &operatorGroups();
std::unordered_map<std::string, operator_t> operatorKernels();
/// Publishes the purity and the graph-reading traits of the module's own operators.
void registerOperatorMetadata();

/// The reading of one `jvp(f)` call, both halves side by side: the operator's spelling, the type the
/// call resolved to, the lifted half of it, and the type of the graph the call answered with.
///
/// The reading is the same text whichever half answered, so `at` names the half (`run` from the
/// kernel, `compile` from the front end's instantiation): what a program is judged by is the graph,
/// and the graph is the same one either way; which half built it is what decides whether the call
/// survives into the program.
void printJvpCallReading(
    std::ostream &os, camel::core::type::FunctionType *function,
    const camel::core::type::FunctionType *answered, std::string_view at);

/// Registers the module's compile-time half: the calls of `jvp` the front end answers while it builds
/// the graph they are written in (jvp_instantiate.cpp, `camel/compile/gir/compile_time.h`). The
/// shapes it answers are the ones whose lift is decided by the source; the rest keep the kernel.
void registerCompileTimeTransforms();

} // namespace camel::transforms