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
 * Created: Oct. 6, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "special.h"

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