// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once
#include "camel/core/operator.h"
#include "camel/core/type.h"
#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// rendezvous(parties, timeout_ms): waits until `parties` calls are waiting at the same time
// and returns the size of the group they form, or returns after `timeout_ms` with the number
// of calls that were waiting together (at least 1, the caller). A result of `parties` or more
// therefore proves that many calls were in flight at once; it is how the test suite asserts
// that a scheduler runs independent calls concurrently, without timing anything.
slot_t __para_rendezvous__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getParaOpsMap();
