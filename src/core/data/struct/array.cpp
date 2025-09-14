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

#include "array.h"
#include "params.h"
#include "utils/scope.h"

#include "../other/any.h"
#include "../other/null.h"
#include "../other/ref.h"

using namespace std;

ArrayData::ArrayData(type_ptr_t type, size_t length, data_list_t data) : data_(data) {
    size_t i = 0;
    for (const auto &e : data) {
        if (e->type()->code() == TypeCode::Ref) {
            refs_.push_back(i);
        } else if (e->type()->convertibility(*type) != TypeConv::SAFE) {
            throw DataConvError(
                "Cannot convert " + e->type()->toString() + " to " + type->toString());
        }
        i++;
    }
    type_ = std::make_shared<ArrayType>(type, length);
    data_.resize(length);
}

bool ArrayData::emplace(const data_ptr_t &e, size_t index) {
    if (index >= data_.size()) {
        return false;
    }
    data_[index] = e;
    if (e->type()->code() == TypeCode::Ref) {
        refs_.push_back(index);
    }
    return true;
}

data_ptr_t ArrayData::get(size_t index) const {
    ASSERT(resolved(), "Cannot get data from unresolved ArrayData");
    if (index >= data_.size()) {
        return nullptr;
    }
    return data_[index];
}

bool ArrayData::set(size_t index, const data_ptr_t &e) {
    ASSERT(resolved(), "Cannot set data to unresolved ArrayData");
    if (index >= data_.size()) {
        return false;
    }
    data_[index] = e;
    return true;
}

size_t ArrayData::size() const { return data_.size(); }
size_t ArrayData::length() const { return data_.size(); }

bool ArrayData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for ArrayData
    return true;
}

data_ptr_t ArrayData::as(type_ptr_t target, bool inplace) {
    if (target == type_ || type_->equals(target)) {
        // same type, no need to convert
        return shared_from_this();
    }
    try {
        if (target->structured()) {
            switch (target->code()) {
                // TODO: implement conversion to other structured types
            case TypeCode::Params: {
                auto res = dynamic_pointer_cast<ParamsType>(target);
                return convertToParams(res);
            } break;
            default:
                throw UnsupportedConvError();
            }
        } else if (target->special()) {
            switch (target->code()) {
            case TypeCode::Any:
                return make_shared<AnyData>(shared_from_this());
                break;
            case TypeCode::Void:
                return Data::null();
                break;
            default:
                throw UnsupportedConvError();
            }
        }
        throw UnsupportedConvError();
    } catch (const UnsupportedConvError &e) {
        throw DataConvError(
            "Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
    } catch (const std::exception &e) {
        throw DataConvError(e.what());
    }
    throw DataConvError(
        "Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

vector<string> ArrayData::refs() const {
    vector<string> res;
    for (const auto &idx : refs_) {
        data_ptr_t ref = data_[idx];
        res.push_back(dynamic_pointer_cast<RefData>(ref)->ref());
    }
    return res;
}

void ArrayData::resolve(const data_vec_t &dataList) {
    if (refs_.empty()) {
        return;
    }
    ASSERT(refs_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < refs_.size(); i++) {
        size_t idx = refs_[i];
        // TODO: need to check type compatibility
        data_[idx] = dataList[i];
    }
    refs_.clear();
}

data_ptr_t ArrayData::clone(bool deep) const {
    auto res = make_shared<ArrayData>(dynamic_pointer_cast<ArrayType>(type_), data_.size());
    for (size_t i = 0; i < data_.size(); i++) {
        res->emplace(deep ? data_[i]->clone(true) : data_[i], i);
    }
    return res;
}

const string ArrayData::toString() const {
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

void ArrayData::print(std::ostream &os) const { os << toString(); }

data_ptr_t ArrayData::convertToParams(const std::shared_ptr<ParamsType> &target) {
    auto params = make_shared<ParamsData>();
    for (const auto &e : data_) {
        params->emplace(e);
    }
    return params->convertToParams(target);
}
