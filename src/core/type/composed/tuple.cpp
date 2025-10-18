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

#include "tuple.h"
#include "utils/assert.h"

#include "error/diagnostics/diagnostics.h"

using namespace std;

TupleType::TupleType() : ComposedType(TypeCode::Tuple) {}

TupleType::TupleType(const initializer_list<type_ptr_t> &types)
    : ComposedType(TypeCode::Tuple), types_(types) {}

TupleType::TupleType(const vector<type_ptr_t> &types)
    : ComposedType(TypeCode::Tuple), types_(types) {}

string TupleType::toString() const {
    string result = "(";
    for (const auto &type : types_) {
        if (type) {
            result += type->toString() + ", ";
        } else {
            result += "null, ";
        }
    }
    if (!types_.empty()) {
        result.pop_back();
        result.pop_back();
    }
    result += ")";
    return result;
}

std::string TupleType::mangle() const {
    std::string result = "P";
    result += std::to_string(types_.size());
    for (const auto &type : types_) {
        result += type->mangle();
    }
    return result;
}

std::optional<type_ptr_t> TupleType::typeAt(struct_idx_t idx) const {
    ASSERT(std::holds_alternative<size_t>(idx), "Tuple index must be a size_t");
    size_t index = std::get<size_t>(idx);
    if (index >= types_.size()) {
        return std::nullopt;
    }
    return types_[index];
}

bool TupleType::resolved() const {
    for (const auto &type : types_) {
        if (type->code() == TypeCode::Ref) {
            return false;
        }
    }
    return true;
}

void TupleType::resolve(const type_vec_t &typeList) {
    ASSERT(typeList.size() > 0, "Type list is empty");
    ASSERT(!resolved(), "TupleType is already resolved");
    size_t i = 0, j = 0;
    while (i < types_.size() && j < typeList.size()) {
        if (types_[i]->code() == TypeCode::Ref) {
            types_[i] = typeList[j];
            j++;
        }
        i++;
    }
    ASSERT(j == typeList.size(), "Not all types in typeList are used");
    ASSERT(resolved(), "TupleType is not fully resolved");
}

bool TupleType::operator==(const Type &other) const {
    if (this == &other) {
        return true;
    }
    if (other.code() != TypeCode::Tuple) {
        return false;
    }
    const TupleType &otherTuple = dynamic_cast<const TupleType &>(other);

    if (types_.size() != otherTuple.types_.size()) {
        return false;
    }
    for (size_t i = 0; i < types_.size(); i++) {
        if (!types_[i]->equals(otherTuple.types_[i])) {
            return false;
        }
    }
    return true;
}
bool TupleType::operator!=(const Type &other) const { return !(*this == other); }

void TupleType::add(const type_ptr_t &type) { types_.push_back(type); }

void TupleType::set(size_t index, const type_ptr_t &type) { types_[index] = type; }

std::shared_ptr<TupleType> TupleType::slice(size_t start, size_t end) const {
    ASSERT(start <= end && end <= types_.size(), "TupleType slice indices out of range");
    return std::make_shared<TupleType>(
        std::vector<type_ptr_t>(types_.begin() + start, types_.begin() + end));
}

size_t TupleType::size() const { return types_.size(); }

const vector<type_ptr_t> &TupleType::types() const { return types_; }

type_ptr_t TupleType::clone() const { return std::make_shared<TupleType>(types_); }

CastSafety TupleType::castSafetyTo(const Type &other) const {
    if (this == &other) {
        return CastSafety::Safe;
    }
    if (other.code() == code_) {
        return CastSafety::Safe;
    }
    if (other.composed()) {
        switch (other.code()) {
        case TypeCode::Array:
            return CastSafety::Safe;
        case TypeCode::Union:
            return CastSafety::Safe;

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
