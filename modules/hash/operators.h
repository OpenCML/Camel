// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"

#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

slot_t __hash_sha256_str__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __hash_sha256_bytes__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __hash_sha256_rounds__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);

std::unordered_map<std::string, operator_t> getHashOpsMap();
