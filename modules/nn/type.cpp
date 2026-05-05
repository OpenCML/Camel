/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "type.h"

#include "camel/core/mm.h"

namespace camel::nn {

using namespace camel::core::type;
namespace mm = camel::core::mm;

ParameterType::ParameterType() : OtherType(typeCode()) {}

TypeCode ParameterType::typeCode() {
    static TypeCode code =
        registerOtherType("Parameter", TypeFlag::Composite | TypeFlag::GC_Traced);
    return code;
}

Type *ParameterType::Default() {
    static ParameterType *type = [] {
        void *mem = mm::autoSpace().alloc(sizeof(ParameterType), alignof(ParameterType));
        ASSERT(mem != nullptr, "Failed to allocate ParameterType from autoSpace");
        return new (mem) ParameterType();
    }();
    return type;
}

std::string ParameterType::toString() const { return "Parameter"; }

std::string ParameterType::mangle() const { return "P"; }

Type *ParameterType::clone(bool deep) const {
    (void)deep;
    return Default();
}

bool ParameterType::equals(Type *type) const {
    return type != nullptr && type->code() == typeCode();
}

CastSafety ParameterType::castSafetyFrom(Type *sourceType) const {
    if (auto r = Type::checkCastSafetyWithAny(code(), sourceType)) {
        return *r;
    }
    return assignableFrom(sourceType) ? CastSafety::Safe : CastSafety::Forbidden;
}

bool ParameterType::assignableFrom(Type *sourceType) const {
    return sourceType != nullptr && sourceType->code() == typeCode();
}

OtherType *ParameterType::cloneWithParams(std::span<Type *const> params) const {
    (void)params;
    return static_cast<OtherType *>(Default());
}

} // namespace camel::nn
