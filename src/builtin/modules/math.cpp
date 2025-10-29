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

#include "math.h"

using namespace std;

static const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "abs",
            {
                {
                    ":math/abs_i",
                    StaticFuncTypeResolver::create({}, {{Type::Int(), false}}, Type::Int()),
                },
                {
                    ":math/abs_l",
                    StaticFuncTypeResolver::create({}, {{Type::Long(), false}}, Type::Long()),
                },
                {
                    ":math/abs_f",
                    StaticFuncTypeResolver::create({}, {{Type::Float(), false}}, Type::Float()),
                },
                {
                    ":math/abs_d",
                    StaticFuncTypeResolver::create({}, {{Type::Double(), false}}, Type::Double()),
                },
            }),
        OperatorGroup::create(
            "exp",
            {
                {
                    ":math/exp_f",
                    StaticFuncTypeResolver::create({}, {{Type::Float(), false}}, Type::Float()),
                },
                {
                    ":math/exp_d",
                    StaticFuncTypeResolver::create({}, {{Type::Double(), false}}, Type::Double()),
                },
            }),
        OperatorGroup::create(
            "round",
            {
                {
                    ":math/round_f",
                    StaticFuncTypeResolver::create({}, {{Type::Float(), false}}, Type::Float()),
                },
                {
                    ":math/round_d",
                    StaticFuncTypeResolver::create({}, {{Type::Double(), false}}, Type::Double()),
                },
            }),
        OperatorGroup::create(
            "ceil",
            {
                {
                    ":math/ceil_f",
                    StaticFuncTypeResolver::create({}, {{Type::Float(), false}}, Type::Float()),
                },
                {
                    ":math/ceil_d",
                    StaticFuncTypeResolver::create({}, {{Type::Double(), false}}, Type::Double()),
                },
            }),
        OperatorGroup::create(
            "floor",
            {
                {
                    ":math/floor_f",
                    StaticFuncTypeResolver::create({}, {{Type::Float(), false}}, Type::Float()),
                },
                {
                    ":math/floor_d",
                    StaticFuncTypeResolver::create({}, {{Type::Double(), false}}, Type::Double()),
                },
            }),
        OperatorGroup::create(
            "bin",
            {
                {
                    ":math/bin_i",
                    StaticFuncTypeResolver::create({}, {{Type::Int(), false}}, Type::String()),
                },
                {
                    ":math/bin_l",
                    StaticFuncTypeResolver::create({}, {{Type::Long(), false}}, Type::String()),
                },
            }),
        OperatorGroup::create(
            "oct",
            {
                {
                    ":math/oct_i",
                    StaticFuncTypeResolver::create({}, {{Type::Int(), false}}, Type::String()),
                },
                {
                    ":math/oct_l",
                    StaticFuncTypeResolver::create({}, {{Type::Long(), false}}, Type::String()),
                },
            }),
        OperatorGroup::create(
            "hex",
            {
                {
                    ":math/hex_i",
                    StaticFuncTypeResolver::create({}, {{Type::Int(), false}}, Type::String()),
                },
                {
                    ":math/hex_l",
                    StaticFuncTypeResolver::create({}, {{Type::Long(), false}}, Type::String()),
                },
            })};

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
