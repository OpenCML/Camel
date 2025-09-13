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
 * Updated: Mar. 09, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "union.h"
#include "../other/ref.h"

UnionData::UnionData(const type_ptr_t &type, const data_ptr_t &data) : Data(type) {
    data_ = data;
    if (data->type()->code() == TypeCode::REF) {
        ref_ = std::dynamic_pointer_cast<RefData>(data)->ref();
    }
}

data_ptr_t UnionData::data() const { return data_; }

type_ptr_t UnionData::actualType() const { return data_->type(); }

std::vector<std::string> UnionData::refs() const { return {ref_}; }

bool UnionData::resolved() const { return ref_.empty(); }

void UnionData::resolve(const data_vec_t &dataList) {
    if (ref_.empty()) {
        return;
    }
    if (dataList.empty()) {
        throw std::runtime_error("Cannot resolve UnionData with an empty dataList");
    }
    data_ = dataList[0];
    ref_.clear();
}

bool UnionData::equals(const data_ptr_t &other) const {
    throw std::runtime_error("UnionData::equals() not implemented");
}

data_ptr_t UnionData::convert(type_ptr_t target, bool inplace) {
    throw std::runtime_error("UnionData::convert() not implemented");
}

data_ptr_t UnionData::clone(bool deep) const {
    return std::make_shared<UnionData>(type_, data_->clone(deep));
}

const std::string UnionData::toString() const { return data_->toString(); }
