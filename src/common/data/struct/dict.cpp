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

#include "dict.h"
#include "params.h"
#include "utils/log.h"

#include "../other/any.h"
#include "../other/null.h"

using namespace std;

DictData::DictData() : StructData(std::make_shared<DictType>()) {}

DictData::DictData(initializer_list<pair<string, data_ptr_t>> data) : StructData(make_shared<DictType>()) {
    DictType &dictType = *static_cast<DictType *>(type_.get());
    for (const auto &e : data) {
        auto &[key, val] = e;
        data_[key] = val;
        dictType.add(key, val->type());
        if (val->type()->code() == TypeCode::REF) {
            refs_.push_back(key);
        }
    }
}

bool DictData::emplace(const std::string &key, const data_ptr_t &val) {
    DictType &dictType = *static_cast<DictType *>(type_.get());
    if (dictType.add(key, val->type())) {
        data_[key] = val;
        if (val->type()->code() == TypeCode::REF) {
            refs_.push_back(key);
        }
        return true;
    }
    return false;
}

bool DictData::add(const string &key, const data_ptr_t &val) {
    cml_assert(resolved(), "Cannot add data to unresolved DictData");
    DictType &dictType = *static_cast<DictType *>(type_.get());
    if (dictType.add(key, val->type())) {
        data_[key] = val;
        return true;
    }
    return false;
}

bool DictData::del(const string &key) {
    cml_assert(resolved(), "Cannot delete data from unresolved DictData");
    DictType &dictType = *static_cast<DictType *>(type_.get());
    if (dictType.del(key)) {
        data_.erase(key);
        return true;
    }
    return false;
}

bool DictData::has(const string &key) const {
    cml_assert(resolved(), "Cannot check data from unresolved DictData");
    return data_.find(key) != data_.end();
}

void DictData::set(const string &key, const data_ptr_t &val) {
    cml_assert(resolved(), "Cannot set data to unresolved DictData");
    DictType &dictType = *static_cast<DictType *>(type_.get());
    dictType.set(key, val->type());
    data_[key] = val;
}

data_ptr_t DictData::get(const string &key) const {
    cml_assert(resolved(), "Cannot get data from unresolved DictData");
    auto it = data_.find(key);
    if (it != data_.end()) {
        return it->second;
    }
    return nullptr;
}

bool DictData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for DictData
    return true;
}

data_ptr_t DictData::convert(type_ptr_t target, bool inplace) {
    if (target == type_ || type_->equals(target)) {
        // same type, no need to convert
        return shared_from_this();
    }
    try {
        if (target->structured()) {
            switch (target->code()) {
                // TODO: implement conversion to other structured types
            case TypeCode::PARAMS: {
                auto res = dynamic_pointer_cast<ParamsType>(target);
                return convertToParams(res);
            } break;
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

vector<string> DictData::refs() const { return refs_; }

void DictData::resolve(const data_vec_t &dataList) {
    if (refs_.empty()) {
        return;
    }
    cml_assert(refs_.size() == dataList.size(), "DataList size mismatch");
    DictType &dictType = *static_cast<DictType *>(type_.get());
    for (size_t i = 0; i < refs_.size(); i++) {
        const string &key = refs_[i];
        data_ptr_t data = dataList[i];
        data_[key] = data;
        dictType.set(key, data->type());
    }
    refs_.clear();
}

data_ptr_t DictData::clone(bool deep) const {
    auto dict = make_shared<DictData>();
    for (const auto &[key, val] : data_) {
        dict->emplace(key, deep ? val->clone(true) : val);
    }
    return dict;
}

const string DictData::toString() const {
    if (data_.size() == 0) {
        return "{}";
    }
    string str = "{ ";
    for (const auto &e : data_) {
        str += e.first + ": " + e.second->toString() + ", ";
    }
    str.pop_back();
    str.pop_back();
    str += " }";
    return str;
}

data_ptr_t DictData::convertToParams(const std::shared_ptr<ParamsType> &target) {
    auto params = make_shared<ParamsData>();
    for (const auto &[key, val] : data_) {
        params->emplace(val, key);
    }
    return params->convertToParams(target);
}
