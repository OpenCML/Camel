// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
