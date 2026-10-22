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

} // namespace camel::passes::sched::fastvm
