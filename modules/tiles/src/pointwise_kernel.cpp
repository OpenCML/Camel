// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Core pointwise domains drive operand addresses. Row-local schedules require
// canonical broadcast maps; general affine maps use the serial/fused lowerer.

#include "tiles/internal/kernel_ir.h"
#include "tiles/program.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace camel::tiles::kernel {
namespace {
namespace sem = core::sem;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(std::string("pointwise core contract: ") + message);
}
void contract(const Program &p, const Node &n) {
    require(n.indexing && n.semantic < p.contracts.size(), "missing index instance");
    const auto &inst = *n.indexing;
    const auto *decl = p.contracts[n.semantic].declaration;
    require(decl && !sem::validate(inst, decl), "invalid index instance");
    require(
        inst.extents() == std::optional<Shape>(n.shape) && inst.with.empty() &&
            inst.norm.size() == n.inputs.size() && inst.domain.guard.empty(),
        "unsupported iteration domain");
    require(
        inst.law == sem::kNoLaw && inst.output.mode == sem::WriteMode::Unique &&
            !inst.output.indirect && inst.output.valid.empty() &&
            inst.output.coords.size() == n.shape.size(),
        "unsupported output map");
    for (size_t d = 0; d < n.shape.size(); ++d) {
        const auto &coord = inst.exprs[inst.output.coords[d]];
        require(
            inst.domain.dims[d].role == sem::DimRole::Parallel && coord.op == sem::ExprOp::Dim &&
                coord.value == static_cast<int64_t>(d),
            "output is not the pointwise domain");
    }
    for (const auto &c : inst.domain.constraints)
        require(
            !inst.exprs.mentionsDim(c.lhs) && !inst.exprs.mentionsDim(c.rhs),
            "dimension-dependent constraint");
    require(
        sem::holds(inst, inst.domain.constraints, std::vector<int64_t>(n.shape.size())),
        "unsatisfied domain constraints");
}
} // namespace
IndexExpr
pointwiseAddress(const Program &p, const Node &n, size_t operand, const IndexExpr &linear) {
    contract(p, n);
    std::vector<IndexExpr> point(n.shape.size(), IndexExpr(0));
    size_t stride = 1;
    for (size_t d = n.shape.size(); d-- > 0;) {
        point[d] = (linear / IndexExpr(stride)) % IndexExpr(std::max<int64_t>(n.shape[d], 1));
        stride *= std::max<int64_t>(n.shape[d], 1);
    }
    const auto &shape = p.nodes.at(n.inputs.at(operand)).shape;
    auto address      = affineAddress(*n.indexing, n.indexing->norm.at(operand), shape, point);
    const auto view   = sem::rowMajorView(*n.indexing, n.indexing->norm.at(operand), shape);
    // Preserve the linear coordinate when the proven storage map is identity;
    // avoid forcing CUDA to rediscover mixed-radix decode/encode cancellation.
    bool identity   = view && view->offset == 0;
    size_t expected = 1;
    for (size_t d = n.shape.size(); identity && d-- > 0;) {
        identity = n.shape[d] <= 1 || view->strides[d] == static_cast<int64_t>(expected);
        expected *= std::max<int64_t>(n.shape[d], 1);
    }
    return identity ? linear : address;
}
bool canonicalPointwise(const Program &p, const Node &n) {
    if (!n.indexing)
        return true; // Explicit physical primitives need no graph declaration.
    try {
        contract(p, n);
        for (size_t input = 0; input < n.inputs.size(); ++input) {
            (void)pointwiseAddress(p, n, input, IndexExpr(0));
            const auto &shape = p.nodes[n.inputs[input]].shape;
            if (shape.size() > n.shape.size())
                return false;
            const auto view = sem::rowMajorView(*n.indexing, n.indexing->norm[input], shape);
            if (!view || view->offset != 0)
                return false;
            int64_t stride = 1;
            for (size_t d = n.shape.size(); d-- > 0;) {
                const auto shift  = n.shape.size() - shape.size();
                const auto extent = d < shift ? 1 : shape[d - shift];
                if (n.shape[d] > 1 && view->strides[d] != (extent == 1 ? 0 : stride))
                    return false;
                if (d >= shift)
                    stride *= std::max<int64_t>(extent, 1);
            }
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}
} // namespace camel::tiles::kernel
