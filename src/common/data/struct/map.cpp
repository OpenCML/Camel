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

#include "map.h"
#include "utils/log.h"

#include "../other/any.h"
#include "../other/null.h"
#include "../other/ref.h"

using namespace std;

MapData::MapData(type_ptr_t keyType, type_ptr_t dataType) : StructData(std::make_shared<MapType>(keyType, dataType)) {}

bool MapData::emplace(const data_ptr_t &key, const data_ptr_t &val) {
    bool res = data_.insert(std::make_pair(key, val)).second;
    if (res && key->type()->code() == TypeCode::REF) {
        refs_.push_back(std::make_pair(key, true));
    }
    if (res && val->type()->code() == TypeCode::REF) {
        refs_.push_back(std::make_pair(key, false));
    }
    return res;
}

bool MapData::set(const data_ptr_t &key, const data_ptr_t &val) {
    assert(resolved(), "Cannot set data to unresolved MapData");
    return data_.insert(std::make_pair(key, val)).second;
}

bool MapData::del(const data_ptr_t &key) {
    assert(resolved(), "Cannot delete data from unresolved MapData");
    return data_.erase(key) > 0;
}

data_ptr_t MapData::get(const data_ptr_t &key) const {
    assert(resolved(), "Cannot get data from unresolved MapData");
    auto it = data_.find(key);
    if (it == data_.end()) {
        return nullptr;
    }
    return it->second;
}

bool MapData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for SetData
    return true;
}

data_ptr_t MapData::convert(type_ptr_t target, bool inplace) {
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

vector<string> MapData::refs() const {
    vector<string> res;
    for (const auto &[ref, isKey] : refs_) {
        if (isKey) {
            res.push_back(dynamic_pointer_cast<RefData>(ref)->ref());
        } else {
            res.push_back(dynamic_pointer_cast<RefData>(data_.at(ref))->ref());
        }
    }
    return res;
}

void MapData::resolve(const data_vec_t &dataList) {
    if (refs_.empty()) {
        return;
    }
    assert(refs_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < refs_.size(); i++) {
        auto &[ref, isKey] = refs_[i];
        data_ptr_t data = dataList[i];
        if (isKey) {
            data_.insert(std::make_pair(data, data_.at(ref)));
            data_.erase(ref);
        } else {
            data_.at(ref) = data;
        }
    }
    refs_.clear();
}

data_ptr_t MapData::clone(bool deep) const { throw runtime_error("Not implemented"); }

const string MapData::toString() const {
    if (data_.empty()) {
        return "{}";
    }
    string str = "{ ";
    for (const auto &[k, V] : data_) {
        str += "[" + k->toString() + "]: " + V->toString() + ", ";
    }
    str.resize(str.size() - 2);
    str += " }";
    return str;
}
