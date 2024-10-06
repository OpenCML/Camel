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
 * Created: Oct. 6, 2024
 * Updated: Oct. 6, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "map.h"

using namespace std;

MapType::MapType(const type_ptr_t &keyType, const type_ptr_t &valueType)
    : StructType(TypeCode::MAP), keyType_(keyType), valueType_(valueType) {}

type_ptr_t MapType::keyType() const { return keyType_; }

type_ptr_t MapType::valueType() const { return valueType_; }

string MapType::toString() const { return "Map<" + keyType_->toString() + ", " + valueType_->toString() + ">"; }

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
