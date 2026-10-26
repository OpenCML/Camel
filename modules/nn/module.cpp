// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "module.h"

#include <algorithm>
#include <string_view>

#include "../tensor/ops/registry.h"
#include "camel/core/context/context.h"
#include "camel/core/module/module.h"
#include "camel/core/operator_traits.h"
#include "executor.h"
#include "layers.h"
#include "optim.h"
#include "state.h"

using namespace camel::core::context;
using namespace camel::core::module;

namespace {

/// The operators whose signatures `stdlib/nn.cml` declares. The
/// C++ side keeps their kernels, executors, traits, resources and semantics; the declaration is
/// the module's signature, so their groups are not exported here. `save_params` comes from the
/// optimizer groups rather than the tensor registry, but it stays on this list: the registry
/// loop below must never export a declared name, whether or not that name moves into the
/// registry.
constexpr std::string_view kDeclaredOperators[] = {
    "save_params",
};

bool isDeclaredOperator(std::string_view name) {
    return std::find(std::begin(kDeclaredOperators), std::end(kDeclaredOperators), name) !=
           std::end(kDeclaredOperators);
}

} // namespace

NnModule::NnModule(context_ptr_t ctx) : BuiltinModule("nn", ctx) {
    // nn's operators rely on the tensor catalog (and its tangent space) being registered.
    camel::tensor::ops::registerTensorOps();
    camel::nn::registerNnTensorOps();
    exportType(Reference("OptimizerState"), camel::nn::OptimizerStateType::Default());
    for (const auto &group : camel::tensor::ops::OpRegistry::instance().operatorGroups("nn")) {
        if (!isDeclaredOperator(group->name())) {
            exportEntity(group->name(), group);
        }
    }
    for (const auto &group : camel::nn::optimizerOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

module_ptr_t NnModule::create(context_ptr_t ctx) { return std::make_shared<NnModule>(ctx); }

bool NnModule::load() {
    if (loaded_) {
        return true;
    }
    context_->registerExecutorFactory("nn", [ctx = context_]() { return createNnExecutor(ctx); });
    // The optimizers are functions of their arguments: they return new trees and states.
    for (const char *uri : {"nn:sgd", "nn:adam_state", "nn:adam"}) {
        camel::core::OperatorTable::instance().set(uri, {.pure = true, .elementwise = false});
    }
    // Writes a file.
    camel::core::OperatorTable::instance().declare("nn", {{"save_params", false}});
    // It writes files (docs/design/op-semantics 8).
    camel::core::OperatorTable::instance().declareResources(
        "nn",
        {{"save_params",
          camel::core::sem::externalAmbient("fs", camel::core::sem::AccessMode::ReadModifyWrite)}});
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new NnModule(ctx->shared_from_this()); }
}
