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
 * Created: Oct. 03, 2024
 * Updated: Oct. 09, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "resolver.h"

#include "utils/assert.h"

std::optional<func_type_ptr_t> StaticFuncTypeResolver::resolve(
    const param_vec_t &with, const param_vec_t &norm, const ModifierSet &modifiers) const {
    ASSERT(funcType_, "FunctionType is null");
    const auto &withTypes = funcType_->withTypes();
    const auto &normTypes = funcType_->normTypes();
    if (withTypes.size() != with.size() || normTypes.size() != norm.size() ||
        funcType_->modifiers() != modifiers) {
        return std::nullopt; // reject
    }
    for (size_t i = 0; i < withTypes.size(); i++) {
        if (withTypes[i].second != with[i].second || !withTypes[i].first->equals(with[i].first)) {
            return std::nullopt; // reject
        }
    }
    for (size_t i = 0; i < normTypes.size(); i++) {
        if (normTypes[i].second != norm[i].second || !normTypes[i].first->equals(norm[i].first)) {
            return std::nullopt; // reject
        }
    }
    return funcType_; // accept
}

std::optional<func_type_ptr_t> DynamicFuncTypeResolver::resolve(
    const param_vec_t &with, const param_vec_t &norm, const ModifierSet &modifiers) const {
    auto optResType = resolver_(with, norm, modifiers);
    if (!optResType) {
        return std::nullopt; // reject
    }
    return std::make_shared<FunctionType>(with, norm, *optResType, modifiers);
}
