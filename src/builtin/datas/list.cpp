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
 * Updated: Oct. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "list.h"
#include "utils/scope.h"

#include "core/data/special/any.h"
#include "core/data/special/null.h"
#include "core/data/special/ref.h"

using namespace std;

ListData::ListData() : OtherData(std::make_shared<ListType>()), data_() {}

ListData::ListData(data_list_t data) : OtherData(std::make_shared<ListType>()), data_(data) {
    size_t i = 0;
    for (const auto &e : data) {
        if (e->type()->code() == TypeCode::Ref) {
            refIndices_.push_back(i);
        }
        i++;
    }
}

ListData::ListData(data_vec_t &&data)
    : OtherData(std::make_shared<ListType>()), data_(std::move(data)) {}

void ListData::emplace(const data_ptr_t &e) {
    data_.push_back(e);
    if (e->type()->code() == TypeCode::Ref) {
        refIndices_.push_back(data_.size() - 1);
    }
}

void ListData::pushBack(const data_ptr_t &e) {
    ASSERT(resolved(), "Cannot push data to unresolved ListData");
    data_.push_back(e);
}

data_ptr_t ListData::popBack() {
    ASSERT(resolved(), "Cannot pop data from unresolved ListData");
    if (data_.empty()) {
        return nullptr;
    }
    data_ptr_t back = data_.back();
    data_.pop_back();
    return back;
}

data_ptr_t ListData::get(size_t index) const {
    ASSERT(resolved(), "Cannot get data from unresolved ListData");
    if (index >= data_.size()) {
        return nullptr;
    }
    return data_[index];
}

bool ListData::set(size_t index, const data_ptr_t &e) {
    ASSERT(resolved(), "Cannot set data to unresolved ListData");
    if (index >= data_.size()) {
        return false;
    }
    data_[index] = e;
    return true;
}

bool ListData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for ListData
    return true;
}

data_ptr_t ListData::convert(type_ptr_t target, bool inplace) {
    if (target == type_ || type_->equals(target)) {
        // same type, no need to convert
        return shared_from_this();
    }
    try {
        if (target->composed()) {
            switch (target->code()) {
            // TODO: implement conversion to other composed types
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

vector<string> ListData::refs() const {
    vector<string> res;
    res.reserve(refIndices_.size());
    for (const auto &idx : refIndices_) {
        data_ptr_t ref = data_[idx];
        res.push_back(tt::as_shared<RefData>(ref)->ref());
    }
    return res;
}

void ListData::resolve(const data_vec_t &dataList) {
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

data_ptr_t ListData::clone(bool deep) const {
    auto res = make_shared<ListData>();
    for (const auto &e : data_) {
        res->emplace(e->clone(deep));
    }
    return res;
}

const string ListData::toString() const {
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

void ListData::print(std::ostream &os) const { os << toString(); }
