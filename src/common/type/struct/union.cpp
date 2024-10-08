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
 * Created: Oct. 06, 2024
 * Updated: Oct. 08, 2024
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
        if (type->code() == TypeCode::UNION)
            insertUnion(dynamic_cast<const UnionType &>(*type));
        else
            types_.insert(type);
    }
}

UnionType::UnionType() : StructType(TypeCode::UNION) {}

UnionType::UnionType(const type_ptr_t &lhs, const type_ptr_t &rhs) : StructType(TypeCode::UNION) {
    if (lhs->code() == TypeCode::UNION)
        insertUnion(dynamic_cast<const UnionType &>(*lhs));
    else
        types_.insert(lhs);

    if (rhs->code() == TypeCode::UNION)
        insertUnion(dynamic_cast<const UnionType &>(*rhs));
    else
        types_.insert(rhs);
}

UnionType::UnionType(const initializer_list<type_ptr_t> &types) : StructType(TypeCode::UNION) {
    for (const auto &type : types) {
        if (type->code() == TypeCode::UNION)
            insertUnion(dynamic_cast<const UnionType &>(*type));
        else
            types_.insert(type);
    }
}

UnionType::UnionType(const vector<type_ptr_t> &types) : StructType(TypeCode::UNION) {
    for (const auto &type : types) {
        if (type->code() == TypeCode::UNION)
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

bool UnionType::operator==(const Type &other) const {
    if (other.code() != TypeCode::UNION) {
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

TypeConv UnionType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::UNION: {
            const UnionType &otherUnion = dynamic_cast<const UnionType &>(other);
            TypeConv result = TypeConv::SAFE;
            for (const auto &type : types_) {
                TypeConv typeConv = TypeConv::FORBIDDEN;
                for (const auto &otherType : otherUnion.types_) {
                    TypeConv tempConv = type->convertibility(*otherType);
                    if (tempConv == TypeConv::SAFE) {
                        typeConv = TypeConv::SAFE;
                        break;
                    } else if (tempConv == TypeConv::UNSAFE) {
                        typeConv = TypeConv::UNSAFE;
                    }
                }
                if (typeConv == TypeConv::FORBIDDEN) {
                    return TypeConv::FORBIDDEN;
                } else if (typeConv == TypeConv::UNSAFE) {
                    result = TypeConv::UNSAFE;
                }
            }
            return result;
        }
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::SET: {
            const type_ptr_t &otherType = dynamic_cast<const SetType &>(other).valueType();
            if (otherType->code() == TypeCode::UNION) {
                const UnionType &otherUnion = dynamic_cast<const UnionType &>(*otherType);
                return convertibility(otherUnion);
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::ARRAY: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            const type_ptr_t &otherType = otherArray.elementType();
            if (otherArray.size() > 1) {
                // 0 or 1 size array is allowed
                return TypeConv::FORBIDDEN;
            }
            if (otherType->code() == TypeCode::UNION) {
                const UnionType &otherUnion = dynamic_cast<const UnionType &>(*otherType);
                return convertibility(otherUnion);
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::VECTOR: {
            const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
            const type_ptr_t &otherType = otherVector.elementType();
            if (otherType->code() == TypeCode::UNION) {
                const UnionType &otherUnion = dynamic_cast<const UnionType &>(*otherType);
                return convertibility(otherUnion);
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::TENSOR: {
            const TensorType &otherTensor = dynamic_cast<const TensorType &>(other);
            const auto &shape = otherTensor.shape();
            if (shape.size() != 1 || shape.front() > 1) {
                return TypeConv::FORBIDDEN;
            }
            const type_ptr_t &otherType = otherTensor.elementType();
            if (otherType->code() == TypeCode::UNION) {
                const UnionType &otherUnion = dynamic_cast<const UnionType &>(*otherType);
                return convertibility(otherUnion);
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::MAP:
            [[fallthrough]];
        case TypeCode::DICT:
            return TypeConv::FORBIDDEN;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primary types and special types are forbidden
    return TypeConv::FORBIDDEN;
}
