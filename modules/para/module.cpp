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
#include "camel/core/operator.h"
#include "camel/core/type.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/type.h"

#include <optional>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

namespace {
const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "unordered_reduce",
            {
                {
                    ":mark/unordered_reduce_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {2, {false, false}}},
                        "<func: (acc: U, item: T) => U, initial: U> (collect: T[]) => U",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (with[0]->code() != TypeCode::Array)
                                return std::nullopt;
                            const auto &vecType = tt::as_ptr<ArrayType>(with[0]);
                            if (norm[0]->code() != TypeCode::Function)
                                return std::nullopt;
                            const auto &funcType = tt::as_ptr<FunctionType>(norm[0]);
                            if (funcType->normTypesCount() < 2)
                                return std::nullopt;
                            if (!funcType->normTypeAt(1)->equals(vecType->elemType()))
                                return std::nullopt;
                            if (!funcType->normTypeAt(0)->equals(norm[1]))
                                return std::nullopt;
                            if (!funcType->exitType()->equals(norm[1]))
                                return std::nullopt;
                            return norm[1];
                        }),
                },
            }),
        OperatorGroup::create(
            "unordered_foreach",
            {
                {
                    ":mark/unordered_foreach_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<func: (item: T) => void> (collect: T[]) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (with[0]->code() != TypeCode::Array)
                                return std::nullopt;
                            const auto &vecType = tt::as_ptr<ArrayType>(with[0]);
                            if (norm[0]->code() != TypeCode::Function)
                                return std::nullopt;
                            const auto &funcType = tt::as_ptr<FunctionType>(norm[0]);
                            if (funcType->normTypesCount() != 1)
                                return std::nullopt;
                            if (!funcType->normTypeAt(0)->equals(vecType->elemType()))
                                return std::nullopt;
                            if (!funcType->exitType()->equals(Type::Void()))
                                return std::nullopt;
                            return Type::Void();
                        }),
                },
            }),
    };
    return groups;
}
} // namespace

ParaModule::ParaModule(context_ptr_t ctx) : BuiltinModule("para", ctx) {
    for (const auto &group : getOperatorGroups())
        exportEntity(group->name(), group);
}

module_ptr_t ParaModule::create(context_ptr_t ctx) { return std::make_shared<ParaModule>(ctx); }

bool ParaModule::load() {
    if (loaded_)
        return true;
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new ParaModule(ctx->shared_from_this()); }
}
