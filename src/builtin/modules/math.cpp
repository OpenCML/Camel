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
 * Updated: Dec. 24, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "math.h"

using namespace std;

static const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "abs",
            {
                {
                    ":math/abs_i",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Int32()),
                },
                {
                    ":math/abs_l",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Int64()),
                },
                {
                    ":math/abs_f",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Float32()),
                },
                {
                    ":math/abs_d",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "exp",
            {
                {
                    ":math/exp_f",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Float32()),
                },
                {
                    ":math/exp_d",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "round",
            {
                {
                    ":math/round_f",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Float32()),
                },
                {
                    ":math/round_d",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "ceil",
            {
                {
                    ":math/ceil_f",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Float32()),
                },
                {
                    ":math/ceil_d",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "floor",
            {
                {
                    ":math/floor_f",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Float32()),
                },
                {
                    ":math/floor_d",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "bin",
            {
                {
                    ":math/bin_i",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::String()),
                },
                {
                    ":math/bin_l",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::String()),
                },
            }),
        OperatorGroup::create(
            "oct",
            {
                {
                    ":math/oct_i",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::String()),
                },
                {
                    ":math/oct_l",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::String()),
                },
            }),
        OperatorGroup::create(
            "hex",
            {
                {
                    ":math/hex_i",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::String()),
                },
                {
                    ":math/hex_l",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::String()),
                },
            }),
        OperatorGroup::create(
            "sqrt",
            {
                {
                    ":math/sqrt",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(val: float | double) => float | double",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            TypeCode tp = norm[0]->code();
                            if (tp == TypeCode::Float32 || tp == TypeCode::Int32)
                                return Type::Float32();
                            if (tp == TypeCode::Float64 || tp == TypeCode::Int64)
                                return Type::Float64();
                            return nullopt;
                        }),
                },
            }),
    };

    return groups;
}

MathBuiltinModule::MathBuiltinModule(context_ptr_t ctx) : BuiltinModule("math", ctx) {
    for (const auto &group : getOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

bool MathBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    return true;
}

module_ptr_t MathBuiltinModule::create(context_ptr_t ctx) {
    return std::make_shared<MathBuiltinModule>(ctx);
}
