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
 * Updated: Oct. 18, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "vector.h"
#include "params.h"
#include "utils/log.h"

#include "../other/any.h"
#include "../other/null.h"
#include "../other/ref.h"

using namespace std;

VectorData::VectorData(type_ptr_t type, data_list_t data) : data_(data) {
    size_t i = 0;
    for (const auto &e : data) {
        if (e->type()->code() == TypeCode::REF) {
            refs_.push_back(i);
        } else if (e->type()->convertibility(*type) != TypeConv::SAFE) {
            throw DataConvError("Cannot convert " + e->type()->toString() + " to " + type->toString());
        }
        i++;
    }
    type_ = std::make_shared<VectorType>(type);
}

void VectorData::emplace(const data_ptr_t &e) {
    if (e->type()->code() == TypeCode::REF) {
        refs_.push_back(data_.size());
    } else if (e->type()->convertibility(*type_) != TypeConv::SAFE) {
        throw DataConvError("Cannot convert " + e->type()->toString() + " to " + type_->toString());
    }
    data_.push_back(e);
}

void VectorData::pushBack(const data_ptr_t &e) {
    ASSERT(resolved(), "Cannot push data to unresolved VectorData");
    if (e->type()->convertibility(*type_) != TypeConv::SAFE) {
        throw DataConvError("Cannot convert " + e->type()->toString() + " to " + type_->toString());
    }
    data_.push_back(e);
}

data_ptr_t VectorData::popBack() {
    ASSERT(resolved(), "Cannot pop data from unresolved VectorData");
    if (data_.empty()) {
        return nullptr;
    }
    data_ptr_t back = data_.back();
    data_.pop_back();
    return back;
}

data_ptr_t VectorData::get(size_t index) const {
    ASSERT(resolved(), "Cannot get data from unresolved VectorData");
    if (index >= data_.size()) {
        return nullptr;
    }
    return data_[index];
}

bool VectorData::set(size_t index, const data_ptr_t &e) {
    ASSERT(resolved(), "Cannot set data to unresolved VectorData");
    if (index >= data_.size()) {
        return false;
    }
    if (e->type()->convertibility(*type_) != TypeConv::SAFE) {
        throw DataConvError("Cannot convert " + e->type()->toString() + " to " + type_->toString());
    }
    data_[index] = e;
    return true;
}

size_t VectorData::size() const { return data_.size(); }

size_t VectorData::length() const { return data_.size(); }

bool VectorData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for VectorData
    return true;
}

data_ptr_t VectorData::convert(type_ptr_t target, bool inplace) {
    if (target == type_ || type_->equals(target)) {
        // same type, no need to convert
        return shared_from_this();
    }
    try {
        if (target->structured()) {
            switch (target->code()) {
                // TODO: implement conversion to other structured types
            case TypeCode::PARAMS:
                return convertToParams(dynamic_pointer_cast<ParamsType>(target));
                break;
            default:
                throw UnsupportedConvError();
            }
        } else if (target->special()) {
            switch (target->code()) {
            case TypeCode::ANY:
                return make_shared<AnyData>(shared_from_this());
                break;
            case TypeCode::VOID:
                return make_shared<NullData>();
                break;
            default:
                throw UnsupportedConvError();
            }
        }
        throw UnsupportedConvError();
    } catch (const UnsupportedConvError &e) {
        throw DataConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
    } catch (const std::exception &e) {
        throw DataConvError(e.what());
    }
    throw DataConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

vector<string> VectorData::refs() const {
    vector<string> res;
    for (const auto &idx : refs_) {
        data_ptr_t ref = data_[idx];
        res.push_back(dynamic_pointer_cast<RefData>(ref)->ref());
    }
    return res;
}

void VectorData::resolve(const data_vec_t &dataList) {
    if (refs_.empty()) {
        return;
    }
    ASSERT(refs_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < refs_.size(); i++) {
        size_t idx = refs_[i];
        data_[idx] = dataList[i];
    }
    refs_.clear();
}

data_ptr_t VectorData::clone(bool deep) const {
    auto vec = make_shared<VectorData>(type_);
    for (const auto &e : data_) {
        vec->emplace(deep ? e->clone(deep) : e);
    }
    return vec;
}

const string VectorData::toString() const {
    string str = "[";
    for (const auto &e : data_) {
        str += e->toString() + ", ";
    }
    if (data_.size() > 0) {
        str.pop_back();
        str.pop_back();
    }
    str += "]";
    return str;
}

data_ptr_t VectorData::convertToParams(const std::shared_ptr<ParamsType> &target) {
    auto params = make_shared<ParamsData>();
    for (const auto &e : data_) {
        params->emplace(e);
    }
    return params->convertToParams(target);
}
