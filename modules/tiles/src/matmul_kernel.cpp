// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Tiled FMA-chain matrix lowering into structured physical statements.

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
void require(bool ok, const char *reason) {
    if (!ok)
        throw std::runtime_error(std::string("matmul core contract: ") + reason);
}
void matrixContract(const Program &p, const Node &n, const Shape &domain) {
    require(n.indexing.has_value() && n.semantic < p.contracts.size(), "missing index instance");
    const auto &inst = *n.indexing;
    const auto *decl = p.contracts[n.semantic].declaration;
    require(decl && inst.law < decl->laws.size(), "missing reduction law");
    const auto problem = sem::validate(inst, decl);
    require(!problem, "invalid index instance");
    require(
        inst.extents() == std::optional<Shape>(domain) && inst.norm.size() == 2 &&
            inst.with.empty() && inst.domain.guard.empty(),
        "unsupported iteration domain");
    require(domain.size() >= 3, "matrix domain rank");
    for (size_t d = 0; d < domain.size(); ++d)
        require(
            inst.domain.dims[d].role ==
                (d + 1 == domain.size() ? sem::DimRole::Reduction : sem::DimRole::Parallel),
            "unsupported dimension roles");
    require(
        sem::holds(inst, inst.domain.constraints, std::vector<int64_t>(domain.size())),
        "unsatisfied domain constraints");
    require(
        inst.output.mode == sem::WriteMode::Combine && !inst.output.indirect &&
            inst.output.valid.empty() && inst.output.coords.size() + 1 == domain.size(),
        "unsupported output map");
    for (size_t d = 0; d < inst.output.coords.size(); ++d) {
        const auto &e = inst.exprs[inst.output.coords[d]];
        require(
            e.op == sem::ExprOp::Dim && e.value == static_cast<int64_t>(d),
            "output is not a direct matrix domain");
    }
    const auto &law = decl->laws[inst.law];
    const auto f32  = core::type::TypeCode::Float32;
    require(
        law.kind == sem::LawKind::FmaChain && law.input == f32 && law.accumulator == f32 &&
            law.result == f32 && law.init == sem::LawInit::Constant && law.initBits == 0 &&
            law.empty == sem::LawEmpty::Initial && law.order == sem::LawOrder::Ascending &&
            law.finalize == sem::LawFinalize::None,
        "requires ascending f32 FMA chain from positive zero");
    // Cooperative A loads are shared across columns; B loads across rows.
    const auto lhs = sem::rowMajorView(inst, inst.norm[0], p.nodes[n.inputs[0]].shape);
    const auto rhs = sem::rowMajorView(inst, inst.norm[1], p.nodes[n.inputs[1]].shape);
    require(
        lhs && rhs && lhs->strides[domain.size() - 2] == 0 && rhs->strides[domain.size() - 3] == 0,
        "cross-output dependence prevents tiled sharing");
}
} // namespace
void validateMatmulContract(const Program &p, const Node &n) {
    const auto &a = p.nodes.at(n.inputs.at(0)).shape;
    const auto &b = p.nodes.at(n.inputs.at(1)).shape;
    require(a.size() >= 2 && b.size() >= 2 && n.shape.size() >= 2, "operand rank");
    auto domain = n.shape;
    domain.push_back(a.back());
    matrixContract(p, n, domain);
    const std::vector<IndexExpr> point(domain.size(), IndexExpr(0));
    (void)affineAddress(*n.indexing, n.indexing->norm[0], a, point);
    (void)affineAddress(*n.indexing, n.indexing->norm[1], b, point);
}
KernelBody matmulBody(const Program &p, Launch &launch) {
    const auto id  = launch.node;
    const auto &n  = p.nodes.at(id);
    const auto &a  = p.nodes.at(n.inputs.at(0)).shape;
    const auto &b  = p.nodes.at(n.inputs.at(1)).shape;
    const size_t m = a.at(a.size() - 2), k = a.back(), cols = b.back();
    validateMatmulContract(p, n);
    const size_t gx = (cols + 15) / 16, gy = (m + 15) / 16;
    const auto batches = elements(Shape(n.shape.begin(), n.shape.end() - 2));
    if (gx > 2147483647 || gy > 65535 || (gy && batches > 65535 / gy))
        throw std::runtime_error("matrix launch exceeds CUDA grid limits");
    launch.blockX = launch.blockY = 16;
    launch.gridX                  = static_cast<unsigned>(std::max(gx, size_t(1)));
    launch.gridY                  = static_cast<unsigned>(std::max(gy * batches, size_t(1)));
    KernelBody result;
    result.buffers = {
        {MemorySpace::Global, DType::F32, n.inputs[0], elements(a), false},
        {MemorySpace::Global, DType::F32, n.inputs[1], elements(b), false},
        {MemorySpace::Global, DType::F32, id, elements(n.shape), true},
        {MemorySpace::Shared, DType::F32, 0, 256, true},
        {MemorySpace::Shared, DType::F32, 1, 256, true}};
    // No operand access is valid for an empty output batch. Emit a no-op body.
    if (!elements(n.shape)) {
        validateBody(result);
        return result;
    }
    const auto tx = IndexExpr::variable("thread_x"), ty = IndexExpr::variable("thread_y");
    const auto row = IndexExpr::variable("k_row"), col = IndexExpr::variable("k_col");
    const auto base = IndexExpr::variable("k_base"), t = IndexExpr::variable("k_t");
    const auto ak = base + tx, bk = base + ty;
    // Flatten batch and row tiles into grid Y. Logical batch coordinates feed
    // the existing core maps, including singleton and unequal-rank broadcasts.
    std::vector<IndexExpr> point;
    auto batch = IndexExpr::variable("block_y") / IndexExpr(gy);
    for (size_t d = 0; d + 2 < n.shape.size(); ++d) {
        const auto stride = elements(Shape(n.shape.begin() + d + 1, n.shape.end() - 2));
        point.push_back((batch / IndexExpr(stride)) % IndexExpr(n.shape[d]));
    }
    const auto matrixPoint = [&](IndexExpr r, IndexExpr c, IndexExpr reduction) {
        auto coordinates = point;
        coordinates.insert(coordinates.end(), {r, c, reduction});
        return coordinates;
    };
    const auto zero = ScalarValue::f32(0);
    auto symbol     = [](const char *name) { return ScalarExpr::symbol(name, DType::F32); };
    // The canonical FMA chain keeps one accumulator across tiles. Padding is
    // allowed for cooperative loads, never for contributions to the chain.
    Block step = {
        Load{"k_left", {3, ty * IndexExpr(16) + t, Predicate(true)}, zero},
        Load{"k_right", {4, t * IndexExpr(16) + tx, Predicate(true)}, zero},
        Assignment{
            "k_acc",
            ScalarExpr::apply(
                ScalarOp::Fma,
                {symbol("k_left"), symbol("k_right"), symbol("k_acc")})}};
    Block tile = {
        Load{
            "k_a",
            {0,
             affineAddress(*n.indexing, n.indexing->norm[0], a, matrixPoint(row, IndexExpr(0), ak)),
             Predicate::less(row, IndexExpr(m)) && Predicate::less(ak, IndexExpr(k))},
            zero},
        Load{
            "k_b",
            {1,
             affineAddress(*n.indexing, n.indexing->norm[1], b, matrixPoint(IndexExpr(0), col, bk)),
             Predicate::less(bk, IndexExpr(k)) && Predicate::less(col, IndexExpr(cols))},
            zero},
        Store{{3, ty * IndexExpr(16) + tx, Predicate(true)}, symbol("k_a")},
        Store{{4, ty * IndexExpr(16) + tx, Predicate(true)}, symbol("k_b")},
        Barrier{},
        Loop{
            "k_t",
            IndexExpr(0),
            IndexExpr(16),
            1,
            {Conditional{Predicate::less(base + t, IndexExpr(k)), std::move(step)}},
            true},
        Barrier{}};
    result.statements = {
        IndexBinding{
            "k_row",
            (IndexExpr::variable("block_y") % IndexExpr(gy)) * IndexExpr(16) + ty},
        IndexBinding{"k_col", IndexExpr::variable("block_x") * IndexExpr(16) + tx},
        ScalarBinding{"k_acc", ScalarExpr::literal(zero)},
        Loop{"k_base", IndexExpr(0), IndexExpr(k), 16, std::move(tile)},
        Store{
            {2,
             affineAddress(
                 *n.indexing,
                 sem::Access{.kind = sem::AccessKind::Affine, .coords = n.indexing->output.coords},
                 n.shape,
                 matrixPoint(row, col, IndexExpr(0))),
             Predicate::less(row, IndexExpr(m)) && Predicate::less(col, IndexExpr(cols))},
            symbol("k_acc")}};
    validateBody(result);
    return result;
}
} // namespace camel::tiles::kernel
