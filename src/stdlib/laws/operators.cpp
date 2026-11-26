// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The one operator of the laws module (see operators.h).
 */

#include "operators.h"
#include "camel/core/operator_table.h"

#include "camel/core/context/context.h"
#include "camel/core/derivative.h"
#include "camel/core/operator_traits.h"

namespace camel::laws {

namespace {

using camel::core::context::Context;

/// The identity: the value is handed back, and the derivative registry answers `noGradient` for the
/// node, so nothing behind it is on any path a cotangent is accumulated along.
slot_t stopGradientKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    return norm.get<slot_t>(0);
}

} // namespace

std::unordered_map<std::string, operator_t> operatorKernels() {
    return {
        {"stop_gradient", &stopGradientKernel},
    };
}

void registerOperatorMetadata() {
    // The declaration says `pure` and takes any value: it reads nothing, writes nothing, allocates
    // nothing, and is not elementwise (a tangent space is decided by the type, not by an element).
    // The declaration and this line are compared rather than one being taken on faith.
    camel::core::OperatorTable::instance().set(
        "laws:stop_gradient",
        {.pure = true, .elementwise = false});
    camel::core::OperatorTable::instance().declareResources(
        "laws", {{"stop_gradient", camel::core::sem::ResourceSpec{}}});
    // No derivative crosses the node: the tangent of the result is a zero of its own type and the
    // cotangent of the argument the same zero, which is what `noGradient` answers. Writing it here,
    // beside the kernel, keeps the two where a reader of one reads the other -- the library route
    // also states it as a rule (`jvp_stop_gradient`, `stop_gradient_t`), and the two must agree.
    camel::core::DerivativeRegistry::instance().setRule("laws:stop_gradient", camel::core::noGradient);
}

} // namespace camel::laws