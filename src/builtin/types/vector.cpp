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

#include "vector.h"

using namespace std;

VectorType::VectorType(const type_ptr_t &elementType, size_t size)
    : OtherType(typeCode()), size_(size), elementType_(elementType) {}

size_t VectorType::size() const { return size_; }

type_ptr_t VectorType::elementType() const { return elementType_; }

string VectorType::toString() const {
    return "Vector<" + elementType_->toString() + ", " + to_string(size_) + ">";
}

std::string VectorType::mangle() const {
    std::string result = "A";
    result += elementType_->mangle();
    result += std::to_string(size_);
    return result;
}

bool VectorType::operator==(const Type &other) const {
    if (this == &other) {
        return true;
    }
    if (other.code() != typeCode()) {
        return false;
    }
    const VectorType &otherVec = dynamic_cast<const VectorType &>(other);
    return size_ == otherVec.size_ && elementType_->equals(otherVec.elementType_);
}

bool VectorType::operator!=(const Type &other) const {
    if (other.code() != typeCode()) {
        return true;
    }
    const VectorType &otherVec = dynamic_cast<const VectorType &>(other);
    return size_ != otherVec.size_ || !elementType_->equals(otherVec.elementType_);
}

CastSafety VectorType::castSafetyTo(const Type &other) const {
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
