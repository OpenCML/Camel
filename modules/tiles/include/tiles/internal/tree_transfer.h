// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Type-directed transfers preserve struct declarations and tuple nesting.

#pragma once
#include "camel/execute/executor.h"
#include "tiles/program.h"
#include <cstddef>
#include <span>

namespace camel::tiles {
core::type::Type *transferTreeType(core::type::Type *type, bool upload);
slot_t transferTree(ArgsView &norm, bool upload);
// What one downloaded leaf became: the host value of the element type the leaf says it holds. A
// caller that already has the bytes -- a resident leaf, or a check -- reaches the same readback
// this way, without a device in the path.
slot_t hostLeafValue(
    core::type::Type *type, const Shape &shape, DType dtype, std::span<const std::byte> data);
} // namespace camel::tiles
