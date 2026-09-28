/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Jul. 29, 2025
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Tensor module wiring: exports the operator groups and types derived from
 * the operator registry and serves the registry's kernels through the
 * module executor.
 */

#include "module.h"

#include "camel/core/context/context.h"
#include "camel/core/module/module.h"
#include "camel/execute/executor.h"
#include "ops/registry.h"
#include "type.h"

using namespace camel::core::context;
using namespace camel::core::module;
using camel::tensor::ops::OpRegistry;

namespace {

class TensorExecutor : public Executor {
  public:
    TensorExecutor(context_ptr_t ctx, std::unordered_map<std::string, operator_t> ops)
        : Executor(std::move(ctx), std::move(ops)) {}
};

} // namespace

TensorModule::TensorModule(context_ptr_t ctx) : BuiltinModule("tensor", ctx) {
    camel::tensor::ops::registerTensorOps();
    exportType(Reference("Tensor"), camel::tensor::TensorType::Default());
    for (const auto &group : OpRegistry::instance().operatorGroups("tensor")) {
        exportEntity(group->name(), group);
        if (group->name().starts_with("__")) {
            exportDefaultImportRef(group->name());
        } else {
            exportEntity(Reference(std::vector<std::string>{"Tensor"}, group->name()), group);
        }
    }
}

module_ptr_t TensorModule::create(context_ptr_t ctx) { return std::make_shared<TensorModule>(ctx); }

bool TensorModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("tensor", [ctx = context_]() -> executor_ptr_t {
        return std::make_shared<TensorExecutor>(ctx, OpRegistry::instance().kernelMap("tensor"));
    });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new TensorModule(ctx->shared_from_this()); }
}
