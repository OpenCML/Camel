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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/type.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/type.h"
#include "executor.h"
#include "operators.h"

#include <optional>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

namespace {
const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "mean",
            {{"statistics:mean",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(arr: (typeas T)[]) => double",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (norm[0]->code() != TypeCode::Array)
                          return std::nullopt;
                      TypeCode elemCode = tt::as_ptr<ArrayType>(norm[0])->elemType()->code();
                      if (elemCode == TypeCode::Int32 || elemCode == TypeCode::Int64 ||
                          elemCode == TypeCode::Float32 || elemCode == TypeCode::Float64)
                          return Type::Float64();
                      return std::nullopt;
                  })}}),
        OperatorGroup::create(
            "stdev",
            {{"statistics:stdev",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(arr: (typeas T)[]) => double",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (norm[0]->code() != TypeCode::Array)
                          return std::nullopt;
                      TypeCode elemCode = tt::as_ptr<ArrayType>(norm[0])->elemType()->code();
                      if (elemCode == TypeCode::Int32 || elemCode == TypeCode::Int64 ||
                          elemCode == TypeCode::Float32 || elemCode == TypeCode::Float64)
                          return Type::Float64();
                      return std::nullopt;
                  })}}),
    };
    return groups;
}
} // namespace

StatisticsModule::StatisticsModule(context_ptr_t ctx) : BuiltinModule("statistics", ctx) {
    for (const auto &group : getOperatorGroups())
        exportEntity(group->name(), group);
}

module_ptr_t StatisticsModule::create(context_ptr_t ctx) {
    return std::make_shared<StatisticsModule>(ctx);
}

bool StatisticsModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("statistics", [ctx = context_]() {
        return createStatisticsExecutor(ctx);
    });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new StatisticsModule(ctx->shared_from_this()); }
}
