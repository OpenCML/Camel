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

#include "struct.h"
#include "utils/scope.h"
#include "utils/str.h"

#include "../special/any.h"
#include "../special/null.h"
#include "../special/ref.h"

using namespace std;

StructData::StructData() : ComposedData(std::make_shared<StructType>()) {}

StructData::StructData(initializer_list<pair<string, data_ptr_t>> data)
    : ComposedData(make_shared<StructType>()) {
    StructType &structType = *static_cast<StructType *>(type_.get());
    for (const auto &e : data) {
        auto &[key, val] = e;
        data_[key] = val;
        structType.add(key, val->type());
        if (val->type()->code() == TypeCode::Ref) {
            refIndices_.push_back(key);
        }
    }
}

StructData::StructData(std::unordered_map<std::string, data_ptr_t> &&data)
    : ComposedData(make_shared<StructType>()), data_(std::move(data)) {}

bool StructData::emplace(const std::string &key, const data_ptr_t &val) {
    StructType &structType = *static_cast<StructType *>(type_.get());
    if (structType.add(key, val->type())) {
        data_[key] = val;
        if (val->type()->code() == TypeCode::Ref) {
            refIndices_.push_back(key);
        }
        return true;
    }
    return false;
}

bool StructData::add(const string &key, const data_ptr_t &val) {
    ASSERT(resolved(), "Cannot add data to unresolved StructData");
    StructType &structType = *static_cast<StructType *>(type_.get());
    if (structType.add(key, val->type())) {
        data_[key] = val;
        return true;
    }
    return false;
}

bool StructData::del(const string &key) {
    ASSERT(resolved(), "Cannot delete data from unresolved StructData");
    StructType &structType = *static_cast<StructType *>(type_.get());
    if (structType.del(key)) {
        data_.erase(key);
        return true;
    }
    return false;
}

bool StructData::has(const string &key) const {
    ASSERT(resolved(), "Cannot check data from unresolved StructData");
    return data_.find(key) != data_.end();
}

void StructData::set(const string &key, const data_ptr_t &val) {
    ASSERT(resolved(), "Cannot set data to unresolved StructData");
    StructType &structType = *static_cast<StructType *>(type_.get());
    structType.set(key, val->type());
    data_[key] = val;
}

data_ptr_t StructData::get(const string &key) const {
    ASSERT(resolved(), "Cannot get data from unresolved StructData");
    auto it = data_.find(key);
    if (it != data_.end()) {
        return it->second;
    }
    return nullptr;
}

bool StructData::equals(const data_ptr_t &other) const {
    if (other == nullptr || other->type()->code() != TypeCode::Struct) {
        return false;
    }
    auto o = tt::as_shared<StructData>(other);
    if (data_.size() != o->data_.size()) {
        return false;
    }
    for (const auto &[k, v] : data_) {
        auto it = o->data_.find(k);
        if (it == o->data_.end() || !v->equals(it->second)) {
            return false;
        }
    }
    return true;
}

data_ptr_t StructData::convert(type_ptr_t target, bool inplace) {
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

vector<string> StructData::refs() const {
    vector<string> res;
    res.reserve(refIndices_.size());
    for (const auto &e : refIndices_) {
        const auto &refData = tt::as_shared<RefData>(data_.at(e));
        res.push_back(refData->ref());
    }
    return res;
}

void StructData::resolve(const data_vec_t &dataList) {
    if (refIndices_.empty()) {
        return;
    }
    ASSERT(refIndices_.size() == dataList.size(), "DataList size mismatch");
    StructType &structType = *static_cast<StructType *>(type_.get());
    for (size_t i = 0; i < refIndices_.size(); i++) {
        const string &key = refIndices_[i];
        data_ptr_t data = dataList[i];
        data_[key] = data;
        structType.set(key, data->type());
    }
    refIndices_.clear();
}

data_ptr_t StructData::clone(bool deep) const {
    auto dict = make_shared<StructData>();
    for (const auto &[key, val] : data_) {
        dict->emplace(key, deep ? val->clone(true) : val);
    }
    return dict;
}

const string StructData::toString() const {
    if (data_.size() == 0) {
        return "{}";
    }
    string str = "{ ";
    str += strutil::join(data_, ", ", [](const pair<const string, data_ptr_t> &e) {
        return e.first + ": " + e.second->toString();
    });
    str += " }";
    return str;
}

void StructData::print(std::ostream &os) const { os << toString(); }
