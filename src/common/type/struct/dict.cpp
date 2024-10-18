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
 * Created: Oct. 06, 2024
 * Updated: Oct. 15, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "dict.h"

using namespace std;

DictType::DictType() : StructType(TypeCode::DICT) {}

string DictType::toString() const {
    if (fields_.empty()) {
        return "{}";
    }
    string result = "{ ";
    for (const auto &field : fields_) {
        result += field.first + ": ";
        if (field.second) {
            result += field.second->toString() + ", ";
        } else {
            result += "NULL, ";
        }
    }
    result.pop_back();
    result.pop_back();
    result += " }";
    return result;
}

bool DictType::operator==(const Type &other) const {
    if (other.code() != TypeCode::DICT) {
        return false;
    }
    const DictType &otherDict = dynamic_cast<const DictType &>(other);

    if (fields_.size() != otherDict.fields_.size()) {
        return false;
    }
    for (const auto &field : otherDict.fields_) {
        const auto &ident = field.first;
        const auto &type = field.second;
        if (!fields_.count(ident)) {
            return false;
        }
        const auto &fieldType = fields_.at(ident);
        if (fieldType->code() != type->code()) {
            return false;
        }
    }
    return true;
}

bool DictType::operator!=(const Type &other) const { return !(*this == other); }

bool DictType::add(const string &name, const type_ptr_t &type) {
    if (has(name)) {
        return false;
    }
    fields_[name] = type;
    return true;
}

bool DictType::del(const string &name) { return fields_.erase(name) > 0; }

bool DictType::has(const string &name) const { return fields_.find(name) != fields_.end(); }

void DictType::set(const string &name, const type_ptr_t &type) { fields_.at(name) = type; }

type_ptr_t DictType::get(const string &name) const { return fields_.at(name); }

void DictType::clear() { fields_.clear(); }

type_ptr_t DictType::operator|(const DictType &other) const {
    auto result = make_shared<DictType>();
    for (const auto &field : fields_) {
        result->add(field.first, field.second);
    }
    for (const auto &field : other.fields_) {
        const auto &ident = field.first;
        const auto &type = field.second;
        if (!result->has(ident)) {
            result->add(ident, type);
        } else {
            // if the field already exists, use the rhs type and value
            result->set(ident, type);
        }
    }
    return result;
}

type_ptr_t DictType::operator&(const DictType &other) const {
    auto result = make_shared<DictType>();
    for (const auto &field : fields_) {
        const auto &ident = field.first;
        if (other.has(ident)) {
            const type_ptr_t &otherType = other.get(ident);
            result->add(ident, otherType);
        }
    }
    return result;
}

TypeConv DictType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::DICT: {
            const DictType &otherDict = dynamic_cast<const DictType &>(other);
            TypeConv result = TypeConv::SAFE;
            for (const auto &field : otherDict.fields_) {
                const auto &ident = field.first;
                const auto &type = field.second;
                if (!fields_.count(ident)) {
                    return TypeConv::FORBIDDEN;
                }
                const auto &fieldType = fields_.at(ident);
                const TypeConv fieldConv = fieldType->convertibility(*type);
                if (fieldConv == TypeConv::FORBIDDEN) {
                    return TypeConv::FORBIDDEN;
                }
                if (fieldConv == TypeConv::UNSAFE) {
                    result = TypeConv::UNSAFE;
                }
            }
            return result;
        }
        case TypeCode::SET:
            [[fallthrough]];
        case TypeCode::MAP:
            [[fallthrough]];
        case TypeCode::ARRAY:
            [[fallthrough]];
        case TypeCode::LIST:
            [[fallthrough]];
        case TypeCode::UNION:
            [[fallthrough]];
        case TypeCode::VECTOR:
            [[fallthrough]];
        case TypeCode::TENSOR:
            return TypeConv::FORBIDDEN;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primary types and special types are forbidden
    return TypeConv::FORBIDDEN;
}
