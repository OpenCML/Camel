/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Tiled FMA-chain matrix lowering into structured physical statements.
#include "tiles/internal/kernel_ir.h"
#include "tiles/program.h"
#include <algorithm>
#include <stdexcept>

namespace camel::tiles::kernel {
KernelBody matmulBody(const Program &p, Launch &launch) {
    const auto id  = launch.node;
    const auto &n  = p.nodes.at(id);
    const auto &a  = p.nodes.at(n.inputs.at(0)).shape;
    const auto &b  = p.nodes.at(n.inputs.at(1)).shape;
    const size_t m = a.at(0), k = a.at(1), cols = b.at(1);
    const size_t gx = (cols + 15) / 16, gy = (m + 15) / 16;
    if (gx > 2147483647 || gy > 65535)
        throw std::runtime_error("matrix launch exceeds CUDA grid limits");
    launch.blockX = launch.blockY = 16;
    launch.gridX                  = static_cast<unsigned>(std::max(gx, size_t(1)));
    launch.gridY                  = static_cast<unsigned>(std::max(gy, size_t(1)));
    KernelBody result;
    result.buffers = {
        {MemorySpace::Global, DType::F32, n.inputs[0], elements(a), false},
        {MemorySpace::Global, DType::F32, n.inputs[1], elements(b), false},
        {MemorySpace::Global, DType::F32, id, elements(n.shape), true},
        {MemorySpace::Shared, DType::F32, 0, 256, true},
        {MemorySpace::Shared, DType::F32, 1, 256, true}};
    const auto tx = IndexExpr::variable("thread_x"), ty = IndexExpr::variable("thread_y");
    const auto row = IndexExpr::variable("k_row"), col = IndexExpr::variable("k_col");
    const auto base = IndexExpr::variable("k_base"), t = IndexExpr::variable("k_t");
    const auto ak = base + tx, bk = base + ty;
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
             row * IndexExpr(k) + ak,
             Predicate::less(row, IndexExpr(m)) && Predicate::less(ak, IndexExpr(k))},
            zero},
        Load{
            "k_b",
            {1,
             bk * IndexExpr(cols) + col,
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
        IndexBinding{"k_row", IndexExpr::variable("block_y") * IndexExpr(16) + ty},
        IndexBinding{"k_col", IndexExpr::variable("block_x") * IndexExpr(16) + tx},
        ScalarBinding{"k_acc", ScalarExpr::literal(zero)},
        Loop{"k_base", IndexExpr(0), IndexExpr(k), 16, std::move(tile)},
        Store{
            {2,
             row * IndexExpr(cols) + col,
             Predicate::less(row, IndexExpr(m)) && Predicate::less(col, IndexExpr(cols))},
            symbol("k_acc")}};
    validateBody(result);
    return result;
}
} // namespace camel::tiles::kernel
