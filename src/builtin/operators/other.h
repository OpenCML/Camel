// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/type.h"

// Stands in for an operator that has no implementation yet. It faults in every build: an
// assertion alone is compiled out of a release build, where the operator then returned null
// (a comparison was false, an assignment did nothing) without a word.
[[noreturn]] slot_t
__not_implemented__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    camel::core::error::throwRuntimeFault(
        camel::core::error::RuntimeDiag::OperatorNotImplemented,
        "this operator");
}
