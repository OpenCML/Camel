/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 5, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "data.h"
#include "entity.h"

#include "log.h"

using namespace std;

/*
SetData
*/

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
    assert(resolved(), "Cannot add data to unresolved SetData");
    return data_.insert(e).second;
}

bool SetData::del(const data_ptr_t &e) {
    assert(resolved(), "Cannot delete data from unresolved SetData");
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
    if (target->structured()) {
        switch (target->code()) {
        case TypeCode::SET:
            /* code */
            break;

        default:
            break;
        }
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
    assert(refs_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < refs_.size(); i++) {
        data_ptr_t ref = refs_[i];
        data_ptr_t data = dataList[i];
        data_.erase(ref);
        data_.insert(data);
    }
    refs_.clear();
}

data_ptr_t SetData::clone(bool deep) const {
    unordered_set<data_ptr_t> cloned;
    for (const auto &e : data_) {
        cloned.insert(e);
    }
    return make_shared<SetData>(type_, cloned);
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

/*
MapData
*/

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

/*
DictData
*/

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
    assert(resolved(), "Cannot add data to unresolved DictData");
    DictType &dictType = *static_cast<DictType *>(type_.get());
    if (dictType.add(key, val->type())) {
        data_[key] = val;
        return true;
    }
    return false;
}

bool DictData::del(const string &key) {
    assert(resolved(), "Cannot delete data from unresolved DictData");
    DictType &dictType = *static_cast<DictType *>(type_.get());
    if (dictType.del(key)) {
        data_.erase(key);
        return true;
    }
    return false;
}

bool DictData::has(const string &key) const {
    assert(resolved(), "Cannot check data from unresolved DictData");
    return data_.find(key) != data_.end();
}

void DictData::set(const string &key, const data_ptr_t &val) {
    assert(resolved(), "Cannot set data to unresolved DictData");
    DictType &dictType = *static_cast<DictType *>(type_.get());
    dictType.set(key, val->type());
    data_[key] = val;
}

data_ptr_t DictData::get(const string &key) const {
    assert(resolved(), "Cannot get data from unresolved DictData");
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
    // TODO
    if (target == type_ || type_->equals(target)) {
        // same type, no need to convert
        return shared_from_this();
    }
    if (target->structured()) {
        switch (target->code()) {
        case TypeCode::SET:
            /* code */
            break;

        default:
            break;
        }
    }
    throw DataConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

vector<string> DictData::refs() const { return refs_; }

void DictData::resolve(const data_vec_t &dataList) {
    if (refs_.empty()) {
        return;
    }
    assert(refs_.size() == dataList.size(), "DataList size mismatch");
    DictType &dictType = *static_cast<DictType *>(type_.get());
    for (size_t i = 0; i < refs_.size(); i++) {
        const string &key = refs_[i];
        data_ptr_t data = dataList[i];
        data_[key] = data;
        dictType.set(key, data->type());
    }
    refs_.clear();
}

data_ptr_t DictData::clone(bool deep) const { return make_shared<DictData>(data_); }

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

/*
ListData
*/

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
    // TODO
    if (target == type_ || type_->equals(target)) {
        // same type, no need to convert
        return shared_from_this();
    }
    if (target->structured()) {
        switch (target->code()) {
        case TypeCode::SET:
            /* code */
            break;

        default:
            break;
        }
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

/*
ArrayData
*/

ArrayData::ArrayData(type_ptr_t type, size_t length, data_list_t data) : data_(data) {
    size_t i = 0;
    for (const auto &e : data) {
        if (e->type()->code() == TypeCode::REF) {
            refs_.push_back(i);
        } else if (e->type()->convertibility(*type) != TypeConv::SAFE) {
            throw DataConvError("Cannot convert " + e->type()->toString() + " to " + type->toString());
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
    if (e->type()->code() == TypeCode::REF) {
        refs_.push_back(index);
    }
    return true;
}

data_ptr_t ArrayData::get(size_t index) const {
    assert(resolved(), "Cannot get data from unresolved ArrayData");
    if (index >= data_.size()) {
        return nullptr;
    }
    return data_[index];
}

bool ArrayData::set(size_t index, const data_ptr_t &e) {
    assert(resolved(), "Cannot set data to unresolved ArrayData");
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

data_ptr_t ArrayData::convert(type_ptr_t target, bool inplace) {
    // TODO
    throw DataConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
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
    assert(refs_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < refs_.size(); i++) {
        size_t idx = refs_[i];
        // TODO: need to check type compatibility
        data_[idx] = dataList[i];
    }
    refs_.clear();
}

data_ptr_t ArrayData::clone(bool) const {
    vector<data_ptr_t> cloned;
    for (const auto &e : data_) {
        cloned.push_back(e);
    }
    return make_shared<ArrayData>(dynamic_pointer_cast<ArrayType>(type_), cloned);
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

/*
TupleData
*/

TupleData::TupleData(data_list_t data) : data_(data) {
    std::vector<type_ptr_t> types;
    size_t i = 0;
    for (const auto &e : data) {
        types.push_back(e->type());
        if (e->type()->code() == TypeCode::REF) {
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
    if (e->type()->code() == TypeCode::REF) {
        refs_.push_back(data_.size() - 1);
    }
}

data_ptr_t TupleData::get(size_t index) const {
    assert(resolved(), "Cannot get data from unresolved TupleData");
    if (index >= data_.size()) {
        return nullptr;
    }
    return data_[index];
}

bool TupleData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for ListData
    return true;
}

data_ptr_t TupleData::convert(type_ptr_t target, bool inplace) {
    // TODO
    throw DataConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
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
    assert(refs_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < refs_.size(); i++) {
        size_t idx = refs_[i];
        data_[idx] = dataList[i];
    }
    refs_.clear();
}

data_ptr_t TupleData::clone(bool) const {
    vector<data_ptr_t> cloned;
    for (const auto &e : data_) {
        cloned.push_back(e);
    }
    return make_shared<TupleData>(cloned);
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

/*
VectorData
*/

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
    assert(resolved(), "Cannot push data to unresolved VectorData");
    if (e->type()->convertibility(*type_) != TypeConv::SAFE) {
        throw DataConvError("Cannot convert " + e->type()->toString() + " to " + type_->toString());
    }
    data_.push_back(e);
}

data_ptr_t VectorData::popBack() {
    assert(resolved(), "Cannot pop data from unresolved VectorData");
    if (data_.empty()) {
        return nullptr;
    }
    data_ptr_t back = data_.back();
    data_.pop_back();
    return back;
}

data_ptr_t VectorData::get(size_t index) const {
    assert(resolved(), "Cannot get data from unresolved VectorData");
    if (index >= data_.size()) {
        return nullptr;
    }
    return data_[index];
}

bool VectorData::set(size_t index, const data_ptr_t &e) {
    assert(resolved(), "Cannot set data to unresolved VectorData");
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
    // TODO
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
    assert(refs_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < refs_.size(); i++) {
        size_t idx = refs_[i];
        data_[idx] = dataList[i];
    }
    refs_.clear();
}

data_ptr_t VectorData::clone(bool) const {
    vector<data_ptr_t> cloned;
    for (const auto &e : data_) {
        cloned.push_back(e);
    }
    return make_shared<VectorData>(dynamic_pointer_cast<VectorType>(type_), cloned);
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

/*
ParamsData
*/

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
    assert(refs_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < refs_.size(); i++) {
        const auto &[idx, key] = refs_[i];
        indexData_[idx] = dataList[i];
        if (!key.empty()) {
            namedData_[key] = dataList[i];
        }
    }
    refs_.clear();
}

data_ptr_t ParamsData::convertToParams(shared_ptr<ParamsType> &other, bool inplace) {
    const auto &typeList = other->elements();
    vector<pair<size_t, string>> refs;
    vector<data_ptr_t> indexData;
    map<string, data_ptr_t> namedData;
    for (size_t i = 0; i < typeList.size(); i++) {
        const auto &[key, valType, defaultData] = typeList[i];
        data_ptr_t val = defaultData;
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
    }
}

bool ParamsData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for ParamsData
    return true;
}

data_ptr_t ParamsData::convert(type_ptr_t target, bool inplace) {
    // TODO
    if (target == type_ || type_->equals(target)) {
        // same type, no need to convert
        return shared_from_this();
    }
    if (target->structured()) {
        switch (target->code()) {
        case TypeCode::SET:
            /* code */
            break;

        default:
            break;
        }
    }
    throw DataConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

data_ptr_t ParamsData::clone(bool) const { return make_shared<ParamsData>(indexData_, namedData_); }

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

/*
TensorData
*/

TensorData::TensorData(const type_ptr_t &elementType, const std::vector<size_t> &shape)
    : StructData(std::make_shared<TensorType>(elementType, shape)) {}

data_ptr_t TensorData::at(const std::vector<size_t> &index) const {
    // TODO
    throw runtime_error("Not implemented");
}

std::vector<std::string> TensorData::refs() const {
    // TODO
    throw runtime_error("Not implemented");
}

void TensorData::resolve(const data_vec_t &dataList) {
    // TODO
    throw runtime_error("Not implemented");
}

bool TensorData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for VectorData
    return true;
}

data_ptr_t TensorData::convert(type_ptr_t target, bool inplace) {
    // TODO
    throw DataConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

data_ptr_t TensorData::clone(bool) const { throw runtime_error("Not implemented"); }

const string TensorData::toString() const { return string("TensorData"); }
