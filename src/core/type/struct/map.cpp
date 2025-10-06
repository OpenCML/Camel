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

#include "map.h"

using namespace std;

MapType::MapType(const type_ptr_t &keyType, const type_ptr_t &valueType)
    : StructType(TypeCode::Map), keyType_(keyType), valueType_(valueType) {}

type_ptr_t MapType::keyType() const { return keyType_; }

type_ptr_t MapType::valueType() const { return valueType_; }

string MapType::toString() const {
    return "Map<" + keyType_->toString() + ", " + valueType_->toString() + ">";
}

std::optional<type_ptr_t> MapType::typeAt(struct_idx_t idx) const {
    ASSERT(false, "MapType does not support indexing");
    return std::nullopt;
}

bool MapType::operator==(const Type &other) const {
    if (other.code() != TypeCode::Map) {
        return false;
    }
    const MapType &otherMap = dynamic_cast<const MapType &>(other);

    return keyType_->equals(otherMap.keyType_) && valueType_->equals(otherMap.valueType_);
}

bool MapType::operator!=(const Type &other) const {
    if (other.code() != TypeCode::Map) {
        return true;
    }
    const MapType &otherMap = dynamic_cast<const MapType &>(other);

    return !keyType_->equals(otherMap.keyType_) || !valueType_->equals(otherMap.valueType_);
}

CastSafety MapType::castSafetyTo(const Type &other) const {
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::Map: {
            const MapType &otherMap = dynamic_cast<const MapType &>(other);
            const CastSafety keyConv = keyType_->castSafetyTo(*otherMap.keyType_);
            const CastSafety valueConv = valueType_->castSafetyTo(*otherMap.valueType_);
            if (keyConv == CastSafety::Forbidden || valueConv == CastSafety::Forbidden) {
                return CastSafety::Forbidden;
            }
            if (keyConv == CastSafety::Safe && valueConv == CastSafety::Safe) {
                return CastSafety::Safe;
            }
            return CastSafety::Unsafe;
        }
        case TypeCode::Set:
            [[fallthrough]];
        case TypeCode::Array:
            [[fallthrough]];
        case TypeCode::List:
            [[fallthrough]];
        case TypeCode::Dict:
            [[fallthrough]];
        case TypeCode::Union:
            [[fallthrough]];
        case TypeCode::Vector:
            [[fallthrough]];
        case TypeCode::Tensor:
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
