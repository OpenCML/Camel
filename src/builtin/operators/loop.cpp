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
 * Author: Camel Contributors
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The operators of loop.h.
 */

#include "loop.h"

#include "camel/core/context/frame.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/type/composite/array.h"

using namespace camel::core::context;

slot_t __loop_collector__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)norm;
    (void)ctx;
    return camel::core::rtdata::toSlot<::Array *>(::Array::create(camel::core::mm::autoSpace(), 0));
}

slot_t __loop_collect__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    auto *tape           = norm.get<::Array *>(0);
    const auto *tapeType = static_cast<const camel::core::type::ArrayType *>(norm.type(0));
    ASSERT(tape != nullptr && tapeType != nullptr, "A collection slot holds an array.");
    try {
        tape->append(norm.slot(1), tapeType);
    } catch (const camel::core::mm::MemoryExhausted &e) {
        // Growing a tape is the loop's allocation, as in loop_driver.h's native rounds; the
        // executor's instruction supplies the source location.
        throw e.during("collecting the values a loop produces each round", 0);
    }
    return norm.slot(0);
}