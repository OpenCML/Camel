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
 * Updated: Mar. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/module/module.h"
#include "executor.h"
#include "operators.h"
#include "type.h"

using namespace camel::core::context;
using namespace camel::core::module;

TensorModule::TensorModule(context_ptr_t ctx) : BuiltinModule("tensor", ctx) {
    exportType(Reference("Tensor"), camel::tensor::TensorType::Default());
    for (const auto &group : getTensorOperatorGroups()) {
        exportEntity(group->name(), group);
        if (group->name().starts_with("__")) {
            exportDefaultImportRef(group->name());
        }
    }
}

module_ptr_t TensorModule::create(context_ptr_t ctx) { return std::make_shared<TensorModule>(ctx); }

bool TensorModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("tensor", [ctx = context_]() {
        return createTensorExecutor(ctx);
    });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new TensorModule(ctx->shared_from_this()); }
}
