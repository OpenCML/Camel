// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"

slot_t __profiler_begin__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __profiler_end__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __profiler_instant__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __profiler_enable__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
