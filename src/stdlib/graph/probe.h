// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The pass that exercises the check in Handle::create: it makes a handle that claims a node the
 * program does not have, and prints whether the mistake was refused where it was made.
 *
 * It is a test, not a member of any pipeline: naming it on the command line is the whole switch.
 */

#pragma once

namespace camel::graphmod {

/// Registers `graph::probe_handle`, the test-only pass that asks Handle::create to accept a node
/// the program's graph does not have and reports the answer.
void registerHandleProbePass();

} // namespace camel::graphmod