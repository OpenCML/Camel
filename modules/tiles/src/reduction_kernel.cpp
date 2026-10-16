/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Bind reduction traversal to the core domain and checked operand map.
#include "tiles/internal/kernel_ir.h"
#include "tiles/program.h"
#include <algorithm>
#include <stdexcept>

namespace camel::tiles::kernel {
namespace {
namespace sem = core::sem;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(std::string("reduction core contract: ") + message);
}
struct Domain {
    std::vector<bool> reduced;
    std::vector<int64_t> outputDims; // -1 denotes a kept reduction coordinate (zero).
    size_t count = 1;
};
Domain domain(const Program &p, const Node &n) {
    const auto &shape = p.nodes.at(n.inputs.at(0)).shape;
    Domain result{std::vector<bool>(shape.size(), true)};
    if (n.op.ends_with("_axis")) {
        const size_t axis = n.attributes.at(0);
        require(axis < shape.size(), "invalid axis");
        const bool keep = n.shape.size() == shape.size();
        require(keep || n.shape.size() + 1 == shape.size(), "unsupported output rank");
        for (size_t d = 0; d < shape.size(); ++d) {
            result.reduced[d] = d == axis;
            if (d != axis || keep)
                result.outputDims.push_back(d == axis ? -1 : static_cast<int64_t>(d));
        }
    } else if (n.op == "sum_to") {
        require(n.shape.size() <= shape.size(), "invalid target rank");
        const auto shift = shape.size() - n.shape.size();
        for (size_t d = shift; d < shape.size(); ++d) {
            const auto extent = n.shape[d - shift];
            require(extent == shape[d] || extent == 1, "invalid target extent");
            result.reduced[d] = extent != shape[d];
            result.outputDims.push_back(result.reduced[d] ? -1 : static_cast<int64_t>(d));
        }
    } else
        require(
            (n.op == "sum_all" || n.op == "mean_all") && n.shape.empty(),
            "unsupported reduction");
    Shape reductions;
    for (size_t d = 0; d < shape.size(); ++d)
        if (result.reduced[d])
            reductions.push_back(shape[d]);
    result.count = elements(reductions);
    return result;
}
void contract(const Program &p, const Node &n, const Domain &domain) {
    require(n.indexing && n.semantic < p.contracts.size(), "missing index instance");
    const auto &inst = *n.indexing;
    const auto *decl = p.contracts[n.semantic].declaration;
    require(decl && !sem::validate(inst, decl), "invalid index instance");
    const auto &shape = p.nodes.at(n.inputs.at(0)).shape;
    require(
        inst.extents() == std::optional<Shape>(shape) && inst.with.empty() && !inst.norm.empty() &&
            inst.domain.guard.empty(),
        "unsupported iteration domain");
    for (size_t i = 1; i < inst.norm.size(); ++i)
        require(inst.norm[i].kind == sem::AccessKind::None, "unexpected data operand");
    for (const auto &c : inst.domain.constraints)
        require(
            !inst.exprs.mentionsDim(c.lhs) && !inst.exprs.mentionsDim(c.rhs),
            "dimension-dependent constraint");
    require(
        sem::holds(inst, inst.domain.constraints, std::vector<int64_t>(shape.size())),
        "unsatisfied domain constraints");
    require(
        inst.output.mode == sem::WriteMode::Combine && !inst.output.indirect &&
            inst.output.valid.empty() && inst.output.coords.size() == n.shape.size(),
        "unsupported output map");
    for (size_t d = 0; d < shape.size(); ++d)
        require(
            inst.domain.dims[d].role ==
                (domain.reduced[d] ? sem::DimRole::Reduction : sem::DimRole::Parallel),
            "unsupported dimension roles");
    for (size_t out = 0; out < domain.outputDims.size(); ++out) {
        const auto d      = domain.outputDims[out];
        const auto &coord = inst.exprs[inst.output.coords[out]];
        require(
            d < 0 ? coord.op == sem::ExprOp::Const && coord.value == 0
                  : coord.op == sem::ExprOp::Dim && coord.value == d,
            "output is not the reduction projection");
    }
    require(inst.law < decl->laws.size(), "missing reduction law");
    const auto &law = decl->laws[inst.law];
    const auto f32 = core::type::TypeCode::Float32, f64 = core::type::TypeCode::Float64;
    const bool maximum = n.op == "max_axis", mean = n.op == "mean_axis" || n.op == "mean_all";
    const bool all = n.op == "sum_all" || n.op == "mean_all";
    require(
        law.input == f32 && law.result == (all ? f64 : f32) && law.init == sem::LawInit::Constant &&
            law.order == sem::LawOrder::Ascending &&
            law.kind == (maximum ? sem::LawKind::Max : sem::LawKind::Add) &&
            law.accumulator == (maximum || n.op == "sum_to" ? f32 : f64) &&
            law.initBits == (maximum ? 0xff800000ULL : 0) &&
            law.empty ==
                (maximum || n.op == "mean_all" ? sem::LawEmpty::Fault : sem::LawEmpty::Initial) &&
            law.finalize == (mean ? sem::LawFinalize::DivideByCount : sem::LawFinalize::None),
        "unsupported numerical law");
}
} // namespace
ReductionAccess
reductionAccess(const Program &p, const Node &n, const IndexExpr &row, const IndexExpr &r) {
    const auto &shape   = p.nodes.at(n.inputs.at(0)).shape;
    const auto geometry = domain(p, n);
    if (n.indexing)
        contract(p, n, geometry);
    std::vector<IndexExpr> point(shape.size(), IndexExpr(0));
    size_t redStride = 1, outStride = 1, inStride = 1;
    IndexExpr direct(0);
    for (size_t d = shape.size(); d-- > 0;) {
        auto &stride       = geometry.reduced[d] ? redStride : outStride;
        const auto &linear = geometry.reduced[d] ? r : row;
        point[d] = (linear / IndexExpr(stride)) % IndexExpr(std::max<int64_t>(shape[d], 1));
        stride *= std::max<int64_t>(shape[d], 1);
        direct = direct + point[d] * IndexExpr(inStride);
        inStride *= std::max<int64_t>(shape[d], 1);
    }
    // Preserve compact formulas for the common full and single-axis domains.
    if (n.op == "sum_all" || n.op == "mean_all")
        direct = r;
    else if (n.op.ends_with("_axis")) {
        const auto axis = static_cast<size_t>(n.attributes.at(0));
        size_t inner    = 1;
        for (size_t d = axis + 1; d < shape.size(); ++d)
            inner *= shape[d];
        const auto safeInner = IndexExpr(std::max(inner, size_t(1)));
        direct = (row / safeInner) * IndexExpr(shape[axis] * inner) + row % safeInner +
                 r * IndexExpr(inner);
    }
    if (!n.indexing)
        return {geometry.count, direct}; // Explicit physical primitives.
    const auto mapped = affineAddress(*n.indexing, n.indexing->norm[0], shape, point);
    const auto view   = sem::rowMajorView(*n.indexing, n.indexing->norm[0], shape);
    bool identity     = view && view->offset == 0;
    size_t stride     = 1;
    for (size_t d = shape.size(); identity && d-- > 0;) {
        identity = shape[d] <= 1 || view->strides[d] == static_cast<int64_t>(stride);
        stride *= std::max<int64_t>(shape[d], 1);
    }
    return {geometry.count, identity ? direct : mapped};
}
bool canonicalAxisReduction(const Program &p, const Node &n) {
    if (!n.indexing)
        return true;
    try {
        (void)reductionAccess(p, n, IndexExpr(0), IndexExpr(0));
        const auto &shape = p.nodes.at(n.inputs.at(0)).shape;
        const auto view   = sem::rowMajorView(*n.indexing, n.indexing->norm[0], shape);
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
