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

#include "union.h"

#include "array.h"
#include "set.h"
#include "tensor.h"
#include "vector.h"

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

UnionType::UnionType() : StructType(TypeCode::Union) {}

UnionType::UnionType(const type_ptr_t &lhs, const type_ptr_t &rhs) : StructType(TypeCode::Union) {
    if (lhs->code() == TypeCode::Union)
        insertUnion(dynamic_cast<const UnionType &>(*lhs));
    else
        types_.insert(lhs);

    if (rhs->code() == TypeCode::Union)
        insertUnion(dynamic_cast<const UnionType &>(*rhs));
    else
        types_.insert(rhs);
}

UnionType::UnionType(const initializer_list<type_ptr_t> &types) : StructType(TypeCode::Union) {
    for (const auto &type : types) {
        if (type->code() == TypeCode::Union)
            insertUnion(dynamic_cast<const UnionType &>(*type));
        else
            types_.insert(type);
    }
}

UnionType::UnionType(const vector<type_ptr_t> &types) : StructType(TypeCode::Union) {
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

std::optional<type_ptr_t> UnionType::typeAt(struct_idx_t idx) const {
    ASSERT(false, "UnionType does not support indexing");
    return std::nullopt;
}

bool UnionType::operator==(const Type &other) const {
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
    if (other.structured()) {
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
        case TypeCode::List:
            return CastSafety::Safe;
        case TypeCode::Set: {
            const type_ptr_t &otherType = dynamic_cast<const SetType &>(other).valueType();
            if (otherType->code() == TypeCode::Union) {
                const UnionType &otherUnion = dynamic_cast<const UnionType &>(*otherType);
                return castSafetyTo(otherUnion);
            }
            return CastSafety::Forbidden;
        }
        case TypeCode::Array: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            const type_ptr_t &otherType = otherArray.elementType();
            if (otherArray.size() > 1) {
                // 0 or 1 size array is allowed
                return CastSafety::Forbidden;
            }
            if (otherType->code() == TypeCode::Union) {
                const UnionType &otherUnion = dynamic_cast<const UnionType &>(*otherType);
                return castSafetyTo(otherUnion);
            }
            return CastSafety::Forbidden;
        }
        case TypeCode::Vector: {
            const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
            const type_ptr_t &otherType = otherVector.elementType();
            if (otherType->code() == TypeCode::Union) {
                const UnionType &otherUnion = dynamic_cast<const UnionType &>(*otherType);
                return castSafetyTo(otherUnion);
            }
            return CastSafety::Forbidden;
        }
        case TypeCode::Tensor: {
            const TensorType &otherTensor = dynamic_cast<const TensorType &>(other);
            const auto &shape = otherTensor.shape();
            if (shape.size() != 1 || shape.front() > 1) {
                return CastSafety::Forbidden;
            }
            const type_ptr_t &otherType = otherTensor.elementType();
            if (otherType->code() == TypeCode::Union) {
                const UnionType &otherUnion = dynamic_cast<const UnionType &>(*otherType);
                return castSafetyTo(otherUnion);
            }
            return CastSafety::Forbidden;
        }
        case TypeCode::Map:
            [[fallthrough]];
        case TypeCode::Dict:
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
