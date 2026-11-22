// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "macro.h"

#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/utils/assert.h"

using namespace camel::core::context;

slot_t __cmp__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
