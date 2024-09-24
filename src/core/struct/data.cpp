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

/*
SetData
*/

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

data_ptr_t SetData::clone(bool deep) const {
    std::unordered_set<data_ptr_t> cloned;
    for (const auto &e : data_) {
        cloned.insert(e);
    }
    return std::make_shared<SetData>(type_, cloned);
}

const std::string SetData::toString() const {
    if (data_.empty()) {
        return "{}";
    }
    std::string str = "{ ";
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

bool MapData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for SetData
    return true;
}

data_ptr_t MapData::convert(type_ptr_t target, bool inplace) {
    throw DataConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

data_ptr_t MapData::clone(bool deep) const { throw std::runtime_error("Not implemented"); }

const std::string MapData::toString() const {
    if (data_.empty()) {
        return "{}";
    }
    std::string str = "{ ";
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

bool DictData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for DictData
    return true;
}

DictData::DictData(std::initializer_list<std::pair<std::string, data_ptr_t>> data)
    : StructData(std::make_shared<DictType>()) {
    DictType &dictType = *static_cast<DictType *>(type_.get());
    for (const auto &e : data) {
        data_[e.first] = e.second;
        dictType.add(e.first, e.second->type());
    }
}

DictData::DictData(const std::unordered_map<std::string, data_ptr_t> &data)
    : StructData(std::make_shared<DictType>()), data_(data) {
    DictType &dictType = *static_cast<DictType *>(type_.get());
    for (const auto &e : data) {
        dictType.add(e.first, e.second->type());
    }
}

bool DictData::add(const std::string &key, const data_ptr_t &e) {
    DictType &dictType = *static_cast<DictType *>(type_.get());
    if (dictType.add(key, e->type())) {
        data_[key] = e;
        return true;
    }
    return false;
}

bool DictData::del(const std::string &key) {
    DictType &dictType = *static_cast<DictType *>(type_.get());
    if (dictType.del(key)) {
        data_.erase(key);
        return true;
    }
    return false;
}

bool DictData::has(const std::string &key) const { return data_.find(key) != data_.end(); }

void DictData::set(const std::string &key, const data_ptr_t &e) {
    DictType &dictType = *static_cast<DictType *>(type_.get());
    dictType.set(key, e->type());
    data_[key] = e;
}

data_ptr_t DictData::get(const std::string &key) const {
    auto it = data_.find(key);
    if (it != data_.end()) {
        return it->second;
    }
    return nullptr;
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

data_ptr_t DictData::clone(bool deep) const { return std::make_shared<DictData>(data_); }

const std::string DictData::toString() const {
    if (data_.size() == 0) {
        return "{}";
    }
    std::string str = "{ ";
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

data_ptr_t ListData::clone(bool) const {
    std::vector<data_ptr_t> cloned;
    for (const auto &e : data_) {
        cloned.push_back(e);
    }
    return std::make_shared<ListData>(cloned);
}

const std::string ListData::toString() const {
    std::string str = "[";
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

bool TupleData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for ListData
    return true;
}

data_ptr_t TupleData::convert(type_ptr_t target, bool inplace) {
    // TODO
    throw DataConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

data_ptr_t TupleData::clone(bool) const {
    std::vector<data_ptr_t> cloned;
    for (const auto &e : data_) {
        cloned.push_back(e);
    }
    return std::make_shared<TupleData>(cloned);
}

const std::string TupleData::toString() const {
    std::string str = "(";
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
ArrayData
*/

bool ArrayData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for ArrayData
    return true;
}

data_ptr_t ArrayData::convert(type_ptr_t target, bool inplace) {
    // TODO
    throw DataConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

data_ptr_t ArrayData::clone(bool) const {
    std::vector<data_ptr_t> cloned;
    for (const auto &e : data_) {
        cloned.push_back(e);
    }
    return std::make_shared<ArrayData>(std::dynamic_pointer_cast<ArrayType>(type_), cloned);
}

const std::string ArrayData::toString() const {
    std::string str = "[";
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
VectorData
*/

bool VectorData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for VectorData
    return true;
}

data_ptr_t VectorData::convert(type_ptr_t target, bool inplace) {
    // TODO
    throw DataConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

data_ptr_t VectorData::clone(bool) const {
    std::vector<data_ptr_t> cloned;
    for (const auto &e : data_) {
        cloned.push_back(e);
    }
    return std::make_shared<VectorData>(std::dynamic_pointer_cast<VectorType>(type_), cloned);
}

const std::string VectorData::toString() const {
    std::string str = "[";
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

bool ParamsData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for ParamsData
    return true;
}

bool ParamsData::setType(type_ptr_t type) {
    if (typeResolved_ || type->code() != TypeCode::PARAMS) {
        return false;
    }
    const auto &typeList = static_cast<ParamsType *>(type.get())->elements();
    if (indexData_.size() + namedData_.size() != typeList.size()) {
        return false;
    }
    size_t idx = 0;
    std::vector<data_ptr_t> indexResult;
    std::map<std::string, data_ptr_t> namedResult;
    for (size_t i = 0; i < typeList.size(); i++) {
        const auto &[key, typ, data] = typeList[i];
        // TODO: unused data
        if (namedData_.find(key) != namedData_.end()) {
            if (namedData_[key]->type()->convertibility(*typ) != TypeConv::SAFE) {
                return false;
            }
            indexResult.push_back(namedData_[key]);
            namedResult[key] = namedData_[key];
        } else {
            if (indexData_[idx]->type()->convertibility(*typ) != TypeConv::SAFE) {
                return false;
            }
            indexResult.push_back(indexData_[idx]);
            namedResult[key] = indexData_[idx];
            idx++;
        }
    }
    indexData_ = indexResult;
    namedData_ = namedResult;
    type_ = type;
    typeResolved_ = true;
    return true;
}

bool ParamsData::add(const data_ptr_t &e, const std::string &key) {
    if (typeResolved_) {
        return false;
    }
    if (key.length() > 0) {
        if (namedData_.find(key) != namedData_.end()) {
            return false;
        }
        namedData_[key] = e;
    } else {
        indexData_.push_back(e);
    }
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

data_ptr_t ParamsData::clone(bool) const { return std::make_shared<ParamsData>(indexData_, namedData_); }

const std::string ParamsData::toString() const {
    std::string str = "(";
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

bool TensorData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for VectorData
    return true;
}

data_ptr_t TensorData::convert(type_ptr_t target, bool inplace) {
    // TODO
    throw DataConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

data_ptr_t TensorData::clone(bool) const { throw std::runtime_error("Not implemented"); }

const std::string TensorData::toString() const { return std::string("TensorData"); }
