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
 * Updated: Oct. 07, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "list.h"
#include "params.h"
#include "utils/log.h"

#include "../other/any.h"
#include "../other/null.h"
#include "../other/ref.h"

using namespace std;

ListData::ListData() : StructData(listTypePtr), data_() {}

ListData::ListData(data_list_t data) : StructData(listTypePtr), data_(data) {
    size_t i = 0;
    for (const auto &e : data) {
        if (e->type()->code() == TypeCode::REF) {
            refs_.push_back(i);
        }
        i++;
    }
}

void ListData::emplace(const data_ptr_t &e) {
    data_.push_back(e);
    if (e->type()->code() == TypeCode::REF) {
        refs_.push_back(data_.size() - 1);
    }
}

void ListData::pushBack(const data_ptr_t &e) {
    assert(resolved(), "Cannot push data to unresolved ListData");
    data_.push_back(e);
}

data_ptr_t ListData::popBack() {
    assert(resolved(), "Cannot pop data from unresolved ListData");
    if (data_.empty()) {
        return nullptr;
    }
    data_ptr_t back = data_.back();
    data_.pop_back();
    return back;
}

data_ptr_t ListData::get(size_t index) const {
    assert(resolved(), "Cannot get data from unresolved ListData");
    if (index >= data_.size()) {
        return nullptr;
    }
    return data_[index];
}

bool ListData::set(size_t index, const data_ptr_t &e) {
    assert(resolved(), "Cannot set data to unresolved ListData");
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

vector<string> ListData::refs() const {
    vector<string> res;
    for (const auto &idx : refs_) {
        data_ptr_t ref = data_[idx];
        res.push_back(dynamic_pointer_cast<RefData>(ref)->ref());
    }
    return res;
}

void ListData::resolve(const data_vec_t &dataList) {
    if (refs_.empty()) {
        return;
    }
    assert(refs_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < refs_.size(); i++) {
        size_t idx = refs_[i];
        data_[idx] = dataList[i];
    }
    refs_.clear();
}

data_ptr_t ListData::clone(bool) const {
    vector<data_ptr_t> cloned;
    for (const auto &e : data_) {
        cloned.push_back(e);
    }
    return make_shared<ListData>(cloned);
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

data_ptr_t ListData::convertToParams(std::shared_ptr<ParamsType> &target) {
    auto params = make_shared<ParamsData>();
    for (const auto &e : data_) {
        params->emplace(e);
    }
    return params->convertToParams(target);
}
