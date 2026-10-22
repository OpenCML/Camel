// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"

slot_t __op_print__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __op_println__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __op_eprintln__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __op_input__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
