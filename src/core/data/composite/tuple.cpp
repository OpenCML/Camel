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
 * Updated: Dec. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "tuple.h"
#include "utils/scope.h"
#include "utils/str.h"

#include "../special/any.h"
#include "../special/null.h"
#include "../special/ref.h"

using namespace std;

TupleData::TupleData(data_list_t data) : data_(data) {
    std::vector<type_ptr_t> types;
    size_t i = 0;
    for (const auto &e : data) {
        types.push_back(e->type());
        if (e->type()->code() == TypeCode::Ref) {
            refIndices_.push_back(i);
        }
        i++;
    }
    type_ = std::make_shared<TupleType>(types);
}

TupleData::TupleData(type_ptr_t type, data_vec_t &&data)
    : CompositeData(type), data_(std::move(data)) {
    ASSERT(type->code() == TypeCode::Tuple, "Type is not TupleType");
    ASSERT(
        (*static_cast<TupleType *>(type_.get())).size() == data_.size(),
        "Data size does not match TupleType size");
}

void TupleData::emplace(const data_ptr_t &e) {
    data_.push_back(e);
    TupleType &tupleType = *static_cast<TupleType *>(type_.get());
    tupleType.add(e->type());
    if (e->type()->code() == TypeCode::Ref) {
        refIndices_.push_back(data_.size() - 1);
    }
}

data_ptr_t TupleData::get(size_t index) const {
    ASSERT(resolved(), "Cannot get data from unresolved TupleData");
    if (index >= data_.size()) {
        return nullptr;
    }
    return data_[index];
}

bool TupleData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for TupleData
    return true;
}

vector<string> TupleData::refs() const {
    vector<string> res;
    res.reserve(refIndices_.size());
    for (const auto &idx : refIndices_) {
        data_ptr_t ref = data_[idx];
        res.push_back(tt::as_shared<RefData>(ref)->ref());
    }
    return res;
}

void TupleData::resolve(const data_vec_t &dataList) {
    if (refIndices_.empty()) {
        return;
    }
    ASSERT(refIndices_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < refIndices_.size(); i++) {
        size_t idx = refIndices_[i];
        data_[idx] = dataList[i];
    }
    refIndices_.clear();
}

data_ptr_t TupleData::clone(bool deep) const {
    auto tuple   = make_shared<TupleData>();
    tuple->type_ = type_;
    for (const auto &e : data_) {
        tuple->emplace(deep ? e->clone(deep) : e);
    }
    return tuple;
}

const string TupleData::toString() const {
    string str = "(";
    str += strutil::join(data_, ", ", [](const data_ptr_t &e) { return e->toString(); });
    str += ")";
    return str;
}
