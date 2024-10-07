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
 * Created: Oct. 6, 2024
 * Updated: Oct. 7, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "string.h"
#include "any.h"

#include "../struct/list.h"

StringData::StringData(const std::string &data) : Data(stringTypePtr), data_(data) {}

const std::string &StringData::data() const { return data_; }

bool StringData::equals(const data_ptr_t &other) const {
    if (auto o = std::dynamic_pointer_cast<StringData>(other)) {
        return data_ == o->data_;
    }
    return false;
}

data_ptr_t StringData::convert(type_ptr_t target, bool inplace) {
    if (target == type_ || type_->code() == target->code()) {
        // same type, no need to convert
        return shared_from_this();
    }
    try {
        if (target->primary()) {
            switch (target->code()) {
            case TypeCode::BOOL: {
                return std::make_shared<PrimaryData<bool>>(data_.length() > 0);
            }
            default:
                throw UnsupportedConvError();
            }

        } else if (target->special()) {
            switch (target->code()) {
            case TypeCode::ANY: {
                return std::make_shared<AnyData>(shared_from_this());
            }
            default:
                throw UnsupportedConvError();
            }
        }
        throw UnsupportedConvError();
    } catch (const UnsupportedConvError &e) {
        throw DataConvError("Cannot convert " + typeCodeToString(type_->code()) + " to " +
                            typeCodeToString(target->code()));
    } catch (const std::exception &e) {
        throw DataConvError(e.what());
    }
}

data_ptr_t StringData::clone(bool deep) const { return std::make_shared<StringData>(data_); }

const std::string StringData::toString() const {
    std::regex re("\\n");
    return "\"" + std::regex_replace(data_, re, "\\n") + "\"";
}
