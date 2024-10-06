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
 * Updated: Oct. 6, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "params.h"
#include "common/data.h"

using namespace std;

TypeConv ParamsType::convertibilityToParams(const ParamsType &other) const {
    // TODO: not fully implemented
    if (elements_.size() != other.elements_.size()) {
        return TypeConv::FORBIDDEN;
    }
    TypeConv result = TypeConv::SAFE;
    for (size_t i = 0; i < elements_.size(); i++) {
        const auto &[name, type, value] = elements_[i];
        const auto &[otherName, otherType, otherValue] = other.elements_[i];
        if (name != otherName) {
            return TypeConv::FORBIDDEN;
        }
        TypeConv paramConv = type->convertibility(*otherType);
        if (paramConv == TypeConv::FORBIDDEN) {
            return TypeConv::FORBIDDEN;
        } else if (paramConv == TypeConv::UNSAFE) {
            result = TypeConv::UNSAFE;
        }
    }
    return result;
}

ParamsType::ParamsType() : StructType(TypeCode::PARAMS) {}

string ParamsType::toString() const {
    string result = "Params<";
    for (const auto &tuple : elements_) {
        auto &[name, type, value] = tuple;
        result += (name.empty() ? "" : name + ": ") + (type ? type->toString() : "NULL");
        if (value) {
            result += " = " + value->toString();
        }
        result += ", ";
    }
    if (elements_.size() > 0) {
        result.pop_back();
        result.pop_back();
    }
    result += ">";
    return result;
}

bool ParamsType::operator==(const Type &other) const {
    if (other.code() != TypeCode::PARAMS) {
        return false;
    }
    const ParamsType &otherParam = dynamic_cast<const ParamsType &>(other);

    if (elements_.size() != otherParam.elements_.size()) {
        return false;
    }
    for (size_t i = 0; i < elements_.size(); i++) {
        auto &[name, type, value] = elements_[i];
        auto &[otherName, otherType, otherValue] = otherParam.elements_[i];
        if (name != otherName) {
            return false;
        }
        if (!type->equals(otherType)) {
            return false;
        }
        if (value && !value->equals(otherValue)) {
            return false;
        }
    }
    return true;
}

bool ParamsType::operator!=(const Type &other) const { return !(*this == other); }

bool ParamsType::add(const string &key, const type_ptr_t &type, const data_ptr_t &value = nullptr) {
    // here we allow duplicate keys, for the sake of simplicity
    // we use "" to represent the unnamed parameter
    elements_.push_back({key, type, value});
    return true;
}

size_t ParamsType::size() const { return elements_.size(); }

const vector<tuple<string, type_ptr_t, data_ptr_t>> &ParamsType::elements() const { return elements_; }

map<string, type_ptr_t> ParamsType::map() const {
    auto result = std::map<string, type_ptr_t>();
    for (const auto &tuple : elements_) {
        const auto &[name, type, value] = tuple;
        result[name] = type;
    }
    return result;
}

void ParamsType::clear() { elements_.clear(); }

TypeConv ParamsType::convertibility(const Type &other) const {
    // TODO: not fully implemented
    // TODO: others' convertibility should be checked
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::PARAMS: {
            const ParamsType &otherParam = dynamic_cast<const ParamsType &>(other);
            if (elements_.size() != otherParam.elements_.size()) {
                return TypeConv::FORBIDDEN;
            }
            TypeConv result = TypeConv::SAFE;
            for (size_t i = 0; i < elements_.size(); i++) {
                const auto &[name, type, value] = elements_[i];
                if (name != name) {
                    return TypeConv::FORBIDDEN;
                }
                TypeConv paramConv = type->convertibility(*type);
                if (paramConv == TypeConv::FORBIDDEN) {
                    return TypeConv::FORBIDDEN;
                } else if (paramConv == TypeConv::UNSAFE) {
                    result = TypeConv::UNSAFE;
                }
            }
            return result;
        }
        case TypeCode::UNION:
            return TypeConv::SAFE;
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::SET:
            return TypeConv::SAFE;
        case TypeCode::ARRAY:
            return TypeConv::SAFE;
        case TypeCode::VECTOR:
            return TypeConv::SAFE;
        case TypeCode::TENSOR:
            return TypeConv::SAFE;
        case TypeCode::MAP:
            return TypeConv::SAFE;
        case TypeCode::DICT:
            return TypeConv::SAFE;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}
