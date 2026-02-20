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
 * Created: Sep. 22, 2025
 * Updated: Feb. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "this.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"

#include <iostream>

slot_t __zen__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::string zen =
#include "ZEN"
        ;
    String *str = String::from(zen, mm::autoSpace());
    return toSlot(str);
}
