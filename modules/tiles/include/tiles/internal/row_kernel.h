/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
