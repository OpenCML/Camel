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
 * Author: Camel Contributors
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Derivative registry (see derivative.h).
 */

#include "camel/core/derivative.h"

#include <mutex>

namespace camel::core {

vjp_node_t VjpBuilder::addStaticFloat(double value) {
    return addStatic(rtdata::toSlot<rtdata::Float64>(value), type::Type::Float64());
}

void noGradient(VjpBuilder &builder, const VjpCall &call) {
    (void)builder;
    (void)call;
}

DerivativeRegistry &DerivativeRegistry::instance() {
    static DerivativeRegistry registry;
    return registry;
}

void DerivativeRegistry::setRule(std::string_view uri, VjpRule rule) {
    std::unique_lock lock(mutex_);
    rules_.insert_or_assign(std::string(uri), rule);
}

VjpRule DerivativeRegistry::findRule(std::string_view uri) const {
    std::shared_lock lock(mutex_);
    auto it = rules_.find(uri);
    return it == rules_.end() ? nullptr : it->second;
}

void DerivativeRegistry::setTangentSpace(type::TypeCode code, TangentSpace space) {
    std::unique_lock lock(mutex_);
    tangents_.insert_or_assign(code, std::move(space));
}

const TangentSpace *DerivativeRegistry::findTangentSpace(const type::Type *type) const {
    if (type == nullptr) {
        return nullptr;
    }
    std::shared_lock lock(mutex_);
    auto it = tangents_.find(type->code());
    return it == tangents_.end() ? nullptr : &it->second;
}

} // namespace camel::core
