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

#include <iostream>

#include "data.h"
#include "entity.h"
#include "function.h"
#include "functor.h"
#include "type.h"

using namespace std;

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

type_ptr_t listTypePtr;

type_ptr_t anyTypePtr;
type_ptr_t voidTypePtr;
type_ptr_t functorTypePtr;

type_ptr_t refTypePtr;

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

string typeCodeToString(TypeCode code) {
    switch (code) {
        // primitive types
    case TypeCode::INT32:
        return "Int32";
    case TypeCode::INT64:
        return "Int64";
    case TypeCode::FLOAT:
        return "Float";
    case TypeCode::DOUBLE:
        return "Double";
    case TypeCode::STRING:
        return "String";
    case TypeCode::BOOL:
        return "Bool";
    case TypeCode::CHAR:
        return "Char";
        // structured types
    case TypeCode::SET:
        return "Set";
    case TypeCode::MAP:
        return "Map";
    case TypeCode::LIST:
        return "List";
    case TypeCode::DICT:
        return "Dict";
    case TypeCode::ARRAY:
        return "Array";
    case TypeCode::TUPLE:
        return "Tuple";
    case TypeCode::UNION:
        return "Union";
    case TypeCode::VECTOR:
        return "Vector";
    case TypeCode::TENSOR:
        return "Tensor";
    case TypeCode::PARAMS:
        return "Params";
        // special types
    case TypeCode::ANY:
        return "Any";
    case TypeCode::VOID:
        return "Void";
    case TypeCode::FUNCTOR:
        return "Functor";
    case TypeCode::REF:
        return "REF";
    }
    return "Unknown";
}

/*
PrimType
*/

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
        case TypeCode::TENSOR:
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

/*
SpecialType
*/

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

/*
SetType
*/

SetType::SetType(const type_ptr_t &valueType) : StructType(TypeCode::SET), valueType_(valueType) {}

type_ptr_t SetType::valueType() const { return valueType_; }

std::string SetType::toString() const { return "Set<" + valueType_->toString() + ">"; }

bool SetType::operator==(const Type &other) const {
    if (other.code() != TypeCode::SET) {
        return false;
    }
    const SetType &otherMap = dynamic_cast<const SetType &>(other);

    return valueType_->equals(otherMap.valueType_);
}
bool SetType::operator!=(const Type &other) const {
    if (other.code() != TypeCode::SET) {
        return true;
    }
    const SetType &otherMap = dynamic_cast<const SetType &>(other);

    return !valueType_->equals(otherMap.valueType_);
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
            if (otherArray.size() == 1) {
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
        case TypeCode::VECTOR: {
            const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
            return valueType_->convertibility(*otherVector.elementType());
        }
        case TypeCode::TENSOR:
            return TypeConv::FORBIDDEN;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

/*
MapType
*/

MapType::MapType(const type_ptr_t &keyType, const type_ptr_t &valueType)
    : StructType(TypeCode::MAP), keyType_(keyType), valueType_(valueType) {}

type_ptr_t MapType::keyType() const { return keyType_; }

type_ptr_t MapType::valueType() const { return valueType_; }

std::string MapType::toString() const { return "Map<" + keyType_->toString() + ", " + valueType_->toString() + ">"; }

bool MapType::operator==(const Type &other) const {
    if (other.code() != TypeCode::MAP) {
        return false;
    }
    const MapType &otherMap = dynamic_cast<const MapType &>(other);

    return keyType_->equals(otherMap.keyType_) && valueType_->equals(otherMap.valueType_);
}

bool MapType::operator!=(const Type &other) const {
    if (other.code() != TypeCode::MAP) {
        return true;
    }
    const MapType &otherMap = dynamic_cast<const MapType &>(other);

    return !keyType_->equals(otherMap.keyType_) || !valueType_->equals(otherMap.valueType_);
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
        case TypeCode::TENSOR:
            return TypeConv::FORBIDDEN;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

/*
DictType
*/

DictType::DictType() : StructType(TypeCode::DICT) {}

std::string DictType::toString() const {
    if (fields_.empty()) {
        return "{}";
    }
    std::string result = "{ ";
    for (const auto &field : fields_) {
        result += field.first + ": ";
        if (field.second) {
            result += field.second->toString() + ", ";
        } else {
            result += "NULL, ";
        }
    }
    result.pop_back();
    result.pop_back();
    result += " }";
    return result;
}

bool DictType::operator==(const Type &other) const {
    if (other.code() != TypeCode::DICT) {
        return false;
    }
    const DictType &otherDict = dynamic_cast<const DictType &>(other);

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

bool DictType::operator!=(const Type &other) const { return !(*this == other); }

bool DictType::add(const std::string &name, const type_ptr_t &type) {
    if (has(name)) {
        return false;
    }
    fields_[name] = type;
    return true;
}

bool DictType::del(const std::string &name) { return fields_.erase(name) > 0; }

bool DictType::has(const std::string &name) const { return fields_.find(name) != fields_.end(); }

void DictType::set(const std::string &name, const type_ptr_t &type) { fields_.at(name) = type; }

type_ptr_t DictType::get(const std::string &name) const { return fields_.at(name); }

void DictType::clear() { fields_.clear(); }

type_ptr_t DictType::operator|(const DictType &other) const {
    auto result = std::make_shared<DictType>();
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
            result->set(ident, type);
        }
    }
    return result;
}

type_ptr_t DictType::operator&(const DictType &other) const {
    auto result = std::make_shared<DictType>();
    for (const auto &field : fields_) {
        const auto &ident = field.first;
        if (other.has(ident)) {
            const type_ptr_t &otherType = other.get(ident);
            result->add(ident, otherType);
        }
    }
    return result;
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
        case TypeCode::TENSOR:
            return TypeConv::FORBIDDEN;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

/*
ListType
*/

ListType::ListType() : StructType(TypeCode::LIST) {}

std::string ListType::toString() const { return "List"; }

bool ListType::operator==(const Type &other) const { return true; }

bool ListType::operator!=(const Type &other) const { return false; }

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
        case TypeCode::TENSOR:
            return TypeConv::FORBIDDEN;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

/*
UnionType
*/

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

UnionType::UnionType(const std::initializer_list<type_ptr_t> &types) : StructType(TypeCode::UNION) {
    for (const auto &type : types) {
        if (type->code() == TypeCode::UNION)
            insertUnion(dynamic_cast<const UnionType &>(*type));
        else
            types_.insert(type);
    }
}

UnionType::UnionType(const std::vector<type_ptr_t> &types) : StructType(TypeCode::UNION) {
    for (const auto &type : types) {
        if (type->code() == TypeCode::UNION)
            insertUnion(dynamic_cast<const UnionType &>(*type));
        else
            types_.insert(type);
    }
}

std::string UnionType::toString() const {
    std::string result = "Union<";
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
            const TensorType &otherMatrix = dynamic_cast<const TensorType &>(other);
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
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

/*
ArrayType
*/

ArrayType::ArrayType(const type_ptr_t &elementType, size_t size)
    : StructType(TypeCode::ARRAY), size_(size), elementType_(elementType) {}

size_t ArrayType::size() const { return size_; }

type_ptr_t ArrayType::elementType() const { return elementType_; }

std::string ArrayType::toString() const {
    return "Array<" + elementType_->toString() + ", " + std::to_string(size_) + ">";
}

bool ArrayType::operator==(const Type &other) const {
    if (other.code() != TypeCode::ARRAY) {
        return false;
    }
    const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
    return size_ == otherArray.size_ && elementType_->equals(otherArray.elementType_);
}

bool ArrayType::operator!=(const Type &other) const {
    if (other.code() != TypeCode::ARRAY) {
        return true;
    }
    const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
    return size_ != otherArray.size_ || !elementType_->equals(otherArray.elementType_);
}

TypeConv ArrayType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::ARRAY: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            return elementType_->convertibility(*otherArray.elementType());
        }
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::SET: {
            const SetType &otherSet = dynamic_cast<const SetType &>(other);
            return elementType_->convertibility(*otherSet.valueType());
        }
        case TypeCode::VECTOR: {
            // we cannot know the size of the array
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::TENSOR: {
            // we cannot know the size of the array
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
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

/*
TupleType
*/

TupleType::TupleType() : StructType(TypeCode::TUPLE) {}

TupleType::TupleType(const std::initializer_list<type_ptr_t> &types) : StructType(TypeCode::TUPLE), types_(types) {}

TupleType::TupleType(const std::vector<type_ptr_t> &types) : StructType(TypeCode::TUPLE), types_(types) {}

string TupleType::toString() const {
    string result = "Tuple<";
    for (const auto &type : types_) {
        if (type) {
            result += type->toString() + ", ";
        } else {
            result += "NULL, ";
        }
    }
    if (!types_.empty()) {
        result.pop_back();
        result.pop_back();
    }
    result += ">";
    return result;
}

bool TupleType::operator==(const Type &other) const {
    if (other.code() != TypeCode::TUPLE) {
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

size_t TupleType::size() const { return types_.size(); }

const std::vector<type_ptr_t> &TupleType::types() const { return types_; }

TypeConv TupleType::convertibility(const Type &other) const {
    // TODO: not fully implemented
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::TENSOR:
            return TypeConv::SAFE;
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::SET:
            return TypeConv::SAFE;
        case TypeCode::ARRAY:
            return TypeConv::SAFE;
        case TypeCode::VECTOR:
            return TypeConv::SAFE;
        case TypeCode::MAP:
            return TypeConv::SAFE;
        case TypeCode::DICT:
            return TypeConv::SAFE;
        case TypeCode::UNION:
            return TypeConv::SAFE;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

/*
VectorType
*/

VectorType::VectorType(const type_ptr_t &elementType) : StructType(TypeCode::VECTOR), elementType_(elementType) {}

type_ptr_t VectorType::elementType() const { return elementType_; }

std::string VectorType::toString() const { return "Vector<" + elementType_->toString() + ">"; }

bool VectorType::operator==(const Type &other) const {
    if (other.code() != TypeCode::VECTOR) {
        return false;
    }
    const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
    return elementType_->equals(otherVector.elementType_);
}

bool VectorType::operator!=(const Type &other) const {
    if (other.code() != TypeCode::VECTOR) {
        return true;
    }
    const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
    return !elementType_->equals(otherVector.elementType_);
}

TypeConv VectorType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::VECTOR: {
            const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
            return elementType_->convertibility(*otherVector.elementType());
        }
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::ARRAY: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            return elementType_->convertibility(*otherArray.elementType());
        }
        case TypeCode::TENSOR: {
            const TensorType &otherMatrix = dynamic_cast<const TensorType &>(other);
            const auto &shape = otherMatrix.shape();
            return elementType_->convertibility(*otherMatrix.elementType());
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
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

/*
ParamsType
*/

TypeConv ParamsType::convertibilityToParams(const ParamsType &other) const {
    // TODO: not fully implemented
    if (elements_.size() != other.elements_.size()) {
        return TypeConv::FORBIDDEN;
    }
    TypeConv result = TypeConv::SAFE;
    for (size_t i = 0; i < elements_.size(); i++) {
        const auto &[name, type, value] = elements_[i];
        const auto &[otherName, otherType, otherValue] = other.elements_[i];
        if (name != otherName) {
            return TypeConv::FORBIDDEN;
        }
        TypeConv paramConv = type->convertibility(*otherType);
        if (paramConv == TypeConv::FORBIDDEN) {
            return TypeConv::FORBIDDEN;
        } else if (paramConv == TypeConv::UNSAFE) {
            result = TypeConv::UNSAFE;
        }
    }
    return result;
}

ParamsType::ParamsType() : StructType(TypeCode::PARAMS) {}

string ParamsType::toString() const {
    string result = "Params<";
    for (const auto &tuple : elements_) {
        auto &[name, type, value] = tuple;
        result += (name.empty() ? "" : name + ": ") + (type ? type->toString() : "NULL");
        if (value) {
            result += " = " + value->toString();
        }
        result += ", ";
    }
    if (elements_.size() > 0) {
        result.pop_back();
        result.pop_back();
    }
    result += ">";
    return result;
}

bool ParamsType::operator==(const Type &other) const {
    if (other.code() != TypeCode::PARAMS) {
        return false;
    }
    const ParamsType &otherParam = dynamic_cast<const ParamsType &>(other);

    if (elements_.size() != otherParam.elements_.size()) {
        return false;
    }
    for (size_t i = 0; i < elements_.size(); i++) {
        auto &[name, type, value] = elements_[i];
        auto &[otherName, otherType, otherValue] = otherParam.elements_[i];
        if (name != otherName) {
            return false;
        }
        if (!type->equals(otherType)) {
            return false;
        }
        if (value && !value->equals(otherValue)) {
            return false;
        }
    }
    return true;
}

bool ParamsType::operator!=(const Type &other) const { return !(*this == other); }

bool ParamsType::add(const std::string &key, const type_ptr_t &type, const data_ptr_t &value = nullptr) {
    // here we allow duplicate keys, for the sake of simplicity
    // we use "" to represent the unnamed parameter
    elements_.push_back({key, type, value});
    return true;
}

size_t ParamsType::size() const { return elements_.size(); }

const std::vector<std::tuple<std::string, type_ptr_t, data_ptr_t>> &ParamsType::elements() const { return elements_; }

std::map<std::string, type_ptr_t> ParamsType::map() const {
    auto result = std::map<std::string, type_ptr_t>();
    for (const auto &tuple : elements_) {
        const auto &[name, type, value] = tuple;
        result[name] = type;
    }
    return result;
}

void ParamsType::clear() { elements_.clear(); }

TypeConv ParamsType::convertibility(const Type &other) const {
    // TODO: not fully implemented
    // TODO: others' convertibility should be checked
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::PARAMS: {
            const ParamsType &otherParam = dynamic_cast<const ParamsType &>(other);
            if (elements_.size() != otherParam.elements_.size()) {
                return TypeConv::FORBIDDEN;
            }
            TypeConv result = TypeConv::SAFE;
            for (size_t i = 0; i < elements_.size(); i++) {
                const auto &[name, type, value] = elements_[i];
                if (name != name) {
                    return TypeConv::FORBIDDEN;
                }
                TypeConv paramConv = type->convertibility(*type);
                if (paramConv == TypeConv::FORBIDDEN) {
                    return TypeConv::FORBIDDEN;
                } else if (paramConv == TypeConv::UNSAFE) {
                    result = TypeConv::UNSAFE;
                }
            }
            return result;
        }
        case TypeCode::UNION:
            return TypeConv::SAFE;
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::SET:
            return TypeConv::SAFE;
        case TypeCode::ARRAY:
            return TypeConv::SAFE;
        case TypeCode::VECTOR:
            return TypeConv::SAFE;
        case TypeCode::TENSOR:
            return TypeConv::SAFE;
        case TypeCode::MAP:
            return TypeConv::SAFE;
        case TypeCode::DICT:
            return TypeConv::SAFE;

        default:
            return TypeConv::FORBIDDEN;
        }
    }
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

/*
TensorType
*/

TensorType::TensorType(const type_ptr_t &elementType, const std::vector<size_t> &shape)
    : StructType(TypeCode::TENSOR), elementType_(elementType), shape_(shape) {
    if (shape_.size() == 0) {
        throw std::invalid_argument("Tensor shape must at least have 1 dim");
    }
    // element type must be a primitive type
    if (!elementType->primitive()) {
        throw std::invalid_argument("Tensor element type must be primitive");
    }
}

std::vector<size_t> TensorType::shape() const { return shape_; }

type_ptr_t TensorType::elementType() const { return elementType_; }

std::string TensorType::toString() const {
    std::string result = "Tensor<" + elementType_->toString() + ", [";
    for (const auto &dim : shape_) {
        result += std::to_string(dim) + ", ";
    }
    if (!shape_.empty()) {
        result.pop_back();
        result.pop_back();
    }
    result += "]>";
    return result;
}

bool TensorType::operator==(const Type &other) const {
    if (other.code() != TypeCode::TENSOR) {
        return false;
    }
    const TensorType &otherMatrix = dynamic_cast<const TensorType &>(other);
    return shape_ == otherMatrix.shape_ && elementType_->equals(otherMatrix.elementType_);
}

bool TensorType::operator!=(const Type &other) const {
    if (other.code() != TypeCode::TENSOR) {
        return true;
    }
    const TensorType &otherMatrix = dynamic_cast<const TensorType &>(other);
    return shape_ != otherMatrix.shape_ || !elementType_->equals(otherMatrix.elementType_);
}

TypeConv TensorType::convertibility(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::TENSOR: {
            const TensorType &otherMatrix = dynamic_cast<const TensorType &>(other);
            if (shape_ == otherMatrix.shape()) {
                return elementType_->convertibility(*otherMatrix.elementType());
            }
            return TypeConv::FORBIDDEN;
        }
        case TypeCode::LIST:
            return TypeConv::SAFE;
        case TypeCode::VECTOR: {
            const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
            return elementType_->convertibility(*otherVector.elementType());
        }
        case TypeCode::ARRAY: {
            const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
            return elementType_->convertibility(*otherArray.elementType());
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
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

/*
FunctorType
*/

FunctorType::FunctorType(const std::shared_ptr<ParamsType> &withType = nullptr,
                         const std::shared_ptr<ParamsType> &paramsType = nullptr,
                         const type_ptr_t &returnType = nullptr)
    : SpecialType(TypeCode::FUNCTOR), withType_(withType), paramsType_(paramsType), returnType_(returnType) {}

void FunctorType::addModifier(FunctionModifier modifier) { modifiers_.insert(modifier); }

void FunctorType::setModifiers(const std::unordered_set<FunctionModifier> &modifiers) { modifiers_ = modifiers; }

bool FunctorType::addIdent(const std::string &ident) {
    if (innerIdents_.find(ident) != innerIdents_.end()) {
        return false;
    }
    innerIdents_.insert(ident);
    return true;
}

bool FunctorType::hasSideEffect() const { return hasSideEffect_; }

const std::unordered_set<std::string> &FunctorType::innerIdents() const { return innerIdents_; }

type_ptr_t FunctorType::withType() const { return std::dynamic_pointer_cast<Type>(withType_); }

type_ptr_t FunctorType::paramsType() const { return std::dynamic_pointer_cast<Type>(paramsType_); }

type_ptr_t FunctorType::returnType() const { return std::dynamic_pointer_cast<Type>(returnType_); }

string FunctorType::toString() const {
    string result = "";
    for (const auto &modifier : modifiers_) {
        result += modifier2str(modifier) + " ";
    }
    if (withType_ && withType_->size() > 0) {
        result += "<";
        const auto &with = dynamic_cast<const ParamsType &>(*withType_);
        const auto &elements = with.elements();
        for (const auto &tuple : elements) {
            const auto &[name, type, value] = tuple;
            result += name + ": " + type->toString();
            if (value) {
                result += " = " + value->toString();
            }
            result += ", ";
        }
        if (elements.size() > 0) {
            result.pop_back();
            result.pop_back();
        }
        result += "> ";
    }
    result += "(";
    if (paramsType_ && paramsType_->size() > 0) {
        const auto &params = dynamic_cast<const ParamsType &>(*paramsType_);
        const auto &elements = params.elements();
        for (const auto &tuple : elements) {
            const auto &[name, type, value] = tuple;
            result += name + ": " + type->toString();
            if (value) {
                result += " = " + value->toString();
            }
            result += ", ";
        }
        if (elements.size() > 0) {
            result.pop_back();
            result.pop_back();
        }
    }
    result += ") => ";
    if (returnType_) {
        result += returnType_->toString();
    } else {
        result += "NULL";
    }
    return result;
}

bool FunctorType::operator==(const Type &other) const {
    if (other.code() != TypeCode::FUNCTOR) {
        return false;
    }
    const FunctorType &otherFunctor = dynamic_cast<const FunctorType &>(other);
    if (withType_ != nullptr && !withType_->equals(otherFunctor.withType_)) {
        return false;
    }
    if (paramsType_ != nullptr && !paramsType_->equals(otherFunctor.paramsType_)) {
        return false;
    }
    if (returnType_ != nullptr && !returnType_->equals(otherFunctor.returnType_)) {
        return false;
    }
    return true;
}

bool FunctorType::operator!=(const Type &other) const { return !(*this == other); }

TypeConv FunctorType::convertibility(const Type &other) const {
    // TODO: not fully implemented
    if (other.code() == TypeCode::FUNCTOR) {
        TypeConv result = TypeConv::SAFE;
        const FunctorType &otherFunctor = dynamic_cast<const FunctorType &>(other);
        if (withType_ && !otherFunctor.withType_) {
            const TypeConv withTypeConv = withType_->convertibility(*otherFunctor.withType_);
            if (withTypeConv == TypeConv::FORBIDDEN) {
                return TypeConv::FORBIDDEN;
            }
            if (withTypeConv == TypeConv::UNSAFE) {
                result = TypeConv::UNSAFE;
            }
        }
        if (paramsType_ && !otherFunctor.paramsType_) {
            const TypeConv paramsTypeConv = paramsType_->convertibility(*otherFunctor.paramsType_);
            if (paramsTypeConv == TypeConv::FORBIDDEN) {
                return TypeConv::FORBIDDEN;
            }
            if (paramsTypeConv == TypeConv::UNSAFE) {
                result = TypeConv::UNSAFE;
            }
        }
        if (returnType_ && !otherFunctor.returnType_) {
            const TypeConv returnTypeConv = returnType_->convertibility(*otherFunctor.returnType_);
            if (returnTypeConv == TypeConv::FORBIDDEN) {
                return TypeConv::FORBIDDEN;
            }
            if (returnTypeConv == TypeConv::UNSAFE) {
                result = TypeConv::UNSAFE;
            }
        }
        return result;
    }
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}

void initTypes() {
    // initialize primitive types
    int32TypePtr = dynamic_pointer_cast<Type>(make_shared<PrimType>(TypeCode::INT32));
    int64TypePtr = dynamic_pointer_cast<Type>(make_shared<PrimType>(TypeCode::INT64));
    floatTypePtr = dynamic_pointer_cast<Type>(make_shared<PrimType>(TypeCode::FLOAT));
    doubleTypePtr = dynamic_pointer_cast<Type>(make_shared<PrimType>(TypeCode::DOUBLE));
    stringTypePtr = dynamic_pointer_cast<Type>(make_shared<PrimType>(TypeCode::STRING));
    boolTypePtr = dynamic_pointer_cast<Type>(make_shared<PrimType>(TypeCode::BOOL));
    charTypePtr = dynamic_pointer_cast<Type>(make_shared<PrimType>(TypeCode::CHAR));

    // initialize alias types
    intTypePtr = int32TypePtr;
    realTypePtr = floatTypePtr;
    numberTypePtr = doubleTypePtr;

    // initialize structured types
    listTypePtr = dynamic_pointer_cast<Type>(make_shared<ListType>());

    // initialize special types
    anyTypePtr = dynamic_pointer_cast<Type>(make_shared<SpecialType>(TypeCode::ANY));
    voidTypePtr = dynamic_pointer_cast<Type>(make_shared<SpecialType>(TypeCode::VOID));
    functorTypePtr = dynamic_pointer_cast<Type>(make_shared<FunctorType>(nullptr, nullptr, anyTypePtr));

    // initialize unknown type
    refTypePtr = dynamic_pointer_cast<Type>(make_shared<SpecialType>(TypeCode::REF));
}
