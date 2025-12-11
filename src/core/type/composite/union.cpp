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
 * Updated: Dec. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "union.h"
#include "utils/assert.h"

using namespace std;

void UnionType::insertUnion(const UnionType &other) {
    // Flatten nested UnionType
    for (const auto &type : other.types_) {
        if (type->code() == TypeCode::Union) {
            insertUnion(static_cast<const UnionType &>(*type));
        } else {
            types_.insert(type);
        }
    }
}

UnionType::UnionType() : CompositeType(TypeCode::Union) {}

UnionType::UnionType(const type_ptr_t &lhs, const type_ptr_t &rhs)
    : CompositeType(TypeCode::Union) {
    if (lhs->code() == TypeCode::Union)
        insertUnion(static_cast<const UnionType &>(*lhs));
    else
        types_.insert(lhs);

    if (rhs->code() == TypeCode::Union)
        insertUnion(static_cast<const UnionType &>(*rhs));
    else
        types_.insert(rhs);
}

UnionType::UnionType(const initializer_list<type_ptr_t> &types) : CompositeType(TypeCode::Union) {
    for (const auto &type : types) {
        if (type->code() == TypeCode::Union)
            insertUnion(static_cast<const UnionType &>(*type));
        else
            types_.insert(type);
    }
}

UnionType::UnionType(const vector<type_ptr_t> &types) : CompositeType(TypeCode::Union) {
    for (const auto &type : types) {
        if (type->code() == TypeCode::Union)
            insertUnion(static_cast<const UnionType &>(*type));
        else
            types_.insert(type);
    }
}

std::shared_ptr<UnionType> UnionType::create(const type_ptr_t &lhs, const type_ptr_t &rhs) {
    return std::make_shared<UnionType>(lhs, rhs);
}

std::shared_ptr<UnionType> UnionType::create(const std::initializer_list<type_ptr_t> &types) {
    return std::make_shared<UnionType>(types);
}

std::shared_ptr<UnionType> UnionType::create(const std::vector<type_ptr_t> &types) {
    return std::make_shared<UnionType>(types);
}

void UnionType::add(const type_ptr_t &type) {
    if (type->code() == TypeCode::Union) {
        insertUnion(static_cast<const UnionType &>(*type));
    } else {
        types_.insert(type);
    }
}

bool UnionType::has(const type_ptr_t &type) const { return types_.find(type) != types_.end(); }

type_ptr_t UnionType::resolve(const type_vec_t &typeList) const {
    ASSERT(false, "UnionType cannot be resolved");
    return nullptr;
}

bool UnionType::resolved() const { return true; }

std::optional<type_ptr_t> UnionType::typeAt(size_t idx) const {
    ASSERT(false, "UnionType does not support typeAt()");
    return std::nullopt;
}

string UnionType::toString() const {
    string result = "Union<";
    for (const auto &type : types_) {
        result += type->toString() + ", ";
    }
    if (!types_.empty()) {
        result.pop_back();
        result.pop_back();
    }
    result += ">";
    return result;
}

std::string UnionType::mangle() const {
    std::string result = "U";
    result += std::to_string(types_.size());
    for (const auto &type : types_) {
        result += type->mangle();
    }
    return result;
}

type_ptr_t UnionType::clone(bool deep /* = false */) const {
    auto result = std::make_shared<UnionType>();
    for (const auto &type : types_) {
        result->types_.insert(deep ? type->clone(true) : type);
    }
    return result;
}

bool UnionType::equals(const type_ptr_t &other) const {
    if (this == other.get()) {
        return true;
    }
    if (other->code() != TypeCode::Union) {
        return false;
    }
    const UnionType &otherUnion = static_cast<const UnionType &>(*other);

    if (types_.size() != otherUnion.types_.size()) {
        return false;
    }
    for (const auto &type : otherUnion.types_) {
        if (types_.find(type) == types_.end()) {
            return false;
        }
    }
    return true;
}

CastSafety UnionType::castSafetyTo(const Type &other) const {
    if (this == &other) {
        return CastSafety::Safe;
    }
    return CastSafety::Forbidden;
}

bool UnionType::assignable(const type_ptr_t &type) const { return equals(type); }
