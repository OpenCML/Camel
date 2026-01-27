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
 * Updated: Jan. 27, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "tuple.h"
#include "core/mm/mm.h"
#include "error/diagnostics/diagnostics.h"
#include "utils/assert.h"
#include "utils/type.h"

using namespace std;

void TupleType::computeLayout() const {
    if (!layout_) {
        std::vector<TypeCode> elemTypes;
        std::vector<size_t> refs;
        elemTypes.reserve(types_.size());
        size_t i = 0;
        for (const auto &t : types_) {
            elemTypes.push_back(t->code());
            if (t->code() == TypeCode::Ref) {
                refs.push_back(i);
            }
            i++;
        }
        layout_ = std::make_shared<TupleTypeLayout>(std::move(elemTypes), std::move(refs));
    }
}

TupleType::TupleType() : CompositeType(TypeCode::Tuple) {}

TupleType::TupleType(const initializer_list<Type *> &types)
    : CompositeType(TypeCode::Tuple), types_(types) {}

TupleType::TupleType(const vector<Type *> &types) : CompositeType(TypeCode::Tuple), types_(types) {}

TupleType::TupleType(std::vector<Type *> &&types)
    : CompositeType(TypeCode::Tuple), types_(std::move(types)) {}

TupleType *TupleType::create() {
    void *mem = mm::permSpace().alloc(sizeof(TupleType), alignof(TupleType));
    ASSERT(mem != nullptr, "Failed to allocate TupleType from permSpace");
    return new (mem) TupleType();
}

TupleType *TupleType::create(const std::vector<Type *> &types) {
    void *mem = mm::permSpace().alloc(sizeof(TupleType), alignof(TupleType));
    ASSERT(mem != nullptr, "Failed to allocate TupleType from permSpace");
    return new (mem) TupleType(types);
}

TupleType *TupleType::create(std::vector<Type *> &&types) {
    void *mem = mm::permSpace().alloc(sizeof(TupleType), alignof(TupleType));
    ASSERT(mem != nullptr, "Failed to allocate TupleType from permSpace");
    return new (mem) TupleType(std::move(types));
}

void TupleType::add(Type *type) { types_.push_back(type); }

void TupleType::set(size_t index, Type *type) { types_[index] = type; }

size_t TupleType::size() const { return types_.size(); }

const vector<Type *> &TupleType::types() const { return types_; }

TupleType *TupleType::slice(size_t start, size_t end) const {
    ASSERT(start <= end && end <= types_.size(), "TupleType slice indices out of range");
    return TupleType::create(std::vector<Type *>(types_.begin() + start, types_.begin() + end));
}

std::optional<Type *> TupleType::typeAt(size_t idx) const {
    if (idx >= types_.size()) {
        return std::nullopt;
    }
    return types_[idx];
}

const TupleTypeLayout &TupleType::layout() const {
    if (!layout_) {
        computeLayout();
    }
    return *layout_;
}

Type *TupleType::resolve(const type_vec_t &typeList) const {
    ASSERT(typeList.size() > 0, "Type list is empty");
    ASSERT(!resolved(), "TupleType is already resolved");
    std::vector<Type *> types = types_;
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

Type *TupleType::clone(bool deep /* = false */) const {
    TupleType *res;
    if (deep) {
        std::vector<Type *> clonedTypes;
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

bool TupleType::equals(Type *other) const {
    if (this == other) {
        return true;
    }
    if (!other || other->code() != TypeCode::Tuple) {
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

bool TupleType::assignable(Type *type) const { return equals(type); }
