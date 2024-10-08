/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Updated: Oct. 08, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "null.h"
#include "any.h"

NullData::NullData() : Data(voidTypePtr) {}

bool NullData::equals(const data_ptr_t &other) const { return true; }

data_ptr_t NullData::convert(type_ptr_t target, bool inplace = false) {
    if (target == type_ || type_->code() == target->code()) {
        // same type, no need to convert
        return shared_from_this();
    }
    if (target->code() == TypeCode::ANY) {
        return std::make_shared<AnyData>(shared_from_this());
    }
    throw DataConvError("Cannot convert null to " + typeCodeToString(target->code()));
}

data_ptr_t NullData::clone(bool deep = false) const { return std::make_shared<NullData>(); }

const std::string NullData::toString() const { return "null"; }
