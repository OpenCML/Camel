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
 * Updated: Oct. 06, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "dict.h"

using namespace std;

DictType::DictType() : StructType(TypeCode::Dict) {}

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
            result += "null, ";
        }
    }
    result.pop_back();
    result.pop_back();
    result += " }";
    return result;
}

std::optional<type_ptr_t> DictType::typeAt(struct_idx_t idx) const {
    ASSERT(std::holds_alternative<std::string>(idx), "Dict index must be a string");
    const std::string &key = std::get<std::string>(idx);
    if (!has(key)) {
        return std::nullopt;
    }
    return get(key);
}

bool DictType::operator==(const Type &other) const {
    if (other.code() != TypeCode::Dict) {
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

CastSafety DictType::castSafetyTo(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::Dict: {
            const DictType &otherDict = dynamic_cast<const DictType &>(other);
            CastSafety result = CastSafety::Safe;
            for (const auto &field : otherDict.fields_) {
                const auto &ident = field.first;
                const auto &type = field.second;
                if (!fields_.count(ident)) {
                    return CastSafety::Forbidden;
                }
                const auto &fieldType = fields_.at(ident);
                const CastSafety fieldConv = fieldType->castSafetyTo(*type);
                if (fieldConv == CastSafety::Forbidden) {
                    return CastSafety::Forbidden;
                }
                if (fieldConv == CastSafety::Unsafe) {
                    result = CastSafety::Unsafe;
                }
            }
            return result;
        }
        case TypeCode::Set:
            [[fallthrough]];
        case TypeCode::Map:
            [[fallthrough]];
        case TypeCode::Array:
            [[fallthrough]];
        case TypeCode::List:
            [[fallthrough]];
        case TypeCode::Union:
            [[fallthrough]];
        case TypeCode::Vector:
            [[fallthrough]];
        case TypeCode::Tensor:
            return CastSafety::Forbidden;

        default:
            return CastSafety::Forbidden;
        }
    }
    if (other.code() == TypeCode::Any) {
        return CastSafety::Safe;
    }
    // primary types and special types are forbidden
    return CastSafety::Forbidden;
}
