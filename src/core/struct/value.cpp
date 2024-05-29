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

#include "value.h"
#include "entity.h"

/*
SetValue
*/

bool SetValue::resolved() {
    if (resolved_) {
        return true;
    }
    for (const auto &e : data_) {
        if (!e->resolved()) {
            return false;
        }
    }
    resolved_ = true;
    return true;
}

const value_ptr_t SetValue::convert(type_ptr_t target, bool inplace) {
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
    throw ValueConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

const value_ptr_t SetValue::clone(bool deep) const {
    std::set<entity_ptr_t> cloned;
    for (const auto &e : data_) {
        cloned.insert(e);
    }
    return std::make_shared<SetValue>(type_, cloned);
}

const std::string SetValue::toString() const {
    std::string str = "{";
    for (const auto &e : data_) {
        str += e->dataStr() + ", ";
    }
    str += "}";
    return str;
}

/*
ListValue
*/

bool ListValue::resolved() {
    if (resolved_) {
        return true;
    }
    for (const auto &e : data_) {
        if (!e->resolved()) {
            return false;
        }
    }
    resolved_ = true;
    return true;
}

const value_ptr_t ListValue::convert(type_ptr_t target, bool inplace) {
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
    throw ValueConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

const value_ptr_t ListValue::clone(bool) const {
    std::vector<entity_ptr_t> cloned;
    for (const auto &e : data_) {
        cloned.push_back(e);
    }
    return std::make_shared<ListValue>(cloned);
}

const std::string ListValue::toString() const {
    std::string str = "[";
    for (const auto &e : data_) {
        str += e->dataStr() + ", ";
    }
    str.pop_back();
    str.pop_back();
    str += "]";
    return str;
}

/*
DictValue
*/

DictValue::DictValue(std::initializer_list<std::pair<std::string, entity_ptr_t>> data)
    : StructValue(std::make_shared<DictType>()) {
    DictType &dictType = *static_cast<DictType *>(type_.get());
    for (const auto &e : data) {
        data_[e.first] = e.second;
        dictType.add(e.first, e.second->type());
    }
}

DictValue::DictValue(const std::unordered_map<std::string, entity_ptr_t> &data)
    : StructValue(std::make_shared<DictType>()), data_(data) {
    DictType &dictType = *static_cast<DictType *>(type_.get());
    for (const auto &e : data) {
        dictType.add(e.first, e.second->type());
    }
}

bool DictValue::add(const std::string &key, const entity_ptr_t &e) {
    DictType &dictType = *static_cast<DictType *>(type_.get());
    if (dictType.add(key, e->type())) {
        data_[key] = e;
        return true;
    }
    return false;
}

bool DictValue::del(const std::string &key) {
    DictType &dictType = *static_cast<DictType *>(type_.get());
    if (dictType.del(key)) {
        data_.erase(key);
        return true;
    }
    return false;
}

bool DictValue::has(const std::string &key) const { return data_.find(key) != data_.end(); }

void DictValue::set(const std::string &key, const entity_ptr_t &e) {
    DictType &dictType = *static_cast<DictType *>(type_.get());
    dictType.set(key, e->type());
    data_[key] = e;
}

entity_ptr_t DictValue::get(const std::string &key) const {
    auto it = data_.find(key);
    if (it != data_.end()) {
        return it->second;
    }
    return nullptr;
}

bool DictValue::resolved() {
    if (resolved_) {
        return true;
    }
    for (const auto &e : data_) {
        if (!e.second->resolved()) {
            return false;
        }
    }
    resolved_ = true;
    return true;
}

const value_ptr_t DictValue::convert(type_ptr_t target, bool inplace) {
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
    throw ValueConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

const value_ptr_t DictValue::clone(bool deep) const { return std::make_shared<DictValue>(data_); }

const std::string DictValue::toString() const {
    std::string str = "{";
    for (const auto &e : data_) {
        str += e.first + ": " + e.second->dataStr() + ", ";
    }
    str += "}";
    return str;
}

/*
NamedTupleValue
*/

bool NamedTupleValue::setType(type_ptr_t type) {
    if (typeResolved_ || type->code() != TypeCode::NAMED_TUPLE) {
        return false;
    }
    const auto &typeList = static_cast<NamedTupleType *>(type.get())->elements();
    if (indexData_.size() + namedData_.size() != typeList.size()) {
        return false;
    }
    size_t idx = 0;
    std::vector<entity_ptr_t> indexResult;
    std::unordered_map<std::string, entity_ptr_t> namedResult;
    for (size_t i = 0; i < typeList.size(); i++) {
        const auto &[key, typ, value] = typeList[i];
        // TODO: unused value
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
}

bool NamedTupleValue::add(const entity_ptr_t &e, const std::string &key) {
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

bool NamedTupleValue::resolved() {
    if (resolved_) {
        return true;
    }
    for (const auto &e : indexData_) {
        if (!e->resolved()) {
            return false;
        }
    }
    for (const auto &e : namedData_) {
        if (!e.second->resolved()) {
            return false;
        }
    }
    resolved_ = true;
    return true;
}

const value_ptr_t NamedTupleValue::convert(type_ptr_t target, bool inplace) {
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
    throw ValueConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

const value_ptr_t NamedTupleValue::clone(bool) const {
    return std::make_shared<NamedTupleValue>(indexData_, namedData_);
}

const std::string NamedTupleValue::toString() const {
    return "NamedTupleValue"; // TODO
    std::string str = "(";
    for (const auto &e : indexData_) {
        str += e->dataStr() + ", ";
    }
    for (const auto &e : namedData_) {
        str += e.first + ": " + e.second->dataStr() + ", ";
    }
    str.pop_back();
    str.pop_back();
    str += ")";
    return str;
}
