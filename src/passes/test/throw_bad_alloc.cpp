// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Throws a bare `std::bad_alloc` from inside a pass, which is what a `std::vector` or a
 * `std::string` does when the host has no memory left. No allocation is actually attempted: the
 * point is the exception type, so that the run shows which diagnostic the pass loop makes of it.
 */

#include "camel/execute/pass/registration.h"
#include "camel/passes/test/throw_bad_alloc.h"

#include <new>

const camel::runtime::Program *
ThrowBadAllocTestPass::apply(const camel::runtime::Program *program, std::ostream &) {
    throw std::bad_alloc();
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The test-only passes this file implements. Each name is the constant its own header
/// declares, so the name a pipeline writes and the class that answers it are one fact.
const PassKernel throwBadAllocKernel(kThrowBadAllocTestPass, makePass<ThrowBadAllocTestPass>());

} // namespace
