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
 * Author: Yuxuan Zheng
 * Created: Dec. 22, 2025
 * Updated: Dec. 22, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "phot.h"

using namespace std;

static const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "config",
            {
                {
                    ":phot/config",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(plot: bool) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm.size() < 1 || norm.size() > 1)
                                return nullopt;
                            if (norm[0]->code() != TypeCode::Bool)
                                return nullopt;
                            return Type::Void();
                        }),
                },
            }),
        OperatorGroup::create(
            "gen_bits",
            {
                {
                    ":phot/gen_bits",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int(), false}, {Type::Int(), false}},
                        TupleType::create({ArrayType::create(Type::Int()), ArrayType::create(Type::Int())})),
                },
            }),
        OperatorGroup::create(
            "modulation",
            {
                {
                    ":phot/modulation",
                    StaticFuncTypeResolver::create(
                        {},
                        {{TupleType::create({ArrayType::create(Type::Int()), ArrayType::create(Type::Int())}), false}, {Type::Int(), false}},
                        // 返回 ((double[], double[]), (double[], double[]))，即 X_Pol 和 Y_Pol 的实部和虚部
                        TupleType::create({
                            TupleType::create({ArrayType::create(Type::Double()), ArrayType::create(Type::Double())}),  // X_Pol (real, imag)
                            TupleType::create({ArrayType::create(Type::Double()), ArrayType::create(Type::Double())})   // Y_Pol (real, imag)
                        })),
                },
            }),
        OperatorGroup::create(
            "up_sample",
            {
                {
                    ":phot/up_sample",
                    StaticFuncTypeResolver::create(
                        {},
                        // 输入：((double[], double[]), (double[], double[])) 即 X_Pol 和 Y_Pol 的实部和虚部
                        {{TupleType::create({
                            TupleType::create({ArrayType::create(Type::Double()), ArrayType::create(Type::Double())}),  // X_Pol
                            TupleType::create({ArrayType::create(Type::Double()), ArrayType::create(Type::Double())})   // Y_Pol
                        }), false}, {Type::Int(), false}},
                        // 返回：((double[], double[]), (double[], double[])) 即 X_Pol 和 Y_Pol 的实部和虚部
                        TupleType::create({
                            TupleType::create({ArrayType::create(Type::Double()), ArrayType::create(Type::Double())}),  // X_Pol
                            TupleType::create({ArrayType::create(Type::Double()), ArrayType::create(Type::Double())})   // Y_Pol
                        })),
                },
            }),
        OperatorGroup::create(
            "pulse_shaper",
            {
                {
                    ":phot/pulse_shaper",
                    StaticFuncTypeResolver::create(
                        {},
                        // 输入：((double[], double[]), (double[], double[])) 即 X_Pol 和 Y_Pol 的实部和虚部
                        {{TupleType::create({
                            TupleType::create({ArrayType::create(Type::Double()), ArrayType::create(Type::Double())}),  // X_Pol
                            TupleType::create({ArrayType::create(Type::Double()), ArrayType::create(Type::Double())})   // Y_Pol
                        }), false}, 
                         {Type::Int(), false}, 
                         {Type::Double(), false}, 
                         {Type::Double(), false}},
                        // 返回：((double[], double[]), (double[], double[])) 即 X_Pol 和 Y_Pol 的实部和虚部
                        TupleType::create({
                            TupleType::create({ArrayType::create(Type::Double()), ArrayType::create(Type::Double())}),  // X_Pol
                            TupleType::create({ArrayType::create(Type::Double()), ArrayType::create(Type::Double())})   // Y_Pol
                        })),
                },
            }),
        OperatorGroup::create(
            "constellation_diagram",
            {
                {
                    ":phot/constellation_diagram",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(signals: ((double[], double[]), (double[], double[])), is_plot?: bool, isdata?: bool) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            if (norm.size() < 1 || norm.size() > 3)
                                return nullopt;
                            // 第一个参数必须是元组，包含两个元组（X_Pol 和 Y_Pol），每个元组包含两个double数组（real 和 imag）
                            if (norm[0]->code() != TypeCode::Tuple)
                                return nullopt;
                            auto outerTupleType = tt::as_shared<TupleType>(norm[0]);
                            if (outerTupleType->types().size() != 2)
                                return nullopt;
                            // 检查 X_Pol 和 Y_Pol 是否都是元组
                            if (outerTupleType->types()[0]->code() != TypeCode::Tuple ||
                                outerTupleType->types()[1]->code() != TypeCode::Tuple)
                                return nullopt;
                            auto xPolTupleType = tt::as_shared<TupleType>(outerTupleType->types()[0]);
                            auto yPolTupleType = tt::as_shared<TupleType>(outerTupleType->types()[1]);
                            // 检查每个元组是否包含两个数组（real 和 imag）
                            if (xPolTupleType->types().size() != 2 || yPolTupleType->types().size() != 2)
                                return nullopt;
                            if (xPolTupleType->types()[0]->code() != TypeCode::Array ||
                                xPolTupleType->types()[1]->code() != TypeCode::Array ||
                                yPolTupleType->types()[0]->code() != TypeCode::Array ||
                                yPolTupleType->types()[1]->code() != TypeCode::Array)
                                return nullopt;
                            // 可选参数必须是bool
                            if (norm.size() > 1 && norm[1]->code() != TypeCode::Bool)
                                return nullopt;
                            if (norm.size() > 2 && norm[2]->code() != TypeCode::Bool)
                                return nullopt;
                            return Type::Void();
                        }),
                },
            }),
    };

    return groups;
}

PhotBuiltinModule::PhotBuiltinModule(context_ptr_t ctx)
    : BuiltinModule("phot", ctx) {
    for (const auto &group : getOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

module_ptr_t PhotBuiltinModule::create(context_ptr_t ctx) {
    return std::make_shared<PhotBuiltinModule>(ctx);
}

bool PhotBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    return true;
}

