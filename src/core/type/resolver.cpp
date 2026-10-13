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
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/type/resolver.h"

#include "camel/core/type/composite/tuple.h"
#include "camel/utils/assert.h"

using namespace camel::core::type;

std::optional<FunctionType *> StaticFuncTypeResolver::resolve(
    const type_vec_t &with, const type_vec_t &norm, const ModifierSet &modifiers) const {
    ASSERT(funcType_, "FunctionType is null");
    const size_t withCount = funcType_->withTypesCount();
    const size_t normCount = funcType_->normTypesCount();
    if (withCount != with.size() || normCount != norm.size()) {
        return std::nullopt; // reject
    }
    // A parameter type is the type of the value; the argument of a `var` parameter is the
    // variable holding it.
    for (size_t i = 0; i < withCount; i++) {
        if (!funcType_->withTypeAt(i)->assignableFrom(valueTypeOf(with[i]))) {
            return std::nullopt; // reject
        }
    }
    for (size_t i = 0; i < normCount; i++) {
        if (!funcType_->normTypeAt(i)->assignableFrom(valueTypeOf(norm[i]))) {
            return std::nullopt; // reject
        }
    }
    return funcType_; // accept
}

std::optional<FunctionType *> DynamicFuncTypeResolver::resolve(
    const type_vec_t &withArgs, const type_vec_t &normArgs, const ModifierSet &modifiers) const {
    if (withVars_.first != -1 && static_cast<int>(withArgs.size()) != withVars_.first) {
        return std::nullopt; // reject
    }
    if (normVars_.first != -1 && static_cast<int>(normArgs.size()) != normVars_.first) {
        return std::nullopt; // reject
    }
    // A `var` parameter is typed by the value; its argument may be the variable holding it.
    const auto isVar = [](const var_declare_t &vars, size_t i) {
        return static_cast<int>(i) < vars.first ? bool(vars.second[i]) : false;
    };
    type_vec_t with = withArgs, norm = normArgs;
    for (size_t i = 0; i < with.size(); i++) {
        with[i] = isVar(withVars_, i) ? valueTypeOf(with[i]) : with[i];
    }
    for (size_t i = 0; i < norm.size(); i++) {
        norm[i] = isVar(normVars_, i) ? valueTypeOf(norm[i]) : norm[i];
    }
    auto optResType = resolver_(with, norm, modifiers);
    if (!optResType) {
        return std::nullopt; // reject
    }
    param_vec_t withParams, normParams;
    for (size_t i = 0; i < with.size(); i++) {
        withParams.emplace_back(with[i], isVar(withVars_, i));
    }
    for (size_t i = 0; i < norm.size(); i++) {
        normParams.emplace_back(norm[i], isVar(normVars_, i));
    }
    // The same call shape resolves to the same signature object: a program resolves the same
    // few operator signatures at every call, and the permanent space never frees.
    return FunctionType::interned(withParams, normParams, *optResType, modifiers);
}
