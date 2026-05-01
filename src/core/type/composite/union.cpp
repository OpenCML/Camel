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
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/type/composite/union.h"
#include "camel/core/mm.h"
#include "camel/utils/assert.h"
#include "camel/utils/log.h"

using namespace std;
using namespace camel::core::type;

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

UnionType::UnionType(Type *lhs, Type *rhs) : CompositeType(TypeCode::Union) {
    if (lhs && lhs->code() == TypeCode::Union)
        insertUnion(static_cast<const UnionType &>(*lhs));
    else if (lhs)
        types_.insert(lhs);

    if (rhs && rhs->code() == TypeCode::Union)
        insertUnion(static_cast<const UnionType &>(*rhs));
    else if (rhs)
        types_.insert(rhs);
}

UnionType::UnionType(const initializer_list<Type *> &types) : CompositeType(TypeCode::Union) {
    for (const auto &type : types) {
        if (type->code() == TypeCode::Union)
            insertUnion(static_cast<const UnionType &>(*type));
        else
            types_.insert(type);
    }
}

UnionType::UnionType(const vector<Type *> &types) : CompositeType(TypeCode::Union) {
    for (const auto &type : types) {
        if (type->code() == TypeCode::Union)
            insertUnion(static_cast<const UnionType &>(*type));
        else
            types_.insert(type);
    }
}

UnionType *UnionType::create() {
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "UnionType",
        "Allocating UnionType: (), size: {} bytes",
        sizeof(UnionType)));
    void *mem = mm::permSpace().alloc(sizeof(UnionType), alignof(UnionType));
    ASSERT(mem != nullptr, "Failed to allocate UnionType from permSpace");
    return new (mem) UnionType();
}

UnionType *UnionType::create(Type *lhs, Type *rhs) {
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "UnionType",
        "Allocating UnionType: (lhs, rhs), size: {} bytes",
        sizeof(UnionType)));
    void *mem = mm::permSpace().alloc(sizeof(UnionType), alignof(UnionType));
    ASSERT(mem != nullptr, "Failed to allocate UnionType from permSpace");
    return new (mem) UnionType(lhs, rhs);
}

UnionType *UnionType::create(const std::initializer_list<Type *> &types) {
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "UnionType",
        "Allocating UnionType: (initializer_list, size={}), size: {} bytes",
        types.size(),
        sizeof(UnionType)));
    void *mem = mm::permSpace().alloc(sizeof(UnionType), alignof(UnionType));
    ASSERT(mem != nullptr, "Failed to allocate UnionType from permSpace");
    return new (mem) UnionType(types);
}

UnionType *UnionType::create(const std::vector<Type *> &types) {
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "UnionType",
        "Allocating UnionType: (vector, size={}), size: {} bytes",
        types.size(),
        sizeof(UnionType)));
    void *mem = mm::permSpace().alloc(sizeof(UnionType), alignof(UnionType));
    ASSERT(mem != nullptr, "Failed to allocate UnionType from permSpace");
    return new (mem) UnionType(types);
}

void UnionType::add(Type *type) {
    if (type && type->code() == TypeCode::Union) {
        insertUnion(static_cast<const UnionType &>(*type));
    } else if (type) {
        types_.insert(type);
    }
}

bool UnionType::has(Type *type) const { return type && types_.find(type) != types_.end(); }

Type *UnionType::resolve(const type_vec_t &typeList) const {
    ASSERT(false, "UnionType cannot be resolved");
    return nullptr;
}

bool UnionType::resolved() const { return true; }

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

Type *UnionType::clone(bool deep /* = false */) const {
    auto result = UnionType::create();
    for (const auto &type : types_) {
        result->types_.insert(deep && type ? type->clone(true) : type);
    }
    return result;
}

bool UnionType::equals(Type *other) const {
    if (this == other) {
        return true;
    }
    if (!other || other->code() != TypeCode::Union) {
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

CastSafety UnionType::castSafetyFrom(Type *sourceType) const {
    if (auto r = Type::checkCastSafetyWithAny(code(), sourceType))
        return *r;
    if (this == sourceType)
        return CastSafety::Safe;
    return CastSafety::Forbidden;
}

bool UnionType::assignableFrom(Type *sourceType) const { return equals(sourceType); }
