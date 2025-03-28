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

#include "set.h"
#include "utils/log.h"

#include "../other/any.h"
#include "../other/null.h"
#include "../other/ref.h"

using namespace std;

SetData::SetData(type_ptr_t elType) : StructData(std::make_shared<SetType>(elType)) {}

SetData::SetData(type_ptr_t elType, data_list_t data) : StructData(std::make_shared<SetType>(elType)), data_(data) {
    for (const auto &e : data) {
        if (e->type()->code() == TypeCode::REF) {
            refs_.push_back(e);
        }
    }
}

bool SetData::emplace(const data_ptr_t &e) {
    bool res = data_.insert(e).second;
    if (res && e->type()->code() == TypeCode::REF) {
        refs_.push_back(e);
    }
    return res;
}

bool SetData::add(const data_ptr_t &e) {
    cml_assert(resolved(), "Cannot add data to unresolved SetData");
    return data_.insert(e).second;
}

bool SetData::del(const data_ptr_t &e) {
    cml_assert(resolved(), "Cannot delete data from unresolved SetData");
    return data_.erase(e) > 0;
}

bool SetData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for SetData
    return true;
}

data_ptr_t SetData::convert(type_ptr_t target, bool inplace) {
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

vector<string> SetData::refs() const {
    vector<string> res;
    for (const auto &ref : refs_) {
        res.push_back(dynamic_pointer_cast<RefData>(ref)->ref());
    }
    return res;
}

void SetData::resolve(const data_vec_t &dataList) {
    if (refs_.empty()) {
        return;
    }
    cml_assert(refs_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < refs_.size(); i++) {
        data_ptr_t ref = refs_[i];
        data_ptr_t data = dataList[i];
        data_.erase(ref);
        data_.insert(data);
    }
    refs_.clear();
}

data_ptr_t SetData::clone(bool deep) const {
    auto set = make_shared<SetData>(type_);
    for (const auto &e : data_) {
        set->emplace(deep ? e->clone(deep) : e);
    }
    return set;
}

const string SetData::toString() const {
    if (data_.empty()) {
        return "{}";
    }
    string str = "{ ";
    for (const auto &e : data_) {
        str += e->toString() + ", ";
    }
    str.pop_back();
    str.pop_back();
    str += " }";
    return str;
}
