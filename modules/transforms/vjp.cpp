// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `vjp` as an operator: the name, and the refusal that says what the name does not do yet.
 *
 * The reading names the function it was handed and says the graph was not built, so a run says the
 * difference between "the declaration found nothing" (`NativeDeclarationNotLoaded`, a compile
 * error) and "the declaration found this step, and this step has no graph builder yet": the first
 * is a name nothing implements, the second is a step that has not been written.
 */

#include "vjp.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/graph.h"

#include <iostream>
#include <string>

namespace camel::transforms {

namespace {

using camel::core::context::Context;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;
using camel::core::type::FunctionType;

slot_t vjpKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    auto *function = norm.get<::Function *>(0);
    const std::string name =
        (function != nullptr && function->graph() != nullptr)
            ? function->graph()->name()
            : std::string("-");
    const FunctionType *type =
        (function != nullptr && function->graph() != nullptr) ? function->graph()->funcType()
                                                             : nullptr;
    const std::string spelled = type != nullptr ? type->toString() : std::string("-");
    std::cout << "REINTERP vjp function=" << name << " type=" << spelled
              << " graph=not_built yet=no" << std::endl;
    throwRuntimeFault(
        RuntimeDiag::RuntimeError,
        "transforms: `vjp` has a name and no graph builder yet -- the reverse transform is still "
        "the library's `transpose(split(jvp(f)))`, written once per arity");
}

} // namespace

std::unordered_map<std::string, operator_t> vjpOperatorKernels() {
    return {
        {"vjp", &vjpKernel},
    };
}

void registerVjpOperatorMetadata() {
    // The module half of the URI is the file the declaration is written in (`stdlib/vjp.cml`), not
    // this module's name: a body-less declaration names `<its module>:<its name>`.
    camel::core::OperatorTable::instance().declare("vjp", {{"vjp", false}});
    // It would read the graph of the function value it is handed -- the transform is a rewrite of
    // that graph -- so it refuses a loop there, as `jvp` and `remat` do.
    camel::core::OperatorTable::instance().set("vjp:vjp", {.pure = false, .readsGraphs = true});
}

} // namespace camel::transforms