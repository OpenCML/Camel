/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Apr. 9, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "type.h"

type_ptr_t int32TypePtr;
type_ptr_t int64TypePtr;
type_ptr_t floatTypePtr;
type_ptr_t doubleTypePtr;
type_ptr_t stringTypePtr;
type_ptr_t boolTypePtr;
type_ptr_t charTypePtr;

type_ptr_t intTypePtr;
type_ptr_t realTypePtr;
type_ptr_t numberTypePtr;

type_ptr_t anyTypePtr;
type_ptr_t voidTypePtr;
type_ptr_t functorTypePtr;

const signed char primeTypeConvMatrix[7][7] = {
    // INT32, INT64, FLOAT, DOUBLE, STRING, BOOL, CHAR
    {01, 01, 01, 01, 00, 01, -1}, // INT32
    {-1, 01, -1, 01, 00, 01, -1}, // INT64
    {-1, -1, 01, 01, 00, 01, -1}, // FLOAT
    {-1, -1, -1, 01, 00, 01, -1}, // DOUBLE
    {00, 00, 00, 00, 01, 01, 00}, // STRING
    {01, 01, 01, 01, 01, 01, 01}, // BOOL
    {01, 01, 01, 01, 01, 01, 01}  // CHAR
};

std::string typeCodeToString(TypeCode code) {
    switch (code) {
        // primitive types
    case TypeCode::INT32:
        return "int32";
    case TypeCode::INT64:
        return "int64";
    case TypeCode::FLOAT:
        return "float";
    case TypeCode::DOUBLE:
        return "double";
    case TypeCode::STRING:
        return "string";
    case TypeCode::BOOL:
        return "bool";
    case TypeCode::CHAR:
        return "char";
        // structured types
    case TypeCode::SET:
        return "set";
    case TypeCode::MAP:
        return "map";
    case TypeCode::ARRAY:
        return "array";
    case TypeCode::LIST:
        return "list";
    case TypeCode::DICT:
        return "dict";
    case TypeCode::UNION:
        return "union";
    case TypeCode::VECTOR:
        return "vector";
    case TypeCode::MATRIX:
        return "matrix";
        // special types
    case TypeCode::ANY:
        return "any";
    case TypeCode::VOID:
        return "void";
    case TypeCode::FUNCTOR:
        return "functor";
    }
}

TypeConv PrimType::convertibility(const Type &other) const {
    const TypeCode otherCode = other.code();
    if (otherCode == code_) {
        return TypeConv::SAFE;
    }
    if (other.primitive()) {
        const int thisIndex = static_cast<int>(code_) & 0b00'000111;
        const int otherIndex = static_cast<int>(otherCode) & 0b00'000111;
        return static_cast<TypeConv>(primeTypeConvMatrix[thisIndex][otherIndex]);
    }
    if (other.structured()) {
        switch (otherCode) {
        case TypeCode::UNION:
            [[fallthrough]];
        case TypeCode::LIST:
            [[fallthrough]];
        case TypeCode::ARRAY:
            [[fallthrough]];
        case TypeCode::VECTOR:
            [[fallthrough]];
        case TypeCode::MATRIX:
            [[fallthrough]];
        case TypeCode::SET:
            return TypeConv::SAFE;
        case TypeCode::MAP:
            [[fallthrough]];
        case TypeCode::DICT:
            return TypeConv::FORBIDDEN;
        default:
            return TypeConv::FORBIDDEN;
        }
    }
    if (other.special()) {
        switch (otherCode) {
        case TypeCode::ANY:
            return TypeConv::SAFE;
        case TypeCode::VOID:
            return TypeConv::UNSAFE;
        case TypeCode::FUNCTOR:
            return TypeConv::FORBIDDEN;
        default:
            return TypeConv::FORBIDDEN;
        }
    }
    return TypeConv::FORBIDDEN;
}

TypeConv SpecialType::convertibility(const Type &other) const {
    if (other.code() == code_) {
        return TypeConv::SAFE;
    }
    if (other.primitive() || other.structured()) {
        return TypeConv::FORBIDDEN;
    }
    if (other.code() == TypeCode::VOID)
        return TypeConv::UNSAFE;
    return TypeConv::FORBIDDEN;
}

TypeConv SetType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::SET:
            return valueType_->convertibility(*(dynamic_cast<const SetType &>(other).valueType_));
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::ARRAY: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            if (otherArray.size() == 0) {
                // if the array size is 0
                // it indicates that the array is dynamic
                return valueType_->convertibility(*otherArray.elementType());
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::MAP:
            [[fallthrough]];
        case TypeCode::DICT:
            [[fallthrough]];
        case TypeCode::UNION:
            [[fallthrough]];
        case TypeCode::VECTOR:
            [[fallthrough]];
        case TypeCode::MATRIX:
            return TypeConv::FORBIDDEN;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

TypeConv MapType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::MAP: {
            const MapType &otherMap = dynamic_cast<const MapType &>(other);
            const TypeConv keyConv = keyType_->convertibility(*otherMap.keyType_);
            const TypeConv valueConv = valueType_->convertibility(*otherMap.valueType_);
            if (keyConv == TypeConv::FORBIDDEN || valueConv == TypeConv::FORBIDDEN) {
                return TypeConv::FORBIDDEN;
            }
            if (keyConv == TypeConv::SAFE && valueConv == TypeConv::SAFE) {
                return TypeConv::SAFE;
            }
            return TypeConv::UNSAFE;
        }
        case TypeCode::SET:
            [[fallthrough]];
        case TypeCode::ARRAY:
            [[fallthrough]];
        case TypeCode::LIST:
            [[fallthrough]];
        case TypeCode::DICT:
            [[fallthrough]];
        case TypeCode::UNION:
            [[fallthrough]];
        case TypeCode::VECTOR:
            [[fallthrough]];
        case TypeCode::MATRIX:
            return TypeConv::FORBIDDEN;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

TypeConv ArrayType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::ARRAY: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            if (size_ == otherArray.size()) {
                return elementType_->convertibility(*otherArray.elementType());
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::SET: {
            const SetType &otherSet = dynamic_cast<const SetType &>(other);
            return elementType_->convertibility(*otherSet.valueType());
        }
        case TypeCode::VECTOR: {
            const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
            if (size_ == otherVector.size()) {
                return elementType_->convertibility(*otherVector.elementType());
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::MATRIX: {
            const MatrixType &otherMatrix = dynamic_cast<const MatrixType &>(other);
            const auto &shape = otherMatrix.shape();
            if (shape.size() == 1 && size_ == shape.front()) {
                return elementType_->convertibility(*otherMatrix.elementType());
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::MAP:
            [[fallthrough]];
        case TypeCode::DICT:
            [[fallthrough]];
        case TypeCode::UNION:
            return TypeConv::FORBIDDEN;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

TypeConv DictType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::DICT: {
            const DictType &otherDict = dynamic_cast<const DictType &>(other);
            TypeConv result = TypeConv::SAFE;
            for (const auto &field : otherDict.fields_) {
                const auto &ident = field.first;
                const auto &type = field.second;
                if (!fields_.count(ident)) {
                    return TypeConv::FORBIDDEN;
                }
                const auto &fieldType = fields_.at(ident);
                const TypeConv fieldConv = fieldType->convertibility(*type);
                if (fieldConv == TypeConv::FORBIDDEN) {
                    return TypeConv::FORBIDDEN;
                }
                if (fieldConv == TypeConv::UNSAFE) {
                    result = TypeConv::UNSAFE;
                }
            }
            return result;
        }
        case TypeCode::SET:
            [[fallthrough]];
        case TypeCode::MAP:
            [[fallthrough]];
        case TypeCode::ARRAY:
            [[fallthrough]];
        case TypeCode::LIST:
            [[fallthrough]];
        case TypeCode::UNION:
            [[fallthrough]];
        case TypeCode::VECTOR:
            [[fallthrough]];
        case TypeCode::MATRIX:
            return TypeConv::FORBIDDEN;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

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
                    }
                    if (tempConv == TypeConv::UNSAFE) {
                        typeConv = TypeConv::UNSAFE;
                    }
                }
                if (typeConv == TypeConv::FORBIDDEN) {
                    return TypeConv::FORBIDDEN;
                }
                if (typeConv == TypeConv::UNSAFE) {
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
            if (otherArray.size() > 1) {
                // 0 or 1 size array is allowed
                return TypeConv::FORBIDDEN;
            }
            const type_ptr_t &otherType = otherArray.elementType();
            if (otherType->code() == TypeCode::UNION) {
                const UnionType &otherUnion = dynamic_cast<const UnionType &>(*otherType);
                return convertibility(otherUnion);
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::VECTOR: {
            const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
            if (otherVector.size() > 1) {
                // 0 or 1 size vector is allowed
                return TypeConv::FORBIDDEN;
            }
            const type_ptr_t &otherType = otherVector.elementType();
            if (otherType->code() == TypeCode::UNION) {
                const UnionType &otherUnion = dynamic_cast<const UnionType &>(*otherType);
                return convertibility(otherUnion);
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::MATRIX: {
            const MatrixType &otherMatrix = dynamic_cast<const MatrixType &>(other);
            const auto &shape = otherMatrix.shape();
            if (shape.size() != 1 || shape.front() > 1) {
                return TypeConv::FORBIDDEN;
            }
            const type_ptr_t &otherType = otherMatrix.elementType();
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
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

TypeConv VectorType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::VECTOR: {
            const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
            if (size_ == otherVector.size()) {
                return elementType_->convertibility(*otherVector.elementType());
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::ARRAY: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            if (otherArray.size() == 0 || otherArray.size() == size_) {
                return elementType_->convertibility(*otherArray.elementType());
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::MATRIX: {
            const MatrixType &otherMatrix = dynamic_cast<const MatrixType &>(other);
            const auto &shape = otherMatrix.shape();
            if (shape.size() == 1 && shape.front() == size_) {
                return elementType_->convertibility(*otherMatrix.elementType());
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::SET: {
            const SetType &otherSet = dynamic_cast<const SetType &>(other);
            return elementType_->convertibility(*otherSet.valueType());
        }
        case TypeCode::MAP:
            [[fallthrough]];
        case TypeCode::DICT:
            [[fallthrough]];
        case TypeCode::UNION:
            return TypeConv::FORBIDDEN;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

TypeConv MatrixType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::MATRIX: {
            const MatrixType &otherMatrix = dynamic_cast<const MatrixType &>(other);
            if (shape_ == otherMatrix.shape()) {
                return elementType_->convertibility(*otherMatrix.elementType());
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::VECTOR: {
            const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
            if (shape_.size() == 1 && shape_.front() == otherVector.size()) {
                return elementType_->convertibility(*otherVector.elementType());
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::ARRAY: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            if (shape_.size() == 1 && (otherArray.size() == 0 || otherArray.size() == shape_.front())) {
                return elementType_->convertibility(*otherArray.elementType());
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::SET: {
            const SetType &otherSet = dynamic_cast<const SetType &>(other);
            return elementType_->convertibility(*otherSet.valueType());
        }
        case TypeCode::MAP:
            [[fallthrough]];
        case TypeCode::DICT:
            [[fallthrough]];
        case TypeCode::UNION:
            return TypeConv::FORBIDDEN;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

TypeConv ListType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::SET:
            [[fallthrough]];
        case TypeCode::MAP:
            [[fallthrough]];
        case TypeCode::ARRAY:
            [[fallthrough]];
        case TypeCode::DICT:
            [[fallthrough]];
        case TypeCode::UNION:
            [[fallthrough]];
        case TypeCode::VECTOR:
            [[fallthrough]];
        case TypeCode::MATRIX:
            return TypeConv::FORBIDDEN;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

void initTypes() {
    // initialize primitive types
    int32TypePtr = std::make_shared<PrimType>(TypeCode::INT32);
    int64TypePtr = std::make_shared<PrimType>(TypeCode::INT64);
    floatTypePtr = std::make_shared<PrimType>(TypeCode::FLOAT);
    doubleTypePtr = std::make_shared<PrimType>(TypeCode::DOUBLE);
    stringTypePtr = std::make_shared<PrimType>(TypeCode::STRING);
    boolTypePtr = std::make_shared<PrimType>(TypeCode::BOOL);
    charTypePtr = std::make_shared<PrimType>(TypeCode::CHAR);

    // initialize alias types
    intTypePtr = int32TypePtr;
    realTypePtr = floatTypePtr;
    numberTypePtr = doubleTypePtr;

    // initialize special types
    anyTypePtr = std::make_shared<SpecialType>(TypeCode::ANY);
    voidTypePtr = std::make_shared<SpecialType>(TypeCode::VOID);
    functorTypePtr = std::make_shared<SpecialType>(TypeCode::FUNCTOR);
}
