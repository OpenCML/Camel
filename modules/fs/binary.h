// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Binary file kernels and crash-safe replacement, separate from text IO.

#pragma once
#include "camel/execute/executor.h"
slot_t fsReadBytes(ArgsView &, ArgsView &, camel::core::context::Context &);
slot_t fsWriteBytes(ArgsView &, ArgsView &, camel::core::context::Context &);
slot_t fsReplaceBytes(ArgsView &, ArgsView &, camel::core::context::Context &);
