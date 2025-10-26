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
 * Updated: Oct. 26, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "globals.h"
#include "builtin/executors/builtin.h"
#include "builtin/types/tensor.h"
#include "core/context/context.h"

#include "utils/assert.h"
#include "utils/log.h"

using namespace std;

module_ptr_t GlobalsBuiltinModule::create(context_ptr_t ctx) {
    return std::make_shared<GlobalsBuiltinModule>(ctx);
}

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
                            if (!norm[0]->equals(norm[1])) {
                                return nullopt;
                            }
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__assn_sub__",
            {
                {
                    ":op/assn_sub_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/assn_sub_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/assn_sub_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float(), true}, {Type::Float(), false}},
                        Type::Float()),
                },
                {
                    ":op/assn_sub_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Double(), true}, {Type::Double(), false}},
                        Type::Double()),
                },
                {
                    ":op/assn_sub",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
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
                            if (!norm[0]->equals(norm[1])) {
                                return nullopt;
                            }
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__assn_pow__",
            {
                {
                    ":op/assn_pow",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
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
                    ":op/assn_and",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), true}, {Type::Bool(), false}},
                        Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__assn_or__",
            {
                {
                    ":op/assn_or",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), true}, {Type::Bool(), false}},
                        Type::Bool()),
                },
            }),
        // ======= 逻辑运算符 =======
        OperatorGroup::create(
            "__and__",
            {
                {
                    ":op/and",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), false}, {Type::Bool(), false}},
                        Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__or__",
            {
                {
                    ":op/or",
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
                            if (!norm[0]->equals(norm[1]))
                                return nullopt;
                            return norm[0];
                        }),
                },
                {
                    ":tensor/multiply",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(self: Tensor, other: Tensor) => Tensor",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() == TensorType::typeCode() &&
                                norm[1]->code() == TensorType::typeCode()) {
                                return norm[0];
                            }
                            if (norm[0]->code() == TensorType::typeCode() &&
                                (norm[1]->code() == TypeCode::Int32 ||
                                 norm[1]->code() == TypeCode::Int64 ||
                                 norm[1]->code() == TypeCode::Float ||
                                 norm[1]->code() == TypeCode::Double)) {
                                return norm[0];
                            }
                            if ((norm[0]->code() == TypeCode::Int32 ||
                                 norm[0]->code() == TypeCode::Int64 ||
                                 norm[0]->code() == TypeCode::Float ||
                                 norm[0]->code() == TypeCode::Double) &&
                                norm[1]->code() == TensorType::typeCode()) {
                                return norm[1];
                            }
                            return nullopt;
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
                            if (!norm[0]->equals(norm[1]))
                                return nullopt;
                            return norm[0];
                        }),
                },
            }),
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
                            if (!norm[0]->equals(norm[1]))
                                return nullopt;
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create("__mat__", {}), // 暂不支持矩阵乘
        OperatorGroup::create(
            "__idx__",
            {
                {
                    ":op/idx_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(arr: T[], index: int32) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array ||
                                norm[1]->code() != TypeCode::Int32)
                                return nullopt;
                            return tt::as_shared<ArrayType>(norm[0])->elementType();
                        }),
                },
                {
                    ":op/idx_str",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(str: string, index: int32) => string",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::String ||
                                norm[1]->code() != TypeCode::Int32)
                                return nullopt;
                            return Type::String();
                        }),
                },
            }),
        // ======= 一元运算符 =======
        OperatorGroup::create(
            "__not__",
            {
                {
                    ":op/not",
                    StaticFuncTypeResolver::create({}, {{Type::Bool(), false}}, Type::Bool()),
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
                            -> optional<type_ptr_t> { return norm[0]; }),
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
                            -> optional<type_ptr_t> { return norm[0]; }),
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
                        {{-1, {}}, {1, {false}}},
                        "<...args: any> (fmt: string) => string",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (!norm[0]->equals(Type::String()))
                                return nullopt;
                            return Type::String();
                        }),
                },
            }),
        OperatorGroup::create(
            "join",
            {
                {
                    ":str/join",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<sep: string> (vec: string[]) => string",
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
                        "<> (arr: T[]) => int32",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
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
                        "(...args: (typeas T)[]) => (T, T, ...)[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm.size() < 2)
                                return nullopt;
                            for (const auto &t : norm) {
                                if (t->code() != TypeCode::Array)
                                    return nullopt;
                            }
                            type_vec_t tupleTypes;
                            for (size_t i = 0; i < norm.size(); i++) {
                                const auto &elemType =
                                    tt::as_shared<ArrayType>(norm[i])->elementType();
                                tupleTypes.push_back(elemType);
                            }
                            return Type::Array(Type::Tuple(tupleTypes));
                        }),
                },
            }),
        OperatorGroup::create(
            "head",
            {
                {
                    ":struct/head_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (arr: T[]) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(norm[0]);
                            return arrType->elementType();
                        }),
                },
            }),
        OperatorGroup::create(
            "tail",
            {
                {
                    ":struct/tail_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (arr: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(norm[0]);
                            return Type::Array(arrType->elementType());
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
                        "(start: int32, end: int32) => int32[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Int32)
                                return nullopt;
                            if (norm[1]->code() != TypeCode::Int32)
                                return nullopt;
                            return Type::Array(Type::Int32());
                        }),
                },
            }),
        OperatorGroup::create(
            "slice",
            {
                {
                    ":struct/slice_arr",
                    DynamicFuncTypeResolver::create(
                        {{2, {false, false}}, {1, {false}}},
                        "<start: int32, end: int32> (arr: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            if (with[0]->code() != TypeCode::Int32)
                                return nullopt;
                            if (with[1]->code() != TypeCode::Int32)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(norm[0]);
                            return arrType;
                        }),
                },
            }),
        OperatorGroup::create(
            "concat",
            {
                {
                    ":struct/concat_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "<> (arr: T[], other: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            if (norm[1]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(norm[0]);
                            const auto &otherArrType = tt::as_shared<ArrayType>(norm[1]);
                            if (!arrType->elementType()->equals(otherArrType->elementType()))
                                return nullopt;
                            return Type::Array(arrType->elementType());
                        }),
                },
            }),
        OperatorGroup::create(
            "append",
            {
                {
                    ":struct/append_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {true}}},
                        "<value: T> (var arr: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(norm[0]);
                            const auto &eleType = arrType->elementType();
                            if (eleType != Type::Void() && !eleType->equals(with[0]))
                                return nullopt;
                            return arrType;
                        }),
                },
            }),
        OperatorGroup::create(
            "extend",
            {
                {
                    ":struct/extend_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {true}}},
                        "<other: T[]> (var arr: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            if (with[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(norm[0]);
                            const auto &otherArrType = tt::as_shared<ArrayType>(with[0]);
                            if (!arrType->equals(otherArrType))
                                return nullopt;
                            return arrType;
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
                        {{1, {false}}, {1, {false}}},
                        "<value: T> (arr: T[]) => bool",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(norm[0]);
                            if (!arrType->elementType()->equals(with[0]))
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
                    ":mark/map_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<f: (item: T) => U> (collect: T[]) => U[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(norm[0]);
                            const auto &elemType = arrType->elementType();
                            if (with[0]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &funcType = tt::as_shared<FunctionType>(with[0]);
                            if (funcType->normTypes().size() != 1)
                                return nullopt;
                            const auto &normTypes = funcType->normTypes();
                            if (!normTypes[0].first->equals(elemType))
                                return nullopt;
                            ASSERT(funcType->hasExitType(), "Function must have exit type");
                            return Type::Array(funcType->exitType());
                        }),
                },
            }),
        OperatorGroup::create(
            "apply",
            {
                {
                    ":mark/apply_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {true}}},
                        "<f: (item: T) => T> (var collect: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(norm[0]);
                            const auto &elemType = arrType->elementType();
                            if (with[0]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &funcType = tt::as_shared<FunctionType>(with[0]);
                            if (funcType->normTypes().size() != 1)
                                return nullopt;
                            const auto &normTypes = funcType->normTypes();
                            if (!normTypes[0].first->equals(elemType))
                                return nullopt;
                            if (!funcType->exitType()->equals(elemType))
                                return nullopt;
                            return arrType;
                        }),
                },
            }),
        OperatorGroup::create(
            "filter",
            {
                {
                    ":mark/filter_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<f: (item: T) => bool> (collect: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(norm[0]);
                            if (with[1]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &funcType = tt::as_shared<FunctionType>(with[1]);
                            if (!funcType->assignable(Type::Func()))
                                return nullopt;
                            if (!funcType->exitType()->equals(Type::Bool()))
                                return nullopt;
                            return arrType;
                        }),
                },
            }),
        OperatorGroup::create(
            "reduce",
            {
                {
                    ":mark/reduce_arr",
                    DynamicFuncTypeResolver::create(
                        {{2, {false, false}}, {1, {false}}},
                        "<f: (acc: U, item: T) => U, initial: U> (collect: T[]) => U",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(norm[0]);
                            if (with[0]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &funcType = tt::as_shared<FunctionType>(with[0]);
                            const auto &normTypes = funcType->normTypes();
                            if (!normTypes[1].first->equals(arrType->elementType()))
                                return nullopt;
                            if (!normTypes[0].first->equals(with[1]))
                                return nullopt;
                            if (!funcType->exitType()->equals(with[1]))
                                return nullopt;
                            return with[1];
                        }),
                },
            }),
        OperatorGroup::create(
            "foreach",
            {
                {
                    ":mark/foreach_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<f: (item: T) => void> (collect: T[]) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_shared<ArrayType>(norm[0]);
                            if (with[0]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &funcType = tt::as_shared<FunctionType>(with[0]);
                            if (funcType->normTypes().size() != 1)
                                return nullopt;
                            const auto &normTypes = funcType->normTypes();
                            if (!normTypes[0].first->equals(arrType->elementType()))
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
                            if (norm[0]->code() != TypeCode::Function)
                                return nullopt;
                            if (norm[1]->code() != TypeCode::Function)
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
        OperatorGroup::create(
            "Tensor",
            {
                {
                    ":type/tensor",
                    StaticFuncTypeResolver::create({}, {}, TensorType::create(Type::Double(), {0})),
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
