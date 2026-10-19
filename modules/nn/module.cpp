/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 04, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"

#include "../tensor/ops/registry.h"
#include "camel/core/context/context.h"
#include "camel/core/module/module.h"
#include "camel/core/operator_traits.h"
#include "executor.h"
#include "include/nn/checkpoint.h"
#include "layers.h"
#include "optim.h"
#include "state.h"

using namespace camel::core::context;
using namespace camel::core::module;

NnModule::NnModule(context_ptr_t ctx) : BuiltinModule("nn", ctx) {
    // nn's operators rely on the tensor catalog (and its tangent space) being registered.
    camel::tensor::ops::registerTensorOps();
    camel::nn::registerNnTensorOps();
    exportType(Reference("OptimizerState"), camel::nn::OptimizerStateType::Default());
    for (const auto &group : camel::tensor::ops::OpRegistry::instance().operatorGroups("nn")) {
        exportEntity(group->name(), group);
    }
    for (const auto &group : camel::nn::optimizerOperatorGroups()) {
        exportEntity(group->name(), group);
    }
    for (const auto &group : camel::nn::checkpointOperatorGroups())
        exportEntity(group->name(), group);
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
    camel::nn::declareCheckpointResources();
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new NnModule(ctx->shared_from_this()); }
}
