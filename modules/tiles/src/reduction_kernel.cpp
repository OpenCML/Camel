/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Bind axis-reduction traversal to the core domain and checked operand map.
#include "tiles/internal/kernel_ir.h"
#include "tiles/program.h"
#include <algorithm>
#include <stdexcept>

namespace camel::tiles::kernel {
namespace {
namespace sem = core::sem;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(std::string("axis reduction core contract: ") + message);
}
void contract(const Program &p, const Node &n) {
    require(n.indexing && n.semantic < p.contracts.size(), "missing index instance");
    const auto &inst = *n.indexing;
    const auto *decl = p.contracts[n.semantic].declaration;
    require(decl && !sem::validate(inst, decl), "invalid index instance");
    const auto &shape = p.nodes.at(n.inputs.at(0)).shape;
    const size_t axis = n.attributes.at(0);
    require(
        axis < shape.size() && inst.extents() == std::optional<Shape>(shape) && inst.with.empty() &&
            !inst.norm.empty() && inst.domain.guard.empty(),
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
    const bool keep = n.shape.size() == shape.size();
    require(keep || n.shape.size() + 1 == shape.size(), "unsupported output rank");
    for (size_t d = 0, out = 0; d < shape.size(); ++d) {
        require(
            inst.domain.dims[d].role ==
                (d == axis ? sem::DimRole::Reduction : sem::DimRole::Parallel),
            "unsupported dimension roles");
        if (d == axis && !keep)
            continue;
        const auto &coord = inst.exprs[inst.output.coords[out++]];
        require(
            d == axis ? coord.op == sem::ExprOp::Const && coord.value == 0
                      : coord.op == sem::ExprOp::Dim && coord.value == static_cast<int64_t>(d),
            "output is not the axis projection");
    }
    require(inst.law < decl->laws.size(), "missing reduction law");
    const auto &law = decl->laws[inst.law];
    const auto f32 = core::type::TypeCode::Float32, f64 = core::type::TypeCode::Float64;
    const bool maximum = n.op == "max_axis", mean = n.op == "mean_axis";
    require(
        (maximum || mean || n.op == "sum_axis") && law.input == f32 && law.result == f32 &&
            law.init == sem::LawInit::Constant && law.order == sem::LawOrder::Ascending &&
            law.kind == (maximum ? sem::LawKind::Max : sem::LawKind::Add) &&
            law.accumulator == (maximum ? f32 : f64) &&
            law.initBits == (maximum ? 0xff800000ULL : 0) &&
            law.empty == (maximum ? sem::LawEmpty::Fault : sem::LawEmpty::Initial) &&
            law.finalize == (mean ? sem::LawFinalize::DivideByCount : sem::LawFinalize::None),
        "unsupported numerical law");
}
} // namespace
IndexExpr
axisReductionAddress(const Program &p, const Node &n, const IndexExpr &row, const IndexExpr &r) {
    const auto &shape = p.nodes.at(n.inputs.at(0)).shape;
    const size_t axis = n.attributes.at(0);
    size_t inner      = 1;
    for (size_t d = axis + 1; d < shape.size(); ++d)
        inner *= shape[d];
    const auto safeInner = IndexExpr(std::max(inner, size_t(1)));
    const auto direct =
        (row / safeInner) * IndexExpr(shape[axis] * inner) + row % safeInner + r * IndexExpr(inner);
    if (!n.indexing)
        return direct; // Explicit physical primitives, e.g. AD expansions.
    contract(p, n);
    std::vector<IndexExpr> point(shape.size(), IndexExpr(0));
    size_t stride = 1;
    for (size_t d = shape.size(); d-- > 0;) {
        if (d == axis) {
            point[d] = r;
            continue;
        }
        point[d] = (row / IndexExpr(stride)) % IndexExpr(std::max<int64_t>(shape[d], 1));
        stride *= std::max<int64_t>(shape[d], 1);
    }
    const auto mapped = affineAddress(*n.indexing, n.indexing->norm[0], shape, point);
    const auto view   = sem::rowMajorView(*n.indexing, n.indexing->norm[0], shape);
    bool identity     = view && view->offset == 0;
    stride            = 1;
    for (size_t d = shape.size(); identity && d-- > 0;) {
        identity = shape[d] <= 1 || view->strides[d] == static_cast<int64_t>(stride);
        stride *= std::max<int64_t>(shape[d], 1);
    }
    return identity ? direct : mapped;
}
bool canonicalAxisReduction(const Program &p, const Node &n) {
    if (!n.indexing)
        return true;
    try {
        (void)axisReductionAddress(p, n, IndexExpr(0), IndexExpr(0));
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
