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
 * Updated: Dec. 06, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "tuple.h"
#include "error/diagnostics/diagnostics.h"
#include "utils/assert.h"
#include "utils/type.h"

using namespace std;

void TupleType::computeLayout() const {
    if (!layout_) {
        std::vector<TypeCode> elemTypes;
        elemTypes.reserve(types_.size());
        for (const auto &t : types_) {
            elemTypes.push_back(t->code());
        }
        layout_ = std::make_shared<TupleLayout>(std::move(elemTypes));
    }
}

TupleType::TupleType() : CompositeType(TypeCode::Tuple) {}

TupleType::TupleType(const initializer_list<type_ptr_t> &types)
    : CompositeType(TypeCode::Tuple), types_(types) {}

TupleType::TupleType(const vector<type_ptr_t> &types)
    : CompositeType(TypeCode::Tuple), types_(types) {}

TupleType::TupleType(std::vector<type_ptr_t> &&types)
    : CompositeType(TypeCode::Tuple), types_(std::move(types)) {}

std::shared_ptr<TupleType> TupleType::create(const std::vector<type_ptr_t> &types) const {
    return std::make_shared<TupleType>(types);
}

std::shared_ptr<TupleType> TupleType::create(std::vector<type_ptr_t> &&types) const {
    return std::make_shared<TupleType>(std::move(types));
}

void TupleType::add(const type_ptr_t &type) { types_.push_back(type); }

void TupleType::set(size_t index, const type_ptr_t &type) { types_[index] = type; }

size_t TupleType::size() const { return types_.size(); }

const vector<type_ptr_t> &TupleType::types() const { return types_; }

std::shared_ptr<TupleType> TupleType::slice(size_t start, size_t end) const {
    ASSERT(start <= end && end <= types_.size(), "TupleType slice indices out of range");
    return std::make_shared<TupleType>(
        std::vector<type_ptr_t>(types_.begin() + start, types_.begin() + end));
}

std::optional<type_ptr_t> TupleType::typeAt(size_t idx) const {
    if (idx >= types_.size()) {
        return std::nullopt;
    }
    return types_[idx];
}

const TupleLayout &TupleType::layout() const {
    if (!layout_) {
        computeLayout();
    }
    return *layout_;
}

type_ptr_t TupleType::resolve(const type_vec_t &typeList) const {
    ASSERT(typeList.size() > 0, "Type list is empty");
    ASSERT(!resolved(), "TupleType is already resolved");
    std::vector<type_ptr_t> types = types_;
    size_t i = 0, j = 0;
    while (i < types.size() && j < typeList.size()) {
        if (types[i]->code() == TypeCode::Ref) {
            types[i] = typeList[j];
            j++;
        }
        i++;
    }
    ASSERT(j == typeList.size(), "Not all types in typeList are used");
    auto newTuple = TupleType::create(std::move(types));
    ASSERT(newTuple->resolved(), "TupleType is not fully resolved");
    return newTuple;
}

bool TupleType::resolved() const {
    for (const auto &type : types_) {
        if (type->code() == TypeCode::Ref) {
            return false;
        }
    }
    return true;
}

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

type_ptr_t TupleType::clone(bool deep /* = false */) const {
    std::shared_ptr<TupleType> res;
    if (deep) {
        std::vector<type_ptr_t> clonedTypes;
        for (const auto &type : types_) {
            clonedTypes.push_back(type->clone(true));
        }
        res = TupleType::create(std::move(clonedTypes));
    } else {
        res = TupleType::create(types_);
    }
    res->layout_ = layout_;
    return res;
}

bool TupleType::equals(const type_ptr_t &other) const {
    if (this == other.get()) {
        return true;
    }
    if (other->code() != TypeCode::Tuple) {
        return false;
    }
    const TupleType &otherTuple = static_cast<const TupleType &>(*other);
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

CastSafety TupleType::castSafetyTo(const Type &other) const {
    if (this == &other) {
        return CastSafety::Safe;
    }
    return CastSafety::Forbidden;
}

bool TupleType::assignable(const type_ptr_t &type) const { return this->equals(type); }
