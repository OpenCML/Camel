// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"

#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

slot_t __sim_pi_hits__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __sim_asian_call_price__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getSimOpsMap();
