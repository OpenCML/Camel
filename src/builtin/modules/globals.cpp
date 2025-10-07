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
 * Updated: Oct. 07, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "globals.h"
#include "builtin/executors/builtin.h"
#include "core/context/context.h"

#include "utils/log.h"

using namespace std;

const std::vector<oper_group_ptr_t> &getGlobalOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        // ======= 赋值运算符 =======
        OperatorGroup::create(
            "__assn__",
            {
                {
                    ":op/assn_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/assn_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/assn_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), true}, {Type::Float(), false}},
                        Type::Float()),
                },
                {
                    ":op/assn_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), true}, {Type::Double(), false}},
                        Type::Double()),
                },
                {
                    ":op/assn_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), true}, {Type::String(), false}},
                        Type::String()),
                },
                {
                    ":op/assn_b",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), true}, {Type::Bool(), false}},
                        Type::Bool()),
                },
                {
                    ":op/assn",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2) {
                                return nullopt;
                            }
                            if (!norm[0]->equals(norm[1])) {
                                return nullopt;
                            }
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__assn_add__",
            {
                {
                    ":op/assn_add_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/assn_add_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/assn_add_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), true}, {Type::Float(), false}},
                        Type::Float()),
                },
                {
                    ":op/assn_add_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), true}, {Type::Double(), false}},
                        Type::Double()),
                },
                {
                    ":op/assn_add_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), true}, {Type::String(), false}},
                        Type::String()),
                },
                {
                    ":op/assn_add",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2) {
                                return nullopt;
                            }
                            if (!norm[0]->equals(norm[1])) {
                                return nullopt;
                            }
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__assn_mul__",
            {
                {
                    ":op/assn_mul_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/assn_mul_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/assn_mul_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), true}, {Type::Float(), false}},
                        Type::Float()),
                },
                {
                    ":op/assn_mul_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), true}, {Type::Double(), false}},
                        Type::Double()),
                },
                {
                    ":op/assn_mul",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2) {
                                return nullopt;
                            }
                            if (!norm[0]->equals(norm[1])) {
                                return nullopt;
                            }
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__assn_div__",
            {
                {
                    ":op/assn_div_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/assn_div_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/assn_div_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), true}, {Type::Float(), false}},
                        Type::Float()),
                },
                {
                    ":op/assn_div_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), true}, {Type::Double(), false}},
                        Type::Double()),
                },
                {
                    ":op/assn_div",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2) {
                                return nullopt;
                            }
                            if (!norm[0]->equals(norm[1])) {
                                return nullopt;
                            }
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__assn_mod__",
            {
                {
                    ":op/assn_mod_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/assn_mod_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/assn_mod",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2) {
                                return nullopt;
                            }
                            if (!norm[0]->equals(norm[1])) {
                                return nullopt;
                            }
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__assn_mat__",
            {
                {
                    ":op/assn_mat",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2) {
                                return nullopt;
                            }
                            if (!norm[0]->equals(norm[1])) {
                                return nullopt;
                            }
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__assn_exp__",
            {
                {
                    ":op/assn_exp",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2) {
                                return nullopt;
                            }
                            if (!norm[0]->equals(norm[1])) {
                                return nullopt;
                            }
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__assn_and__",
            {
                {
                    ":op/assn_and_b",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), true}, {Type::Bool(), false}},
                        Type::Bool()),
                },
                {
                    ":op/assn_and",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2) {
                                return nullopt;
                            }
                            if (!norm[0]->equals(norm[1])) {
                                return nullopt;
                            }
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__assn_or__",
            {
                {
                    ":op/assn_or_b",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), true}, {Type::Bool(), false}},
                        Type::Bool()),
                },
                {
                    ":op/assn_or",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2) {
                                return nullopt;
                            }
                            if (!norm[0]->equals(norm[1])) {
                                return nullopt;
                            }
                            return norm[0];
                        }),
                },
            }),
        // ======= 逻辑运算符 =======
        OperatorGroup::create(
            "__or__",
            {
                {
                    ":op/or_b",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), false}, {Type::Bool(), false}},
                        Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__and__",
            {
                {
                    ":op/and_b",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), false}, {Type::Bool(), false}},
                        Type::Bool()),
                },
            }),
        // ======= 比较运算符 =======
        OperatorGroup::create(
            "__eq__",
            {
                {
                    ":op/eq_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), false}, {Type::Float(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), false}, {Type::Double(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_b",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), false}, {Type::Bool(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(self: typeas T, other: T) => bool",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2) {
                                return nullopt;
                            }
                            if (!norm[0]->equals(norm[1])) {
                                return nullopt;
                            }
                            return Type::Bool();
                        }),
                },
            }),
        OperatorGroup::create(
            "__neq__",
            {
                {
                    ":op/neq_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/neq_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/neq_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), false}, {Type::Float(), false}},
                        Type::Bool()),
                },
                {
                    ":op/neq_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), false}, {Type::Double(), false}},
                        Type::Bool()),
                },
                {
                    ":op/neq_b",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), false}, {Type::Bool(), false}},
                        Type::Bool()),
                },
                {
                    ":op/neq_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
                {
                    ":op/neq",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(self: typeas T, other: T) => bool",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2) {
                                return nullopt;
                            }
                            if (!norm[0]->equals(norm[1])) {
                                return nullopt;
                            }
                            return Type::Bool();
                        }),
                },
            }),
        OperatorGroup::create(
            "__strict_eq__",
            {
                {
                    ":op/strict_eq",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}, {Type::Any(), false}},
                        Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__strict_neq__",
            {
                {
                    ":op/strict_neq",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}, {Type::Any(), false}},
                        Type::Bool()),
                },
            }),
        // <、<=、>、>=
        OperatorGroup::create(
            "__lt__",
            {
                {
                    ":op/lt_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/lt_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/lt_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), false}, {Type::Float(), false}},
                        Type::Bool()),
                },
                {
                    ":op/lt_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), false}, {Type::Double(), false}},
                        Type::Bool()),
                },
                {
                    ":op/lt_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
                {
                    ":op/lt",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}, {Type::Any(), false}},
                        Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__le__",
            {
                {
                    ":op/le_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/le_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/le_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), false}, {Type::Float(), false}},
                        Type::Bool()),
                },
                {
                    ":op/le_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), false}, {Type::Double(), false}},
                        Type::Bool()),
                },
                {
                    ":op/le_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
                {
                    ":op/le",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}, {Type::Any(), false}},
                        Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__gt__",
            {
                {
                    ":op/gt_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/gt_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/gt_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), false}, {Type::Float(), false}},
                        Type::Bool()),
                },
                {
                    ":op/gt_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), false}, {Type::Double(), false}},
                        Type::Bool()),
                },
                {
                    ":op/gt_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
                {
                    ":op/gt",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}, {Type::Any(), false}},
                        Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__ge__",
            {
                {
                    ":op/ge_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ge_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ge_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), false}, {Type::Float(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ge_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), false}, {Type::Double(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ge_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ge",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}, {Type::Any(), false}},
                        Type::Bool()),
                },
            }),
        // ======= 算术运算符 =======
        OperatorGroup::create(
            "__add__",
            {
                {
                    ":op/add_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/add_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/add_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), false}, {Type::Float(), false}},
                        Type::Float()),
                },
                {
                    ":op/add_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), false}, {Type::Double(), false}},
                        Type::Double()),
                },
                {
                    ":op/add_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::String()),
                },
                {
                    ":op/add",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2)
                                return nullopt;
                            if (!norm[0]->assignable(norm[1]))
                                return nullopt;
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__sub__",
            {
                {
                    ":op/sub_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/sub_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/sub_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), false}, {Type::Float(), false}},
                        Type::Float()),
                },
                {
                    ":op/sub_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), false}, {Type::Double(), false}},
                        Type::Double()),
                },
                {
                    ":op/sub",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2)
                                return nullopt;
                            if (!norm[0]->equals(norm[1]))
                                return nullopt;
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__mul__",
            {
                {
                    ":op/mul_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/mul_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/mul_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), false}, {Type::Float(), false}},
                        Type::Float()),
                },
                {
                    ":op/mul_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), false}, {Type::Double(), false}},
                        Type::Double()),
                },
                {
                    ":op/mul",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2)
                                return nullopt;
                            if (!norm[0]->equals(norm[1]))
                                return nullopt;
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__div__",
            {
                {
                    ":op/div_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Float()),
                },
                {
                    ":op/div_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Float()),
                },
                {
                    ":op/div_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), false}, {Type::Float(), false}},
                        Type::Float()),
                },
                {
                    ":op/div_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), false}, {Type::Double(), false}},
                        Type::Double()),
                },
                {
                    ":op/div",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2)
                                return nullopt;
                            if (!norm[0]->equals(norm[1]))
                                return nullopt;
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__mod__",
            {
                {
                    ":op/mod_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/mod_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/mod",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2)
                                return nullopt;
                            if (!norm[0]->equals(norm[1]))
                                return nullopt;
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create("__mat__", {}), // 暂不支持矩阵乘
        OperatorGroup::create(
            "__pow__",
            {
                {
                    ":op/pow_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/pow_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/pow_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), false}, {Type::Float(), false}},
                        Type::Float()),
                },
                {
                    ":op/pow_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), false}, {Type::Double(), false}},
                        Type::Double()),
                },
                {
                    ":op/pow",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2)
                                return nullopt;
                            if (!norm[0]->equals(norm[1]))
                                return nullopt;
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__idx__",
            {
                {
                    ":op/idx",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(collect: typeas T, index: int32) => ValOf<T>",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with.size() != 0 || norm.size() != 2)
                                return nullopt;
                            const auto &collect = norm[0];
                            if (!norm[0]->structured())
                                return nullopt;
                            switch (norm[0]->code()) {
                            case TypeCode::Array: {
                                if (norm[1]->code() != TypeCode::Int32)
                                    return nullopt;
                                return tt::as_shared<ArrayType>(norm[0])->elementType();
                            }
                            case TypeCode::Vector: {
                                if (norm[1]->code() != TypeCode::Int32)
                                    return nullopt;
                                return tt::as_shared<VectorType>(norm[0])->elementType();
                            }
                            case TypeCode::Map: {
                                const auto &mapType = tt::as_shared<MapType>(norm[0]);
                                if (!mapType->keyType()->equals(norm[1]))
                                    return nullopt;
                                return mapType->valueType();
                            }
                            case TypeCode::Set: {
                                const auto &setType = tt::as_shared<SetType>(norm[0]);
                                if (!setType->valueType()->equals(norm[1]))
                                    return nullopt;
                                return setType->valueType();
                            }
                            case TypeCode::String: {
                                if (norm[1]->code() != TypeCode::Int32)
                                    return nullopt;
                                return Type::String();
                            }
                            default:
                                break;
                            }
                            return nullopt;
                        }),
                },
            }),
        // ======= 一元运算符 =======
        OperatorGroup::create(
            "__not__",
            {
                {
                    ":op/not_b",
                    StaticFuncTypeResolver::create({}, {{Type::Bool(), false}}, Type::Bool()),
                },
                {
                    ":op/not",
                    StaticFuncTypeResolver::create({}, {{Type::Any(), false}}, Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__neg__",
            {
                {
                    ":op/neg_i",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Int32()),
                },
                {
                    ":op/neg_l",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Int64()),
                },
                {
                    ":op/neg_f",
                    StaticFuncTypeResolver::create({}, {{Type::Float(), false}}, Type::Float()),
                },
                {
                    ":op/neg_d",
                    StaticFuncTypeResolver::create({}, {{Type::Double(), false}}, Type::Double()),
                },
                {
                    ":op/neg",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(self: typeas T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm.size() != 1)
                                return nullopt;
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__inv__",
            {
                {
                    ":op/inv_i",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Int32()),
                },
                {
                    ":op/inv_l",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Int64()),
                },
                {
                    ":op/inv",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(self: typeas T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm.size() != 1)
                                return nullopt;
                            return norm[0];
                        }),
                },
            }),
        // ======= IO =======
        OperatorGroup::create(
            "input",
            {
                {
                    ":io/input",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(prompt: string) => string",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm.size() != 1)
                                return nullopt;
                            if (!norm[0]->equals(Type::String()))
                                return nullopt;
                            return Type::String();
                        }),
                },
            }),
        OperatorGroup::create(
            "print",
            {
                {
                    ":io/print",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(...args: any) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> { return Type::Void(); }),
                },
            }),
        OperatorGroup::create(
            "println",
            {
                {
                    ":io/println",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(...args: any) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> { return Type::Void(); }),
                },
            }),
        // ======= OS =======
        OperatorGroup::create(
            "sleep",
            {
                {
                    ":os/sleep",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Void()),
                },
            }),
        OperatorGroup::create(
            "exit",
            {
                {
                    ":os/exit",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Void()),
                },
            }),
        // ======= string =======
        OperatorGroup::create(
            "format",
            {
                {
                    ":str/format",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {-1, {}}},
                        "<fmt: string> (...args: any) => string",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (!with[0]->equals(Type::String()))
                                return nullopt;
                            return Type::String();
                        }),
                },
            }),
        OperatorGroup::create(
            "join",
            {
                {
                    ":str/join_vec",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {-1, {}}},
                        "<sep: string> (vec: Vector<string>) => string",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (!with[0]->equals(Type::String()))
                                return nullopt;
                            const auto &type = norm[0];
                            if (type->code() != TypeCode::Vector) {
                                return nullopt;
                            }
                            const auto &vecType = tt::as_shared<VectorType>(type);
                            if (vecType->elementType()->code() != TypeCode::String) {
                                return nullopt;
                            }
                            return Type::String();
                        }),
                },
                {
                    ":str/join_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {-1, {}}},
                        "<sep: string> (vec: Array<string, n>) => string",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (!with[0]->equals(Type::String()))
                                return nullopt;
                            const auto &type = norm[0];
                            if (type->code() != TypeCode::Array) {
                                return nullopt;
                            }
                            const auto &arrType = tt::as_shared<ArrayType>(type);
                            if (arrType->elementType()->code() != TypeCode::String) {
                                return nullopt;
                            }
                            return Type::String();
                        }),
                },
                {
                    ":str/join",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {-1, {}}},
                        "<sep: string> (...args: string) => string",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (!with[0]->equals(Type::String()))
                                return nullopt;
                            for (const auto &t : norm) {
                                if (!t->equals(Type::String())) {
                                    return nullopt;
                                }
                            }
                            return Type::String();
                        }),
                },
            }),
        // ======= struct =======
        OperatorGroup::create(
            "len",
            {
                {
                    ":struct/len_str",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Int32()),
                },
                {
                    ":struct/len_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(arr: Array<T, n>) => int32",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            return Type::Int32();
                        }),
                },
                {
                    ":struct/len_vec",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(vec: Vector<T>) => int32",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Vector)
                                return nullopt;
                            return Type::Int32();
                        }),
                },
                {
                    ":struct/len_map",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(map: Map<K, V>) => int32",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Map)
                                return nullopt;
                            return Type::Int32();
                        }),
                },
                {
                    ":struct/len_set",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(set: Set<V>) => int32",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Set)
                                return nullopt;
                            return Type::Int32();
                        }),
                },
            }),
        OperatorGroup::create(
            "zip",
            {
                {
                    ":struct/zip",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(...args: Vector<typeas T>) => Vector<Tuple<T, T, ...>>",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm.size() < 2)
                                return nullopt;
                            for (const auto &t : norm) {
                                if (t->code() != TypeCode::Vector)
                                    return nullopt;
                            }
                            type_vec_t tupleTypes;
                            for (size_t i = 0; i < norm.size(); i++) {
                                const auto &elemType =
                                    tt::as_shared<VectorType>(norm[i])->elementType();
                                tupleTypes.push_back(elemType);
                            }
                            return Type::Vector(Type::Tuple(tupleTypes));
                        }),
                },
            }),
        OperatorGroup::create(
            "head",
            {
                {
                    ":struct/head_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {}}, {0, {}}},
                        "<arr: Array<T, n>> () => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(with[0]);
                            return arrType->elementType();
                        }),
                },
                {
                    ":struct/head_vec",
                    DynamicFuncTypeResolver::create(
                        {{1, {}}, {0, {}}},
                        "<vec: Vector<T>> () => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Vector)
                                return nullopt;
                            const auto &vecType = tt::as_shared<VectorType>(with[0]);
                            return vecType->elementType();
                        }),
                },
            }),
        OperatorGroup::create(
            "tail",
            {
                {
                    ":struct/tail_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {}}, {0, {}}},
                        "<arr: Array<T, n>> () => Array<T, m>",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(with[0]);
                            return Type::Array(arrType->elementType(), arrType->size() - 1);
                        }),
                },
                {
                    ":struct/tail_vec",
                    DynamicFuncTypeResolver::create(
                        {{1, {}}, {0, {}}},
                        "<vec: Vector<T>> () => Vector<T>",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Vector)
                                return nullopt;
                            const auto &vecType = tt::as_shared<VectorType>(with[0]);
                            return Type::Vector(vecType->elementType());
                        }),
                },
            }),
        OperatorGroup::create(
            "range",
            {
                {
                    ":struct/range",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(start: int32, end: int32) => Vector<int32, n>",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Int32)
                                return nullopt;
                            if (norm[1]->code() != TypeCode::Int32)
                                return nullopt;
                            return Type::Vector(Type::Int32());
                        }),
                },
            }),
        OperatorGroup::create(
            "slice",
            {
                {
                    ":struct/slice_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {}}, {2, {false, false}}},
                        "<arr: Array<T, n>> (start: int32, end: int32) => Array<T, m>",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Array)
                                return nullopt;
                            if (norm[0]->code() != TypeCode::Int32)
                                return nullopt;
                            if (norm[1]->code() != TypeCode::Int32)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(with[0]);
                            return Type::Array(arrType->elementType(), -1);
                        }),
                },
                {
                    ":struct/slice_vec",
                    DynamicFuncTypeResolver::create(
                        {{1, {}}, {2, {false, false}}},
                        "<vec: Vector<T>> (start: int32, end: int32) => Vector<T>",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Vector)
                                return nullopt;
                            if (norm[0]->code() != TypeCode::Int32)
                                return nullopt;
                            if (norm[1]->code() != TypeCode::Int32)
                                return nullopt;
                            const auto &vecType = tt::as_shared<VectorType>(with[0]);
                            return Type::Vector(vecType->elementType());
                        }),
                },
            }),
        OperatorGroup::create(
            "concat",
            {
                {
                    ":struct/concat_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<arr: Array<T, n>> (other: Array<T, m>) => Array<T, m + n>",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Array)
                                return nullopt;
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(with[0]);
                            const auto &otherArrType = tt::as_shared<ArrayType>(norm[0]);
                            if (!arrType->elementType()->equals(otherArrType->elementType()))
                                return nullopt;
                            return Type::Array(
                                arrType->elementType(),
                                arrType->size() + otherArrType->size());
                        }),
                },
                {
                    ":struct/concat_vec",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<vec: Vector<T>> (other: Vector<T>) => Vector<T>",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Vector)
                                return nullopt;
                            if (norm[0]->code() != TypeCode::Vector)
                                return nullopt;
                            const auto &vecType = tt::as_shared<VectorType>(with[0]);
                            const auto &otherVecType = tt::as_shared<VectorType>(norm[0]);
                            if (!vecType->elementType()->equals(otherVecType->elementType()))
                                return nullopt;
                            return Type::Vector(vecType->elementType());
                        }),
                },
            }),
        OperatorGroup::create(
            "append",
            {
                {
                    ":struct/append_vec",
                    DynamicFuncTypeResolver::create(
                        {{1, {true}}, {1, {false}}},
                        "<vec: Vector<T>> (value: T) => Vector<T>",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Vector)
                                return nullopt;
                            const auto &vecType = tt::as_shared<VectorType>(with[0]);
                            if (!vecType->elementType()->equals(norm[0]))
                                return nullopt;
                            return Type::Vector(vecType->elementType());
                        }),
                },
            }),
        OperatorGroup::create(
            "extend",
            {
                {
                    ":struct/extend_vec",
                    DynamicFuncTypeResolver::create(
                        {{1, {true}}, {1, {false}}},
                        "<vec: Vector<T>> (other: Vector<T>) => Vector<T>",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Vector)
                                return nullopt;
                            if (norm[0]->code() != TypeCode::Vector)
                                return nullopt;
                            const auto &vecType = tt::as_shared<VectorType>(with[0]);
                            const auto &otherVecType = tt::as_shared<VectorType>(norm[0]);
                            if (!vecType->elementType()->equals(otherVecType->elementType()))
                                return nullopt;
                            return Type::Vector(vecType->elementType());
                        }),
                },
            }),
        OperatorGroup::create(
            "contains",
            {
                {
                    ":struct/contains_str",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
                {
                    ":struct/contains_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(arr: Array<T, n>, value: T) => bool",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(with[0]);
                            if (!arrType->elementType()->equals(norm[1]))
                                return nullopt;
                            return Type::Bool();
                        }),
                },
                {
                    ":struct/contains_vec",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(vec: Vector<T>, value: T) => bool",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm.size() != 2)
                                return nullopt;
                            if (with[0]->code() != TypeCode::Vector)
                                return nullopt;
                            const auto &vecType = tt::as_shared<VectorType>(with[0]);
                            if (!vecType->elementType()->equals(norm[1]))
                                return nullopt;
                            return Type::Bool();
                        }),
                },
            }),
        // ======= Marked Operators =======
        OperatorGroup::create(
            "map",
            {
                {
                    ":mark/map_vec",
                    DynamicFuncTypeResolver::create(
                        {{1, {}}, {1, {false}}},
                        "<collect: Vector<T>> (func: (item: T) => U) => Vector<U>",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Vector)
                                return nullopt;
                            const auto &vecType = tt::as_shared<VectorType>(with[0]);
                            if (norm[1]->code() != TypeCode::Func)
                                return nullopt;
                            const auto &funcType = tt::as_shared<FunctionType>(norm[1]);
                            if (!funcType->assignable(Type::AnyFunc()))
                                return nullopt;
                            return Type::Vector(funcType->exitType());
                        }),
                },
            }),
        OperatorGroup::create(
            "apply",
            {
                {
                    ":mark/apply_vec",
                    DynamicFuncTypeResolver::create(
                        {{1, {}}, {1, {false}}},
                        "<collect: Vector<T>> (func: (item: T) => void) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Vector)
                                return nullopt;
                            const auto &vecType = tt::as_shared<VectorType>(with[0]);
                            if (norm[1]->code() != TypeCode::Func)
                                return nullopt;
                            const auto &funcType = tt::as_shared<FunctionType>(norm[1]);
                            if (!funcType->assignable(Type::AnyFunc()))
                                return nullopt;
                            if (!funcType->exitType()->equals(Type::Void()))
                                return nullopt;
                            return Type::Void();
                        }),
                },
            }),
        OperatorGroup::create(
            "filter",
            {
                {
                    ":mark/filter_vec",
                    DynamicFuncTypeResolver::create(
                        {{1, {}}, {1, {false}}},
                        "<collect: Vector<T>> (func: (item: T) => bool) => Vector<T>",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Vector)
                                return nullopt;
                            const auto &vecType = tt::as_shared<VectorType>(with[0]);
                            if (norm[1]->code() != TypeCode::Func)
                                return nullopt;
                            const auto &funcType = tt::as_shared<FunctionType>(norm[1]);
                            if (!funcType->assignable(Type::AnyFunc()))
                                return nullopt;
                            if (!funcType->exitType()->equals(Type::Bool()))
                                return nullopt;
                            return Type::Vector(vecType->elementType());
                        }),
                },
            }),
        OperatorGroup::create(
            "reduce",
            {
                {
                    ":mark/reduce_vec",
                    DynamicFuncTypeResolver::create(
                        {{1, {}}, {2, {false, false}}},
                        "<collect: Vector<T>> (func: (acc: U, item: T) => U, initial: U) => U",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Vector)
                                return nullopt;
                            const auto &vecType = tt::as_shared<VectorType>(with[0]);
                            if (norm[0]->code() != TypeCode::Func)
                                return nullopt;
                            const auto &funcType = tt::as_shared<FunctionType>(norm[0]);
                            const auto &withTypes = funcType->withTypes();
                            if (withTypes.size() != 2)
                                return nullopt;
                            if (!withTypes[1].first->equals(vecType->elementType()))
                                return nullopt;
                            if (!withTypes[0].first->equals(norm[1]))
                                return nullopt;
                            if (!funcType->exitType()->equals(norm[1]))
                                return nullopt;
                            return norm[1];
                        }),
                },
            }),
        OperatorGroup::create(
            "foreach",
            {
                {
                    ":mark/foreach_vec",
                    DynamicFuncTypeResolver::create(
                        {{1, {}}, {1, {false}}},
                        "<collect: Vector<T>> (func: (item: T) => void) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (with[0]->code() != TypeCode::Vector)
                                return nullopt;
                            const auto &vecType = tt::as_shared<VectorType>(with[0]);
                            if (norm[1]->code() != TypeCode::Func)
                                return nullopt;
                            const auto &funcType = tt::as_shared<FunctionType>(norm[1]);
                            if (funcType->normTypes().size() != 1)
                                return nullopt;
                            const auto &normTypes = funcType->normTypes();
                            if (!normTypes[0].first->equals(vecType->elementType()))
                                return nullopt;
                            if (!funcType->exitType()->equals(Type::Void()))
                                return nullopt;
                            return Type::Void();
                        }),
                },
            }),
        // ======= Macros =======
        OperatorGroup::create(
            "__cmp__",
            {
                {
                    ":macro/cmp",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(lhs: (arg1: T1, arg2: T2, ...) => R, rhs: (arg: R) => U) => U",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm.size() != 2)
                                return nullopt;
                            if (norm[0]->code() != TypeCode::Func)
                                return nullopt;
                            if (norm[1]->code() != TypeCode::Func)
                                return nullopt;
                            const auto &lhsFuncType = tt::as_shared<FunctionType>(norm[0]);
                            const auto &rhsFuncType = tt::as_shared<FunctionType>(norm[1]);
                            const auto &lhsReturnType = lhsFuncType->exitType();
                            const auto &rhsNormTypes = rhsFuncType->normTypes();
                            if (rhsNormTypes.size() != 1)
                                return nullopt;
                            if (!rhsNormTypes[0].first->equals(lhsReturnType))
                                return nullopt;
                            return rhsFuncType->exitType();
                        }),
                },
            }),
    };
    return groups;
}

GlobalsBuiltinModule::GlobalsBuiltinModule(context_ptr_t ctx) : BuiltinModule("", ctx) {
    for (const auto &group : getGlobalOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

bool GlobalsBuiltinModule::load() {
    EXEC_WHEN_DEBUG(l.in("GlobalsBuiltinModule").info("Loading basic built-in module."));
    if (loaded_) {
        return true;
    }
    context_->registerExecutorFactory("", [&]() { return BasicBuiltinExecutor::create(context_); });
    loaded_ = true;
    return true;
}
