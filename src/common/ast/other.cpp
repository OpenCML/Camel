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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Jul. 03, 2025
 * Updated: Jul. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "other.h"
#include "utils/str.h"

namespace AbstractSyntaxTree {
const std::string CarrierLoad::toString() const {
    std::string refs = join(refs_, std::string(", "));
    switch (type_) {
    case CarrierType::Dict:
        return "Carrier: {" + refs + "}";
    case CarrierType::List:
        return "Carrier: [" + refs + "]";
    case CarrierType::Tuple:
        return "Carrier: (" + refs + ")";
    default:
        throw std::runtime_error("Unknown CarrierType");
    }
}
} // namespace AbstractSyntaxTree