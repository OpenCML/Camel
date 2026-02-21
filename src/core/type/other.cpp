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
 * Created: Oct. 11, 2024
 * Updated: Feb. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/type/other.h"
#include "camel/core/mm.h"
#include "camel/utils/assert.h"

#include <span>

std::mutex OtherTypeRegistry::mtx;
std::unordered_map<std::string, TypeCode> OtherTypeRegistry::registry;
std::unordered_map<TypeCode, std::string> OtherTypeRegistry::reverseRegistry;
std::atomic<unsigned int> OtherTypeRegistry::counter{0};

TypeCode registerOtherType(const std::string &typeName, TypeFlag flags) {
    return OtherTypeRegistry::registerType(typeName, flags);
}

std::string OtherType::typeName() const {
    ASSERT(::isOtherType(code_), "Not an OtherType");
    return OtherTypeRegistry::getTypeName(code_);
}

Type **OtherType::copyParams(std::span<Type *const> params) {
    if (params.empty())
        return nullptr;
    size_t n     = params.size();
    void *arrMem = mm::permSpace().alloc(n * sizeof(Type *), alignof(Type *));
    ASSERT(arrMem != nullptr, "Failed to allocate params array from permSpace");
    Type **p = static_cast<Type **>(arrMem);
    for (size_t i = 0; i < n; ++i)
        p[i] = params[i];
    return p;
}
