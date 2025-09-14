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
 * Created: Oct. 07, 2024
 * Updated: Oct. 17, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "any.h"
#include "null.h"

AnyData::AnyData(const data_ptr_t &data) : Data(anyTypePtr) {
    if (data) {
        data_ = data;
    } else {
        data_ = Data::null();
    }
}

bool AnyData::equals(const data_ptr_t &other) const { return true; }

data_ptr_t AnyData::as(type_ptr_t target, bool inplace) {
    if (target == type_ || type_->code() == target->code()) {
        // same type, no need to convert
        return shared_from_this();
    }
    throw DataConvError("Cannot convert any to " + typeCodeToString(target->code()));
}

data_ptr_t AnyData::clone(bool deep) const { return std::make_shared<AnyData>(data_->clone(deep)); }

const std::string AnyData::toString() const { return data_->toString(); }
