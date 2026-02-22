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
 * Updated: Feb. 22, 2026
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

namespace {
const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "seed",
            {{"rand:seed",
              StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Void())}}),
        OperatorGroup::create(
            "rand",
            {{"rand:rand", StaticFuncTypeResolver::create({}, {}, Type::Float64())}}),
        OperatorGroup::create(
            "randn",
            {{"rand:randn", StaticFuncTypeResolver::create({}, {}, Type::Float64())}}),
        OperatorGroup::create(
            "randint",
            {{"rand:randint",
              StaticFuncTypeResolver::create(
                  {},
                  {{Type::Int64(), false}, {Type::Int64(), false}},
                  Type::Int64())}}),
        OperatorGroup::create(
            "choice",
            {{"rand:choice",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "<> (array: T[]) => T",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (norm[0]->code() != TypeCode::Array)
                          return std::nullopt;
                      return tt::as_ptr<ArrayType>(norm[0])->elemType();
                  })}}),
        OperatorGroup::create(
            "sample",
            {{"rand:sample",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "<> (array: T[], n: int) => T[]",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (norm[0]->code() != TypeCode::Array)
                          return std::nullopt;
                      return norm[0];
                  })}}),
        OperatorGroup::create(
            "shuffle",
            {{"rand:shuffle",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "<> (array: T[]) => T[]",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (norm[0]->code() != TypeCode::Array)
                          return std::nullopt;
                      return norm[0];
                  })}}),
    };
    return groups;
}
} // namespace

RandomModule::RandomModule(context_ptr_t ctx) : BuiltinModule("random", ctx) {
    for (const auto &group : getOperatorGroups())
        exportEntity(group->name(), group);
}

module_ptr_t RandomModule::create(context_ptr_t ctx) { return std::make_shared<RandomModule>(ctx); }

bool RandomModule::load() {
    if (loaded_)
        return true;
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("rand", [ctx]() { return createRandomExecutor(ctx); });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new RandomModule(ctx->shared_from_this()); }
}
