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
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "union.h"

#include "array.h"

using namespace std;

void UnionType::insertUnion(const UnionType &other) {
    // flatten the union type
    for (const auto &type : other.types_) {
        if (type->code() == TypeCode::Union)
            insertUnion(dynamic_cast<const UnionType &>(*type));
        else
            types_.insert(type);
    }
}

UnionType::UnionType() : ComposedType(TypeCode::Union) {}

UnionType::UnionType(const type_ptr_t &lhs, const type_ptr_t &rhs) : ComposedType(TypeCode::Union) {
    if (lhs->code() == TypeCode::Union)
        insertUnion(dynamic_cast<const UnionType &>(*lhs));
    else
        types_.insert(lhs);

    if (rhs->code() == TypeCode::Union)
        insertUnion(dynamic_cast<const UnionType &>(*rhs));
    else
        types_.insert(rhs);
}

UnionType::UnionType(const initializer_list<type_ptr_t> &types) : ComposedType(TypeCode::Union) {
    for (const auto &type : types) {
        if (type->code() == TypeCode::Union)
            insertUnion(dynamic_cast<const UnionType &>(*type));
        else
            types_.insert(type);
    }
}

UnionType::UnionType(const vector<type_ptr_t> &types) : ComposedType(TypeCode::Union) {
    for (const auto &type : types) {
        if (type->code() == TypeCode::Union)
            insertUnion(dynamic_cast<const UnionType &>(*type));
        else
            types_.insert(type);
    }
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

std::optional<type_ptr_t> UnionType::typeAt(struct_idx_t idx) const {
    ASSERT(false, "UnionType does not support indexing");
    return std::nullopt;
}

bool UnionType::operator==(const Type &other) const {
    if (this == &other) {
        return true;
    }
    if (other.code() != TypeCode::Union) {
        return false;
    }
    const UnionType &otherUnion = dynamic_cast<const UnionType &>(other);

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

bool UnionType::operator!=(const Type &other) const { return !(*this == other); }

void UnionType::add(const type_ptr_t &type) { types_.insert(type); }

bool UnionType::has(const type_ptr_t &type) const { return types_.find(type) != types_.end(); }

CastSafety UnionType::castSafetyTo(const Type &other) const {
    if (this == &other) {
        return CastSafety::Safe;
    }
    if (other.code() == code_) {
        return CastSafety::Safe;
    }
    if (other.composed()) {
        switch (other.code()) {
        case TypeCode::Union: {
            const UnionType &otherUnion = dynamic_cast<const UnionType &>(other);
            CastSafety result = CastSafety::Safe;
            for (const auto &type : types_) {
                CastSafety typeConv = CastSafety::Forbidden;
                for (const auto &otherType : otherUnion.types_) {
                    CastSafety tempConv = type->castSafetyTo(*otherType);
                    if (tempConv == CastSafety::Safe) {
                        typeConv = CastSafety::Safe;
                        break;
                    } else if (tempConv == CastSafety::Unsafe) {
                        typeConv = CastSafety::Unsafe;
                    }
                }
                if (typeConv == CastSafety::Forbidden) {
                    return CastSafety::Forbidden;
                } else if (typeConv == CastSafety::Unsafe) {
                    result = CastSafety::Unsafe;
                }
            }
            return result;
        }

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
