// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
