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
 * Operator traits registry (see operator_traits.h).
 */

#include "camel/core/operator_traits.h"

#include <mutex>

namespace camel::core {

OperatorTraitsRegistry &OperatorTraitsRegistry::instance() {
    static OperatorTraitsRegistry registry;
    return registry;
}

void OperatorTraitsRegistry::set(std::string_view uri, OperatorTraits traits) {
    std::unique_lock lock(mutex_);
    traits_.insert_or_assign(std::string(uri), traits);
}

std::optional<OperatorTraits> OperatorTraitsRegistry::find(std::string_view uri) const {
    std::shared_lock lock(mutex_);
    auto it = traits_.find(uri);
    if (it == traits_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool OperatorTraitsRegistry::isPure(std::string_view uri) const {
    const auto traits = find(uri);
    return traits && traits->pure;
}

} // namespace camel::core
