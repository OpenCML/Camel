// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Makes types of ever more members until the permanent space, where types are interned, is full:
 * the pass then ends with RuntimeDiag::MemoryExhausted, as any pass does that meets the limit.
 * Without a limit it stops after a bounded number of types and leaves the program as it is.
 */

#include "camel/execute/pass/registration.h"
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

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The test-only passes this file implements. Each name is the constant its own header
/// declares, so the name a pipeline writes and the class that answers it are one fact.
const PassKernel exhaustPermSpaceKernel(kExhaustPermSpaceTestPass, makePass<ExhaustPermSpaceTestPass>());

} // namespace
