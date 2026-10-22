// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Type-directed transfers preserve struct declarations and tuple nesting.

#pragma once
#include "camel/execute/executor.h"

namespace camel::tiles {
core::type::Type *transferTreeType(core::type::Type *type, bool upload);
slot_t transferTree(ArgsView &norm, bool upload);
} // namespace camel::tiles
