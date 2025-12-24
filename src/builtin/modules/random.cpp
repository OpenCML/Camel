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

#include "random.h"

using namespace std;

static const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "seed",
            {
                {
                    ":rand/seed",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Void()),
                },
            }),
        OperatorGroup::create(
            "rand",
            {
                {
                    ":rand/rand",
                    StaticFuncTypeResolver::create({}, {}, Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "randn",
            {
                {
                    ":rand/randn",
                    StaticFuncTypeResolver::create({}, {}, Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "randint",
            {
                {
                    ":rand/randint",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
            }),
        OperatorGroup::create(
            "choice",
            {
                {
                    ":rand/choice",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (array: T[]) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &vecType = tt::as_shared<ArrayType>(norm[0]);
                            return vecType->elemType();
                        }),
                },
            }),
        OperatorGroup::create(
            "sample",
            {
                {
                    ":rand/sample",
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
            "shuffle",
            {
                {
                    ":rand/shuffle",
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
    };

    return groups;
}

RandomBuiltinModule::RandomBuiltinModule(context_ptr_t ctx) : BuiltinModule("random", ctx) {
    for (const auto &group : getOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

module_ptr_t RandomBuiltinModule::create(context_ptr_t ctx) {
    return std::make_shared<RandomBuiltinModule>(ctx);
}

bool RandomBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    return true;
}
