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
 * Created: Dec. 19, 2025
 * Updated: Dec. 22, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "plot.h"

using namespace std;

static const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "plot",
            {
                {
                    ":plot/plot",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(data: (typeas T)[], filename?: string) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            // 检查参数数量：至少1个，最多2个
                            if (norm.size() < 1 || norm.size() > 2)
                                return nullopt;
                            // 第一个参数必须是数组
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            // 检查数组元素类型是否支持（int, long, float, double）
                            TypeCode elemCode =
                                tt::as_shared<ArrayType>(norm[0])->elemType()->code();
                            if (elemCode != TypeCode::Int && elemCode != TypeCode::Long &&
                                elemCode != TypeCode::Float && elemCode != TypeCode::Double)
                                return nullopt;
                            // 第二个参数是可选的字符串（文件名）
                            if (norm.size() == 2 && norm[1]->code() != TypeCode::String)
                                return nullopt;
                            // 返回void
                            return Type::Void();
                        }),
                },
            }),
        OperatorGroup::create(
            "run_phot",
            {
                {
                    ":plot/run_phot",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(script_path: string, ...args: string[]) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<type_ptr_t> {
                            // 检查参数数量：至少1个
                            if (norm.size() < 1)
                                return nullopt;
                            // 第一个参数必须是字符串（脚本路径）
                            if (norm[0]->code() != TypeCode::String)
                                return nullopt;
                            // 其他参数都必须是字符串
                            for (size_t i = 1; i < norm.size(); ++i) {
                                if (norm[i]->code() != TypeCode::String)
                                    return nullopt;
                            }
                            // 返回void
                            return Type::Void();
                        }),
                },
            }),
    };

    return groups;
}

PlotBuiltinModule::PlotBuiltinModule(context_ptr_t ctx)
    : BuiltinModule("plot", ctx) {
    for (const auto &group : getOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

module_ptr_t PlotBuiltinModule::create(context_ptr_t ctx) {
    return std::make_shared<PlotBuiltinModule>(ctx);
}

bool PlotBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    return true;
}

