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
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Value type refiners (see operator_traits.h).
 */

#include "camel/core/operator_traits.h"

#include <mutex>

namespace camel::core {

ValueTypeRefinerRegistry &ValueTypeRefinerRegistry::instance() {
    static ValueTypeRefinerRegistry registry;
    return registry;
}

void ValueTypeRefinerRegistry::add(ValueTypeRefiner refiner) {
    std::unique_lock lock(mutex_);
    refiners_.push_back(std::move(refiner));
}

type::Type *ValueTypeRefinerRegistry::refine(slot_t value, type::Type *type) const {
    std::shared_lock lock(mutex_);
    for (const ValueTypeRefiner &refiner : refiners_) {
        if (type::Type *exact = refiner(value, type)) {
            return exact;
        }
    }
    return type;
}

} // namespace camel::core
