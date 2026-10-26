// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"

#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// The two probes of the module: the meeting point the calls of a test wait at, and the count of
// the threads this process has.
slot_t __test_support_rendezvous__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __test_support_thread_count__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
// A native operator with a generic declaration and `with` parameters that have defaults: it gives
// back its three `with` values as one number, so a test reads which defaults a call was given.
slot_t __test_support_with_probe__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
// The same for an array argument, so the operator is a set of two declarations.
slot_t __test_support_with_probe_array__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getTestSupportOpsMap();