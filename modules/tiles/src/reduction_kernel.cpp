// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Bind reduction traversal to the core domain and checked operand map.

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
        throw std::runtime_error(std::string("reduction core contract: ") + message);
}
struct Domain {
    Shape extents;
    std::vector<bool> reduced;
    std::vector<size_t> outputAxes; // Domain dim -> output axis; SIZE_MAX means fixed zero.
    size_t count = 1;
};
Domain domain(const Program &p, const Node &n) {
    require(n.indexing && n.semantic < p.contracts.size(), "missing index instance");
    const auto &inst = *n.indexing;
    const auto *decl = p.contracts[n.semantic].declaration;
    require(decl && !sem::validate(inst, decl), "invalid index instance");
    const auto extents = inst.extents();
    require(
        extents && inst.with.empty() && !inst.norm.empty() && inst.domain.guard.empty(),
        "unsupported iteration domain");
    elements(*extents); // Checks the complete rectangular domain, independently of storage.
    for (size_t i = 1; i < inst.norm.size(); ++i)
        require(inst.norm[i].kind == sem::AccessKind::None, "unexpected data operand");
    for (const auto &c : inst.domain.constraints)
        require(
            !inst.exprs.mentionsDim(c.lhs) && !inst.exprs.mentionsDim(c.rhs),
            "dimension-dependent constraint");
    require(
        sem::holds(inst, inst.domain.constraints, std::vector<int64_t>(extents->size())),
        "unsatisfied domain constraints");
    require(
        inst.output.mode == sem::WriteMode::Combine && !inst.output.indirect &&
            inst.output.valid.empty() && inst.output.coords.size() == n.shape.size(),
        "unsupported output map");
    Domain result{
        *extents,
        std::vector<bool>(extents->size()),
        std::vector<size_t>(extents->size(), SIZE_MAX)};
    Shape reductions;
    for (size_t d = 0; d < extents->size(); ++d) {
        const auto role = inst.domain.dims[d].role;
        require(
            role == sem::DimRole::Parallel || role == sem::DimRole::Reduction,
            "unsupported dimension roles");
        result.reduced[d] = role == sem::DimRole::Reduction;
        if (result.reduced[d])
            reductions.push_back((*extents)[d]);
    }
    result.count = elements(reductions);
    for (size_t out = 0; out < inst.output.coords.size(); ++out) {
        const auto coord =
            inst.exprs.affine(inst.output.coords[out], extents->size(), inst.paramValues());
        require(coord && coord->constant == 0, "output is not the reduction projection");
        size_t dimension = SIZE_MAX;
        for (size_t d = 0; d < extents->size(); ++d)
            if (coord->coeffs[d]) {
                require(
                    coord->coeffs[d] == 1 && dimension == SIZE_MAX && !result.reduced[d],
                    "output is not the reduction projection");
                dimension = d;
            }
        if (dimension == SIZE_MAX) {
            require(n.shape[out] == 1, "constant output coordinate has nonunit extent");
            continue;
        }
        require(
            result.outputAxes[dimension] == SIZE_MAX && n.shape[out] == (*extents)[dimension],
            "duplicate or incompatible reduction projection");
        result.outputAxes[dimension] = out;
    }
    for (size_t d = 0; d < extents->size(); ++d)
        require(
            result.reduced[d] || result.outputAxes[d] != SIZE_MAX || (*extents)[d] == 1,
            "parallel dimension is missing from reduction projection");
    require(inst.law < decl->laws.size(), "missing reduction law");
    const auto &law = decl->laws[inst.law];
    const auto f32 = core::type::TypeCode::Float32, f64 = core::type::TypeCode::Float64;
    const bool maximum = n.op == "max_axis", mean = n.op == "mean_axis" || n.op == "mean_all";
    const bool all = n.op == "sum_all" || n.op == "mean_all";
    // A whole-tensor reduction's result is the elements' own type (design principles 7.3), which
    // is the node's annotation; an axis reduction keeps the f32 result it always had.
    const auto wholeResult = n.dtype == DType::F32 ? f32 : f64;
    require(
        law.input == f32 && law.result == (all ? wholeResult : f32) &&
            law.init == sem::LawInit::Constant &&
            law.order == sem::LawOrder::Ascending &&
            law.kind == (maximum ? sem::LawKind::Max : sem::LawKind::Add) &&
            law.accumulator == (maximum || n.op == "sum_to" ? f32 : f64) &&
            law.initBits == (maximum ? 0xff800000ULL : 0) &&
            law.finalize == (mean ? sem::LawFinalize::DivideByCount : sem::LawFinalize::None),
        "unsupported numerical law");
    require(result.count || law.empty != sem::LawEmpty::Fault, "empty reduction domain");
    return result;
}
} // namespace
ReductionAccess
reductionAccess(const Program &p, const Node &n, const IndexExpr &row, const IndexExpr &r) {
    const auto &shape   = p.nodes.at(n.inputs.at(0)).shape;
    const auto geometry = domain(p, n);
    if (!geometry.count || !elements(n.shape)) {
        const std::vector<IndexExpr> point(geometry.extents.size(), IndexExpr(0));
        // Validate the access form without building unreachable linear coordinates.
        return {geometry.count, affineAddress(*n.indexing, n.indexing->norm[0], shape, point)};
    }
    std::vector<size_t> outputStrides(n.shape.size());
    size_t stride = 1;
    for (size_t out = n.shape.size(); out-- > 0;) {
        outputStrides[out] = stride;
        stride *= std::max<int64_t>(n.shape[out], 1);
    }
    std::vector<IndexExpr> point(geometry.extents.size(), IndexExpr(0));
    size_t reductionStride = 1;
    for (size_t d = geometry.extents.size(); d-- > 0;) {
        const auto extent = IndexExpr(std::max<int64_t>(geometry.extents[d], 1));
        if (geometry.reduced[d]) {
            point[d] = (r / IndexExpr(reductionStride)) % extent;
            reductionStride *= std::max<int64_t>(geometry.extents[d], 1);
        } else if (geometry.outputAxes[d] != SIZE_MAX)
            point[d] = (row / IndexExpr(outputStrides[geometry.outputAxes[d]])) % extent;
    }
    return {geometry.count, affineAddress(*n.indexing, n.indexing->norm[0], shape, point)};
}
bool canonicalAxisReduction(const Program &p, const Node &n) {
    try {
        const auto geometry = domain(p, n);
        // Empty outputs need no specialized launch. Avoid multiplying artificial
        // unit extents through zero axes: their physical strides may be enormous.
        if (!elements(n.shape))
            return false;
        const auto &shape = p.nodes.at(n.inputs.at(0)).shape;
        // Existing row/axis kernels use the operand's physical row-major axes.
        // This eligibility proof never supplies the generic reduction's semantics.
        if (geometry.extents != shape || n.attributes.size() != 1 || n.attributes[0] < 0 ||
            size_t(n.attributes[0]) >= shape.size())
            return false;
        const auto axis = size_t(n.attributes[0]);
        size_t output   = 0;
        const bool keep = n.shape.size() == shape.size();
        if (!keep && n.shape.size() + 1 != shape.size())
            return false;
        for (size_t d = 0; d < shape.size(); ++d) {
            if (geometry.reduced[d] != (d == axis))
                return false;
            if (d != axis) {
                if (geometry.outputAxes[d] != output++)
                    return false;
            } else if (keep && n.shape[output++] != 1)
                return false;
        }
        (void)reductionAccess(p, n, IndexExpr(0), IndexExpr(0));
        const auto view = sem::rowMajorView(*n.indexing, n.indexing->norm[0], shape);
        if (!view || view->offset != 0)
            return false;
        int64_t stride = 1;
        for (size_t d = shape.size(); d-- > 0;) {
            if (shape[d] > 1 && view->strides[d] != stride)
                return false;
            stride *= std::max<int64_t>(shape[d], 1);
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}
} // namespace camel::tiles::kernel
