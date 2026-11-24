// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `std::nop`: the pass that consumes the program and ends the pipeline. Its class is part of the
 * pass API (`NopPass`, camel/execute/pass/base.h), because a caller that has nothing to do
 * to a program still has to answer with one; the implementation and its registration live here.
 */

#include "camel/execute/pass/base.h"
#include "camel/execute/pass/registration.h"

#include <ostream>

const camel::runtime::Program *
NopPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    (void)os;
    return nullptr;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

const PassKernel nopKernel("std::nop", makePass<NopPass>());

} // namespace