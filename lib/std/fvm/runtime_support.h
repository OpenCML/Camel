// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Shared FastVM interpreter helpers.
 *
 * This file holds bytecode-local execution semantics that are duplicated by
 * multiple interpreter backends. These helpers are intentionally scoped to the
 * FastVM bytecode layer rather than the runtime GCGraph layer.
 */

#pragma once

#include "bytecode.h"
#include "camel/core/context/frame.h"

namespace camel::passes::sched::fastvm {

size_t selectBranchArm(const Bytecode &bc, camel::core::context::Frame *frame);

/// What a call hands on, for an interpreter that has just returned a callee's result into the
/// caller's frame: the result itself, or the tuple of it and the new version of each captured
/// variable the callee wrote. `fastop[0]` of the caller's FUNC/TAIL bytecode names those
/// variables (it is 0 when the callee writes none), and the bytecode's operands hold their
/// values. `callerFrame` is the caller's frame, already restored.
slot_t funcValueForCall(
    const BytecodeHeader *call, camel::core::context::Frame *callerFrame, slot_t result);

} // namespace camel::passes::sched::fastvm
