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
            "sorted",
            {
                {
                    "algo:sorted",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (array: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return std::nullopt;
                            return tt::as_ptr<ArrayType>(norm[0]);
                        }),
                },
            }),

        OperatorGroup::create(
            "sort",
            {
                {
                    "algo:sort",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {true}}},
                        "<> (var array: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return std::nullopt;
                            return tt::as_ptr<ArrayType>(norm[0]);
                        }),
                },
            }),

        OperatorGroup::create(
            "insert_sorted",
            {
                {
                    "algo:insert_sorted",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (array: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return std::nullopt;
                            return tt::as_ptr<ArrayType>(norm[0]);
                        }),
                },
            }),

        OperatorGroup::create(
            "insert_sort",
            {
                {
                    "algo:insert_sort",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {true}}},
                        "<> (var array: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return std::nullopt;
                            return tt::as_ptr<ArrayType>(norm[0]);
                        }),
                },
            }),

        OperatorGroup::create(
            "quick_sorted",
            {
                {
                    "algo:quick_sorted",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (array: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return std::nullopt;
                            return tt::as_ptr<ArrayType>(norm[0]);
                        }),
                },
            }),

        OperatorGroup::create(
            "quick_sort",
            {
                {
                    "algo:quick_sort",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {true}}},
                        "<> (var array: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return std::nullopt;
                            return tt::as_ptr<ArrayType>(norm[0]);
                        }),
                },
            }),

        OperatorGroup::create(
            "merge_sorted",
            {
                {
                    "algo:merge_sorted",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (array: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return std::nullopt;
                            return tt::as_ptr<ArrayType>(norm[0]);
                        }),
                },
            }),

        OperatorGroup::create(
            "merge_sort",
            {
                {
                    "algo:merge_sort",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {true}}},
                        "<> (var array: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return std::nullopt;
                            return tt::as_ptr<ArrayType>(norm[0]);
                        }),
                },
            }),

        OperatorGroup::create(
            "merge_sorted_arrays",
            {
                {
                    "algo:merge_sorted_arrays",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "<> (lft: T[], rgt: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array ||
                                norm[1]->code() != TypeCode::Array)
                                return std::nullopt;
                            const auto &lftElemType = tt::as_ptr<ArrayType>(norm[0])->elemType();
                            const auto &rgtElemType = tt::as_ptr<ArrayType>(norm[1])->elemType();
                            if (!lftElemType->equals(rgtElemType))
                                return std::nullopt;
                            return tt::as_ptr<ArrayType>(norm[0]);
                        }),
                },
            }),
    };
    return groups;
}
} // namespace

AlgoModule::AlgoModule(context_ptr_t ctx) : BuiltinModule("algo", ctx) {
    for (const auto &group : getOperatorGroups())
        exportEntity(group->name(), group);
}

module_ptr_t AlgoModule::create(context_ptr_t ctx) { return std::make_shared<AlgoModule>(ctx); }

bool AlgoModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("algo", [ctx = context_]() {
        return createAlgoExecutor(ctx);
    });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new AlgoModule(ctx->shared_from_this()); }
}
