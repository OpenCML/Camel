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
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Makes types of ever more members until the permanent space, where types are interned, is full:
 * the pass then ends with RuntimeDiag::MemoryExhausted, as any pass does that meets the limit.
 * Without a limit it stops after a bounded number of types and leaves the program as it is.
 */
#include "camel/passes/test/exhaust_perm_space.h"

#include "camel/core/type/composite/tuple.h"

const camel::runtime::Program *
ExhaustPermSpaceTestPass::apply(const camel::runtime::Program *program, std::ostream &) {
    using camel::core::type::TupleType;
    using camel::core::type::Type;
    for (size_t members = 1; members <= 4096; ++members) {
        (void)TupleType::create(std::vector<Type *>(members, Type::Int64()));
    }
    return program;
}
