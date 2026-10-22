// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Block-local row regions: ordered stages, shared lifetimes and explicit reduction laws.

#pragma once
#include "tiles/internal/kernel_ir.h"
#include "tiles/program.h"
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace camel::tiles::kernel {
struct RowStage {
    size_t node;
    std::optional<ScalarExpr> expression;
    std::optional<core::sem::ReductionLaw> reduction;
};
struct RowKernel {
    size_t rows, width, sharedElements;
    size_t orderedElements = 0;
    std::vector<RowStage> stages;
    std::vector<size_t> offsets, widths;
    std::vector<size_t> members() const;
};
std::optional<RowKernel> rowKernel(const Program &program);
KernelBody rowBody(const Program &program, const RowKernel &kernel, Launch &launch);
} // namespace camel::tiles::kernel
