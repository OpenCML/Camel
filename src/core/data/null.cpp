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
 * Created: Oct. 06, 2024
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/data/null.h"

using namespace camel::core::type;

namespace camel::core::data {

NullData::NullData() : Data(Type::Void()) {}

bool NullData::equals(const data_ptr_t &other) const { return true; }

data_ptr_t NullData::clone(bool deep) const { return Data::null(); }

const std::string NullData::toString() const { return "null"; }

data_ptr_t NullData::convertTo(type::Type *type) {
    if (type->equals(type_)) {
        return std::make_shared<NullData>();
    }
    return nullptr;
}

} // namespace camel::core::data
