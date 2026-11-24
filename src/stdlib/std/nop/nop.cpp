// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `std::nop`: the pass that consumes the program and ends the pipeline. Its class is part of the
 * pass API (`NopPass`, camel/execute/pass/base.h), because a caller that has nothing to do
 * to a program still has to answer with one; the implementation and its registration live here.
 */

#include "camel/execute/pass/sched.h"
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

// The name is the pass's own (`NopPass::kName`): the question "does the pipeline end in the pass
// that executes nothing" is answered for a pipeline before any pass is built, and it reads the name
// from there, so this registration is not a second spelling of it.
const PassKernel nopKernel(NopPass::kName, makePass<NopPass>());

} // namespace