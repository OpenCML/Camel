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
 * Created: Oct. 21, 2025
 * Updated: Oct. 31, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "tensor.h"
#include "builtin/executors/builtin.h"
#include "builtin/types/tensor.h"
#include "core/context/context.h"
#include "core/type/resolver.h"

using namespace std;

static const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "ones",
            {
                {
                    ":tensor/ones",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Array(Type::Int()), false}},
                        TensorType::Default()),
                },
            }),

        OperatorGroup::create(
            "zeros",
            {
                {
                    ":tensor/zeros",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Array(Type::Int()), false}},
                        TensorType::Default()),
                },
            }),
        OperatorGroup::create(
            "eye",
            {
                {
                    ":tensor/eye",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int(), false}},
                        TensorType::Default()),
                },
            }),
        OperatorGroup::create(
            "diag",
            {
                {
                    ":tensor/diag",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}},
                        TensorType::Default()),
                },
            }),
        OperatorGroup::create(
            "linspace",
            {
                {
                    ":tensor/linspace",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}},
                        TensorType::Default()),
                },
            }),
        OperatorGroup::create(
            "arange",
            {
                {
                    ":tensor/arange",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}},
                        TensorType::Default()),
                },
            }),
        OperatorGroup::create(
            "random",
            {
                {
                    ":tensor/random",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Array(Type::Int()), false}},
                        TensorType::Default()),
                },
                {
                    ":tensor/random",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Array(Type::Int()), false},
                         {Type::Float(), false},
                         {Type::Float(), false}},
                        TensorType::Default()),
                },
            }),
        OperatorGroup::create(
            "randn",
            {
                {
                    ":tensor/randn",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Array(Type::Int()), false}},
                        TensorType::Default()),
                },
                {
                    ":tensor/randn_params",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Array(Type::Int()), false},
                         {Type::Float(), false},
                         {Type::Float(), false}},
                        TensorType::Default()),
                },
            }),
        OperatorGroup::create(
            "shape",
            {
                {
                    ":tensor/shape",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(a: Tensor) => int[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() == TensorType::typeCode()) {
                                auto int32Type = std::make_shared<PrimaryType>(TypeCode::Int);
                                return std::make_shared<ArrayType>(int32Type);
                            }
                            return nullopt;
                        }),
                },
            }),

        OperatorGroup::create(
            "add",
            {
                {
                    ":tensor/add",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(a: Tensor, b: Tensor) => Tensor",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() == TensorType::typeCode() &&
                                norm[1]->code() == TensorType::typeCode()) {
                                return norm[0];
                            }
                            if (norm[0]->code() == TensorType::typeCode() &&
                                (norm[1]->code() == TypeCode::Int ||
                                 norm[1]->code() == TypeCode::Long ||
                                 norm[1]->code() == TypeCode::Float ||
                                 norm[1]->code() == TypeCode::Double)) {
                                return norm[0];
                            }
                            if ((norm[0]->code() == TypeCode::Int ||
                                 norm[0]->code() == TypeCode::Long ||
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
            "subtract",
            {
                {
                    ":tensor/subtract",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(a: Tensor, b: Tensor) => Tensor",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() == TensorType::typeCode() &&
                                norm[1]->code() == TensorType::typeCode()) {
                                return norm[0];
                            }
                            if (norm[0]->code() == TensorType::typeCode() &&
                                (norm[1]->code() == TypeCode::Int ||
                                 norm[1]->code() == TypeCode::Long ||
                                 norm[1]->code() == TypeCode::Float ||
                                 norm[1]->code() == TypeCode::Double)) {
                                return norm[0];
                            }
                            if ((norm[0]->code() == TypeCode::Int ||
                                 norm[0]->code() == TypeCode::Long ||
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
            "multiply",
            {
                {
                    ":tensor/multiply",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(a: Tensor, b: Tensor) => Tensor",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            TypeCode lhs = norm[0]->code();
                            TypeCode rhs = norm[1]->code();
                            if (lhs == TensorType::typeCode() && rhs == TensorType::typeCode()) {
                                return norm[0];
                            }
                            if (lhs == TensorType::typeCode() &&
                                (rhs == TypeCode::Int || rhs == TypeCode::Long ||
                                 rhs == TypeCode::Float || rhs == TypeCode::Double)) {
                                return norm[0];
                            }
                            if ((lhs == TypeCode::Int || lhs == TypeCode::Long ||
                                 lhs == TypeCode::Float || lhs == TypeCode::Double) &&
                                rhs == TensorType::typeCode()) {
                                return norm[1];
                            }
                            return nullopt;
                        }),
                },
            }),

        OperatorGroup::create(
            "divide",
            {
                {
                    ":tensor/divide",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(a: Tensor, b: Tensor) => Tensor",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() == TensorType::typeCode() &&
                                norm[1]->code() == TensorType::typeCode()) {
                                return norm[0];
                            }
                            if (norm[0]->code() == TensorType::typeCode() &&
                                (norm[1]->code() == TypeCode::Int ||
                                 norm[1]->code() == TypeCode::Long ||
                                 norm[1]->code() == TypeCode::Float ||
                                 norm[1]->code() == TypeCode::Double)) {
                                return norm[0];
                            }
                            if ((norm[0]->code() == TypeCode::Int ||
                                 norm[0]->code() == TypeCode::Long ||
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
            "matmul",
            {
                {
                    ":tensor/matmul",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(a: Tensor, b: Tensor) => Tensor",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() == TensorType::typeCode() &&
                                norm[1]->code() == TensorType::typeCode()) {
                                return norm[0];
                            }
                            return nullopt;
                        }),
                },
            }),
        OperatorGroup::create(
            "pow",
            {
                {
                    ":tensor/pow",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(a: Tensor, b: Tensor | number) => Tensor",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            // Tensor to Tensor power
                            if (norm[0]->code() == TensorType::typeCode() &&
                                norm[1]->code() == TensorType::typeCode()) {
                                return norm[0];
                            }
                            if (norm[0]->code() == TensorType::typeCode() &&
                                (norm[1]->code() == TypeCode::Int ||
                                 norm[1]->code() == TypeCode::Long ||
                                 norm[1]->code() == TypeCode::Float ||
                                 norm[1]->code() == TypeCode::Double)) {
                                return norm[0];
                            }
                            if ((norm[0]->code() == TypeCode::Int ||
                                 norm[0]->code() == TypeCode::Long ||
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
            "matpow",
            {
                {
                    ":tensor/matpow",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(a: Tensor, n: int) => Tensor",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() == TensorType::typeCode() &&
                                norm[1]->equals(Type::Int())) {
                                auto tensor_type = std::dynamic_pointer_cast<TensorType>(norm[0]);
                                if (tensor_type && tensor_type->shape().size() == 2 &&
                                    tensor_type->shape()[0] == tensor_type->shape()[1]) {
                                    return norm[0];
                                }
                            }
                            return nullopt;
                        }),
                },
            }),
        OperatorGroup::create(
            "reshape",
            {
                {
                    ":tensor/reshape",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}, {Type::Any(), false}},
                        TensorType::Default()),
                },
            }),
        OperatorGroup::create(
            "transpose",
            {
                {
                    ":tensor/transpose",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(a: Tensor) => Tensor",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() == TensorType::typeCode()) {
                                auto tensor_type = std::dynamic_pointer_cast<TensorType>(norm[0]);
                                if (tensor_type && tensor_type->shape().size() == 2) {
                                    auto shape = tensor_type->shape();
                                    std::swap(shape[0], shape[1]);
                                    return TensorType::create(tensor_type->dType(), shape);
                                }
                                return norm[0];
                            }
                            return nullopt;
                        }),
                },
            }),
        OperatorGroup::create(
            "flatten",
            {
                {
                    ":tensor/flatten",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(a: Tensor) => Tensor",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() == TensorType::typeCode()) {
                                return norm[0];
                            }
                            return nullopt;
                        }),
                },
            }),

        OperatorGroup::create(
            "concat",
            {
                {
                    ":tensor/concat",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}, {Type::Any(), false}, {Type::Int(), false}},
                        TensorType::Default()),
                },
            }),
        OperatorGroup::create(
            "stack",
            {
                {
                    ":tensor/stack",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}, {Type::Int(), false}},
                        TensorType::Default()),
                },
            }),

        OperatorGroup::create(
            "sum",
            {
                {
                    ":tensor/sum",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(a: Tensor) => float",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() == TensorType::typeCode()) {
                                return Type::Float();
                            }
                            return nullopt;
                        }),
                },
            }),
        OperatorGroup::create(
            "mean",
            {
                {
                    ":tensor/mean",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(a: Tensor) => Tensor",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm[0]->code() == TensorType::typeCode()) {
                                return TensorType::Default();
                            }
                            return nullopt;
                        }),
                },
            }),
        OperatorGroup::create(
            "min",
            {
                {
                    ":tensor/min",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}},
                        TensorType::Default()),
                },
            }),
        OperatorGroup::create(
            "max",
            {
                {
                    ":tensor/max",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}},
                        TensorType::Default()),
                },
            }),

        OperatorGroup::create(
            "norm_l1",
            {
                {
                    ":tensor/norm_l1",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}},
                        TensorType::Default()),
                },
            }),
        OperatorGroup::create(
            "norm_l2",
            {
                {
                    ":tensor/norm_l2",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}},
                        TensorType::Default()),
                },
            }),

        OperatorGroup::create(
            "sin",
            {
                {
                    ":tensor/sin",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}},
                        TensorType::create(Type::Double(), {0})),
                },
            }),
        OperatorGroup::create(
            "cos",
            {
                {
                    ":tensor/cos",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}},
                        TensorType::Default()),
                },
            }),
        OperatorGroup::create(
            "exp",
            {
                {
                    ":tensor/exp",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Any(), false}},
                        TensorType::Default()),
                },
            }),
        OperatorGroup::create(
            "show",
            {
                {
                    ":tensor/show",
                    StaticFuncTypeResolver::create({}, {{Type::Any(), false}}, Type::Void()),
                },
            }),
    };

    return groups;
}

TensorBuiltinModule::TensorBuiltinModule(context_ptr_t ctx) : BuiltinModule("tensor", ctx) {
    for (const auto &group : getOperatorGroups()) {
        exportEntity(group->name(), group);
    }
    exportType("Tensor", TensorType::Default());
}

bool TensorBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    context_->registerExecutorFactory("tensor", [&]() {
        return BasicBuiltinExecutor::create(context_);
    });
    loaded_ = true;
    return true;
}
