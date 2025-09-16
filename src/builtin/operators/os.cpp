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
 * Created: Jul. 29, 2025
 * Updated: Jul. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "os.h"
#include "core/context/context.h"

data_ptr_t __sleep__(Context &ctx, data_vec_t &with, data_vec_t &norm) {
    if (norm.size() != 1) {
        ctx.rtmDiags()->of(RuntimeDiag::IncorrectArgsCount).commit("<sleep>", 1, norm.size());
        return Data::null();
    }
    data_ptr_t arg = norm[0];
    if (!Type::castSafetyCheck(arg->type(), Type::Int64())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<sleep>", "int64", arg->type()->toString());
        return Data::null();
    }
    auto pd = arg->as<Int64Data>(Type::Int64());
    if (pd->data() < 0) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<sleep> requires a non-negative integer");
        return Data::null();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(pd->data()));
    return Data::null();
}
