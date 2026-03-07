/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may obtain a copy of the MIT license at:
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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/type.h"
#include "camel/core/type/resolver.h"
#include "executor.h"
#include "operators.h"

#include <optional>

using namespace camel::core::context;
using namespace camel::core::module;

namespace {

const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    // input/print/println 已移至内置算子（globals），io 模块保留给其它不常见算子
    static const std::vector<oper_group_ptr_t> groups = {};
    return groups;
}

} // namespace

IOModule::IOModule(context_ptr_t ctx) : BuiltinModule("io", ctx) {
    for (const auto &group : getOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

module_ptr_t IOModule::create(context_ptr_t ctx) { return std::make_shared<IOModule>(ctx); }

bool IOModule::load() {
    if (loaded_)
        return true;
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("io", [ctx]() { return createIOExecutor(ctx); });
    loaded_ = true;
    return true;
}

extern "C" {

Module *camel_module_create(Context *ctx) { return new IOModule(ctx->shared_from_this()); }
}
