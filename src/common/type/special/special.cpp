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
 * Updated: Oct. 08, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "special.h"

TypeConv SpecialType::convertibility(const Type &other) const {
    if (other.code() == code_) {
        return TypeConv::SAFE;
    }
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    if (other.primary() || other.structured()) {
        return TypeConv::FORBIDDEN;
    }
    if (other.code() == TypeCode::VOID) {
        return TypeConv::UNSAFE;
    }
    return TypeConv::FORBIDDEN;
}
