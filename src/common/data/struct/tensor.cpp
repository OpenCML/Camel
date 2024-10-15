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

#include "tensor.h"
#include "utils/log.h"

using namespace std;

TensorData::TensorData(const type_ptr_t &elementType, const std::vector<size_t> &shape)
    : StructData(std::make_shared<TensorType>(elementType, shape)) {}

data_ptr_t TensorData::at(const std::vector<size_t> &index) const {
    // TODO
    throw runtime_error("Not implemented");
}

std::vector<std::string> TensorData::refs() const {
    // TODO
    throw runtime_error("Not implemented");
}

void TensorData::resolve(const data_vec_t &dataList) {
    // TODO
    throw runtime_error("Not implemented");
}

bool TensorData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for VectorData
    return true;
}

data_ptr_t TensorData::convert(type_ptr_t target, bool inplace) {
    // TODO
    throw DataConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

data_ptr_t TensorData::clone(bool) const { throw runtime_error("Not implemented"); }

const string TensorData::toString() const { return string("TensorData"); }
