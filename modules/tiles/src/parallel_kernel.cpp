/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Block-uniform shared-memory tree lowering for canonical maximum reductions.
#include "tiles/internal/kernel_ir.h"
#include "tiles/program.h"
#include <algorithm>

namespace camel::tiles::kernel {
KernelBody parallelMaxBody(const Program &p, Launch &launch) {
    const auto &n    = p.nodes.at(launch.node);
    const auto input = n.inputs.at(0);
    const auto width = static_cast<size_t>(p.nodes[input].shape.back());
    const auto count = elements(n.shape);
    launch.blockX    = 256;
    launch.gridX     = static_cast<unsigned>(std::clamp<size_t>(count, 1, 65535));
    KernelBody kernel{
        {{MemorySpace::Global, DType::F32, input, elements(p.nodes[input].shape), false},
         {MemorySpace::Global, DType::F32, launch.node, count, true},
         {MemorySpace::Shared, DType::F32, 0, 256, true}},
        {}};
    const auto lane = IndexExpr::variable("thread_x"), row = IndexExpr::variable("k_row");
    const auto r                = IndexExpr::variable("k_r");
    const auto negativeInfinity = ScalarValue{DType::F32, 0xff800000U};
    auto symbol = [](const char *name) { return ScalarExpr::symbol(name, DType::F32); };
    Block rows  = {
        ScalarBinding{"k_best", ScalarExpr::literal(negativeInfinity)},
        Loop{
            "k_r",
            lane,
            IndexExpr(width),
            256,
            {Load{"k_input", {0, row * IndexExpr(width) + r, Predicate(true)}, negativeInfinity},
             Assignment{
                 "k_best",
                 ScalarExpr::apply(ScalarOp::Maximum, {symbol("k_best"), symbol("k_input")})}}},
        Store{{2, lane, Predicate(true)}, symbol("k_best")},
        Barrier{}};
    // Each barrier belongs to the uniform row loop, outside the lane predicate.
    // Canonical NaN and signed-zero rules make these merges bitwise order-independent.
    for (size_t stride = 128; stride; stride /= 2) {
        rows.push_back(
            Conditional{
                Predicate::less(lane, IndexExpr(stride)),
                {Load{"k_left", {2, lane, Predicate(true)}, negativeInfinity},
                 Load{"k_right", {2, lane + IndexExpr(stride), Predicate(true)}, negativeInfinity},
                 Store{
                     {2, lane, Predicate(true)},
                     ScalarExpr::apply(
                         ScalarOp::Maximum,
                         {symbol("k_left"), symbol("k_right")})}}});
        rows.push_back(Barrier{});
    }
    rows.push_back(
        Conditional{
            Predicate::equal(lane, IndexExpr(0)),
            {Load{"k_result", {2, IndexExpr(0), Predicate(true)}, negativeInfinity},
             Store{{1, row, Predicate(true)}, symbol("k_result")}}});
    rows.push_back(Barrier{});
    kernel.statements.push_back(
        Loop{
            "k_row",
            IndexExpr::variable("block_x"),
            IndexExpr(count),
            launch.gridX,
            std::move(rows)});
    validateBody(kernel);
    return kernel;
}
} // namespace camel::tiles::kernel
