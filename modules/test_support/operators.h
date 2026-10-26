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

std::unordered_map<std::string, operator_t> getTestSupportOpsMap();