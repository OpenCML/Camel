// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"

/// A second name of a mutable value, as an ordinary operator: `copy` of `lib/std/prelude.cml`.
/// The answer is the one the three backends' COPY arms give (plan 2.3, step 1): a value the
/// collector traces is cloned with its own type, and everything else -- a scalar, a value whose
/// parts no write could be seen through -- is the slot itself.
slot_t __copy__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);