/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * autodiff module wiring.
 */

#include "module.h"

#include "operators.h"

#include "camel/core/context/context.h"
#include "camel/execute/executor.h"

using namespace camel::core::context;
using namespace camel::core::module;

namespace {

class AutodiffExecutor : public Executor {
  public:
    explicit AutodiffExecutor(context_ptr_t ctx)
        : Executor(std::move(ctx), camel::autodiff::operatorKernels()) {}
};

} // namespace

AutodiffModule::AutodiffModule(context_ptr_t ctx) : BuiltinModule("autodiff", ctx) {
    camel::autodiff::registerOperatorMetadata();
    for (const auto &group : camel::autodiff::operatorGroups()) {
        exportEntity(group->name(), group);
    }
}

module_ptr_t AutodiffModule::create(context_ptr_t ctx) {
    return std::make_shared<AutodiffModule>(ctx);
}

bool AutodiffModule::load() {
    if (loaded_) {
        return true;
    }
    context_->registerExecutorFactory("autodiff", [ctx = context_]() -> executor_ptr_t {
        return std::make_shared<AutodiffExecutor>(ctx);
    });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new AutodiffModule(ctx->shared_from_this()); }
}
