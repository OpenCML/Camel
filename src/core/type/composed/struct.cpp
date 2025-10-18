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
 * Updated: Oct. 18, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "struct.h"

using namespace std;

StructType::StructType() : ComposedType(TypeCode::Struct) {}

string StructType::toString() const {
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

std::string StructType::mangle() const {
    std::string result = "D";
    result += std::to_string(fields_.size());
    for (const auto &field : fields_) {
        result += "K" + std::to_string(field.first.size()) + field.first;
        result += "V" + field.second->mangle();
    }
    return result;
}

std::optional<type_ptr_t> StructType::typeAt(struct_idx_t idx) const {
    ASSERT(std::holds_alternative<std::string>(idx), "Dict index must be a string");
    const std::string &key = std::get<std::string>(idx);
    if (!has(key)) {
        return std::nullopt;
    }
    return get(key);
}

bool StructType::resolved() const { return refIndices_.empty(); }

void StructType::resolve(const type_vec_t &typeList) {
    ASSERT(typeList.size() > 0, "Type list is empty");
    ASSERT(!resolved(), "StructType is already resolved");
    ASSERT(
        refIndices_.size() == typeList.size(),
        "Type list size does not match number of unresolved fields");
    for (size_t i = 0; i < refIndices_.size(); ++i) {
        const std::string &ref = refIndices_[i];
        const type_ptr_t &type = typeList[i];
        fields_[ref] = type;
    }
    refIndices_.clear();
}

bool StructType::operator==(const Type &other) const {
    if (this == &other) {
        return true;
    }
    if (other.code() != TypeCode::Struct) {
        return false;
    }
    const StructType &otherDict = dynamic_cast<const StructType &>(other);

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

bool StructType::operator!=(const Type &other) const { return !(*this == other); }

bool StructType::add(const string &name, const type_ptr_t &type) {
    if (has(name)) {
        return false;
    }
    if (type->code() == TypeCode::Ref) {
        refIndices_.push_back(name);
    }
    fields_[name] = type;
    return true;
}

bool StructType::has(const string &name) const { return fields_.find(name) != fields_.end(); }

type_ptr_t StructType::get(const string &name) const { return fields_.at(name); }

void StructType::clear() { fields_.clear(); }

type_ptr_t StructType::operator|(const StructType &other) const {
    ASSERT(resolved() && other.resolved(), "Cannot union with unresolved StructType");
    auto result = make_shared<StructType>();
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
            result->fields_[ident] = type;
        }
    }
    return result;
}

type_ptr_t StructType::operator&(const StructType &other) const {
    ASSERT(resolved() && other.resolved(), "Cannot intersect with unresolved StructType");
    auto result = make_shared<StructType>();
    for (const auto &field : fields_) {
        const auto &ident = field.first;
        if (other.has(ident)) {
            const type_ptr_t &otherType = other.get(ident);
            result->add(ident, otherType);
        }
    }
    return result;
}

type_ptr_t StructType::clone() const {
    auto newStruct = std::make_shared<StructType>();
    for (const auto &field : fields_) {
        newStruct->add(field.first, field.second);
    }
    return newStruct;
}

CastSafety StructType::castSafetyTo(const Type &other) const {
    if (this == &other) {
        return CastSafety::Safe;
    }
    if (other.code() == code_) {
        return CastSafety::Safe;
    }
    if (other.composed()) {
        switch (other.code()) {
        case TypeCode::Struct: {
            const StructType &otherDict = dynamic_cast<const StructType &>(other);
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
        case TypeCode::Array:
            [[fallthrough]];
        case TypeCode::Union:
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
