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

#include "params.h"
#include "list.h"
#include "map.h"
#include "tuple.h"
#include "utils/log.h"

#include "../other/any.h"
#include "../other/null.h"
#include "../other/ref.h"
#include "../other/string.h"

using namespace std;

ParamsData::ParamsData() : StructData(std::make_shared<ParamsType>()) {}

bool ParamsData::emplace(const data_ptr_t &val, const std::string &key) {
    ParamsType &paramsType = *static_cast<ParamsType *>(type_.get());
    indexData_.push_back(val);
    if (!key.empty()) {
        namedData_[key] = val;
    }
    paramsType.add(key, val->type());
    if (val->type()->code() == TypeCode::REF) {
        refs_.emplace_back(indexData_.size() - 1, key);
    }
    return true;
}

vector<string> ParamsData::refs() const {
    vector<string> res;
    for (const auto &[idx, _] : refs_) {
        const data_ptr_t &ref = indexData_[idx];
        res.push_back(dynamic_pointer_cast<RefData>(ref)->ref());
    }
    return res;
}

void ParamsData::resolve(const data_vec_t &dataList) {
    if (refs_.empty()) {
        return;
    }
    ASSERT(refs_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < refs_.size(); i++) {
        const auto &[idx, key] = refs_[i];
        indexData_[idx] = dataList[i];
        if (!key.empty()) {
            namedData_[key] = dataList[i];
        }
    }
    refs_.clear();
}

bool ParamsData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for ParamsData
    return true;
}

data_ptr_t ParamsData::convert(type_ptr_t target, bool inplace) {
    if (target == type_ || type_->equals(target)) {
        // same type, no need to convert
        return shared_from_this();
    }
    try {
        if (target->structured()) {
            switch (target->code()) {
            case TypeCode::MAP:
                return convertToMap();
                break;
            case TypeCode::LIST:
                return convertToList();
                break;
            case TypeCode::TUPLE:
                return convertToTuple();
                break;
            case TypeCode::PARAMS:
                return convertToParams(dynamic_pointer_cast<ParamsType>(target), inplace);
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

data_ptr_t ParamsData::clone(bool deep) const {
    auto params = make_shared<ParamsData>();
    params->type_ = type_;
    for (const auto &e : indexData_) {
        params->indexData_.push_back(deep ? e->clone(deep) : e);
    }
    for (const auto &e : namedData_) {
        params->namedData_[e.first] = deep ? e.second->clone(deep) : e.second;
    }
    return params;
}

const string ParamsData::toString() const {
    string str = "(";
    for (const auto &e : indexData_) {
        str += e->toString() + ", ";
    }
    for (const auto &e : namedData_) {
        str += e.first + ": " + e.second->toString() + ", ";
    }
    if (str.length() > 1) {
        str.pop_back();
        str.pop_back();
    }
    str += ")";
    return str;
}

data_ptr_t ParamsData::convertToMap() {
    auto mapData = make_shared<MapData>(stringTypePtr, anyTypePtr);
    for (const auto &e : namedData_) {
        const auto &key = dynamic_pointer_cast<Data>(make_shared<StringData>(e.first));
        const auto &val = e.second->convert(anyTypePtr);
        mapData->emplace(key, val);
    }
    return mapData;
}

data_ptr_t ParamsData::convertToList() {
    auto listData = make_shared<ListData>();
    for (const auto &e : indexData_) {
        listData->emplace(e);
    }
    return listData;
}

data_ptr_t ParamsData::convertToTuple() {
    auto tupleData = make_shared<TupleData>();
    for (const auto &e : indexData_) {
        tupleData->emplace(e);
    }
    return tupleData;
}

data_ptr_t ParamsData::convertToParams(const shared_ptr<ParamsType> &other, bool inplace) {
    const auto &typeList = other->elements();
    vector<pair<size_t, string>> refs;
    vector<data_ptr_t> indexData;
    map<string, data_ptr_t> namedData;
    for (size_t i = 0; i < typeList.size(); i++) {
        const auto &[key, _, value] = typeList[i];
        data_ptr_t val = value;
        if (namedData_.find(key) != namedData_.end()) {
            val = namedData_[key];
        } else if (i < indexData_.size()) {
            val = indexData_[i];
        }
        if (!val) {
            throw DataConvError("Missing value for key " + key);
        }
        // TODO: need to check type compatibility
        // notice that we cannot check type compatibility here, because the ref of val may not be resolved yet
        indexData.push_back(val);
        if (!key.empty()) {
            namedData[key] = val;
        }
        if (val->type()->code() == TypeCode::REF) {
            refs.emplace_back(i, key);
        }
    }
    if (inplace) {
        type_ = other;
        refs_ = std::move(refs);
        indexData_ = std::move(indexData);
        namedData_ = std::move(namedData);
        return shared_from_this();
    } else {
        auto params = make_shared<ParamsData>();
        params->type_ = other;
        params->refs_ = std::move(refs);
        params->indexData_ = std::move(indexData);
        params->namedData_ = std::move(namedData);
        return params;
    }
}
