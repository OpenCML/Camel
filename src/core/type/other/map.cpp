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
 * Updated: Oct. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "map.h"

using namespace std;

MapType::MapType(const type_ptr_t &keyType, const type_ptr_t &valueType)
    : OtherType(typeCode()), keyType_(keyType), valueType_(valueType) {}

type_ptr_t MapType::keyType() const { return keyType_; }

type_ptr_t MapType::valueType() const { return valueType_; }

string MapType::toString() const {
    return "Map<" + keyType_->toString() + ", " + valueType_->toString() + ">";
}

std::string MapType::mangle() const {
    std::string result = "M";
    result += keyType_->mangle();
    result += valueType_->mangle();
    return result;
}

bool MapType::operator==(const Type &other) const {
    if (this == &other) {
        return true;
    }
    if (other.code() != typeCode()) {
        return false;
    }
    const MapType &otherMap = dynamic_cast<const MapType &>(other);

    return keyType_->equals(otherMap.keyType_) && valueType_->equals(otherMap.valueType_);
}

bool MapType::operator!=(const Type &other) const {
    if (other.code() != typeCode()) {
        return true;
    }
    const MapType &otherMap = dynamic_cast<const MapType &>(other);

    return !keyType_->equals(otherMap.keyType_) || !valueType_->equals(otherMap.valueType_);
}

CastSafety MapType::castSafetyTo(const Type &other) const {
    if (this == &other) {
        return CastSafety::Safe;
    }
    if (other.code() == typeCode()) {
        return CastSafety::Safe;
    }
    if (other.composed()) {
        switch (other.code()) {
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
