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
 * Author: Zhenjie Wei
 * Created: Jul. 29, 2025
 * Updated: Oct. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "algo.h"

using namespace std;

static const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "sort",
            {
                {
                    ":algo/sort",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (array: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &vecType = tt::as_shared<ArrayType>(norm[0]);
                            return vecType;
                        }),
                },
            }),

        OperatorGroup::create(
            "sort_i",
            {
                {
                    ":algo/sort_i",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {true}}},
                        "<> (var array: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &vecType = tt::as_shared<ArrayType>(norm[0]);
                            return vecType;
                        }),
                },
            }),

        OperatorGroup::create(
            "insert_sort",
            {
                {
                    ":algo/insert_sort",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (array: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &vecType = tt::as_shared<ArrayType>(norm[0]);
                            return vecType;
                        }),
                },
            }),

        OperatorGroup::create(
            "insert_sort_i",
            {
                {
                    ":algo/insert_sort_i",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {true}}},
                        "<> (var array: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &vecType = tt::as_shared<ArrayType>(norm[0]);
                            return vecType;
                        }),
                },
            }),

        OperatorGroup::create(
            "quick_sort",
            {
                {
                    ":algo/quick_sort",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (array: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &vecType = tt::as_shared<ArrayType>(norm[0]);
                            return vecType;
                        }),
                },
            }),
        OperatorGroup::create(
            "quick_sort_i",
            {
                {
                    ":algo/quick_sort_i",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {true}}},
                        "<> (var array: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &vecType = tt::as_shared<ArrayType>(norm[0]);
                            return vecType;
                        }),
                },
            }),

        OperatorGroup::create(
            "merge_sort",
            {
                {
                    ":algo/merge_sort",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (array: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &vecType = tt::as_shared<ArrayType>(norm[0]);
                            return vecType;
                        }),
                },
            }),

        OperatorGroup::create(
            "merge_sort_i",
            {
                {
                    ":algo/merge_sort_i",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {true}}},
                        "<> (var array: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &vecType = tt::as_shared<ArrayType>(norm[0]);
                            return vecType;
                        }),
                },
            }),

        OperatorGroup::create(
            "merge_and_sort",
            {
                {
                    ":algo/merge_and_sort",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "<> (lft: T[], rgt: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            if (norm[1]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &lftElemType =
                                tt::as_shared<ArrayType>(norm[0])->elementType();
                            const auto &rgtElemType =
                                tt::as_shared<ArrayType>(norm[1])->elementType();
                            if (!lftElemType->equals(rgtElemType))
                                return nullopt;
                            const auto &vecType = tt::as_shared<ArrayType>(norm[0]);
                            return vecType;
                        }),
                },
            }),
    };

    return groups;
}

AlgoBuiltinModule::AlgoBuiltinModule(context_ptr_t ctx) : BuiltinModule("algo", ctx) {
    for (const auto &group : getOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

bool AlgoBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    return true;
}

module_ptr_t AlgoBuiltinModule::create(context_ptr_t ctx) {
    return std::make_shared<AlgoBuiltinModule>(ctx);
}
