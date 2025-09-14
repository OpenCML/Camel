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

#include "tuple.h"
#include "params.h"
#include "utils/scope.h"

#include "../other/any.h"
#include "../other/null.h"
#include "../other/ref.h"

using namespace std;

TupleData::TupleData(data_list_t data) : data_(data) {
    std::vector<type_ptr_t> types;
    size_t i = 0;
    for (const auto &e : data) {
        types.push_back(e->type());
        if (e->type()->code() == TypeCode::Ref) {
            refs_.push_back(i);
        }
        i++;
    }
    type_ = std::make_shared<TupleType>(types);
}

void TupleData::emplace(const data_ptr_t &e) {
    data_.push_back(e);
    TupleType &tupleType = *static_cast<TupleType *>(type_.get());
    tupleType.add(e->type());
    if (e->type()->code() == TypeCode::Ref) {
        refs_.push_back(data_.size() - 1);
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

data_ptr_t TupleData::as(type_ptr_t target, bool inplace) {
    if (target == type_ || type_->equals(target)) {
        // same type, no need to convert
        return shared_from_this();
    }
    try {
        if (target->structured()) {
            switch (target->code()) {
                // TODO: implement conversion to other structured types
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

vector<string> TupleData::refs() const {
    vector<string> res;
    for (const auto &idx : refs_) {
        data_ptr_t ref = data_[idx];
        res.push_back(dynamic_pointer_cast<RefData>(ref)->ref());
    }
    return res;
}

void TupleData::resolve(const data_vec_t &dataList) {
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

data_ptr_t TupleData::clone(bool deep) const {
    auto tuple = make_shared<TupleData>();
    tuple->type_ = type_;
    for (const auto &e : data_) {
        tuple->emplace(deep ? e->clone(deep) : e);
    }
    return tuple;
}

const string TupleData::toString() const {
    string str = "(";
    for (const auto &e : data_) {
        str += e->toString() + ", ";
    }
    if (data_.size() > 0) {
        str.pop_back();
        str.pop_back();
    }
    str += ")";
    return str;
}

void TupleData::print(std::ostream &os) const { os << toString(); }

data_ptr_t TupleData::convertToParams(const std::shared_ptr<ParamsType> &target) {
    auto params = make_shared<ParamsData>();
    for (const auto &e : data_) {
        params->emplace(e);
    }
    return params->convertToParams(target);
}
