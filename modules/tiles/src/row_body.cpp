/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Structured shared-row lowering with a data-dependent exactness proof and
// canonical ascending fallback. Physical choices do not relax numeric contracts.
#include "tiles/internal/row_kernel.h"
#include <algorithm>
#include <utility>

namespace camel::tiles::kernel {
namespace {
ScalarExpr i32(int32_t x) { return ScalarExpr::literal(ScalarValue::i32(x)); }
ScalarExpr u32(uint32_t x) { return ScalarExpr::literal(ScalarValue::u32(x)); }
ScalarExpr f64(double x) { return ScalarExpr::literal(ScalarValue::f64(x)); }
ScalarExpr op(ScalarOp kind, ScalarExpr a, ScalarExpr b) {
    return ScalarExpr::apply(kind, {std::move(a), std::move(b)});
}
ScalarPredicate cmp(ScalarCompare kind, ScalarExpr a, ScalarExpr b) {
    return {kind, std::move(a), std::move(b)};
}
struct Lowerer {
    const Program &p;
    const RowKernel &plan;
    KernelBody body;
    size_t values, scratch, sums, lower, upper, sequence = 0;
    const IndexExpr lane = IndexExpr::variable("thread_x"), row = IndexExpr::variable("k_row"),
                    col = IndexExpr::variable("k_col");
    std::string fresh() { return "k_v" + std::to_string(sequence++); }
    size_t shared(ScalarType type, size_t count) {
        const auto id = body.buffers.size();
        body.buffers.push_back({MemorySpace::Shared, type, id, count, true});
        return id;
    }
    Lowerer(const Program &program, const RowKernel &region) : p(program), plan(region) {
        for (size_t id = 0; id < p.nodes.size(); ++id)
            body.buffers.push_back(
                {MemorySpace::Global,
                 p.nodes[id].dtype,
                 id,
                 elements(p.nodes[id].shape),
                 id == p.outputs[0]});
        values  = shared(DType::F32, plan.sharedElements);
        scratch = shared(DType::F32, 32);
        sums    = shared(DType::F64, 32);
        lower   = shared(ScalarType::I32, 32);
        upper   = shared(ScalarType::I32, 32);
    }
    ScalarExpr bind(Block &block, ScalarExpr value) {
        const auto name = fresh();
        block.push_back(ScalarBinding{name, value});
        return ScalarExpr::symbol(name, value.type());
    }
    ScalarExpr load(Block &block, size_t buffer, IndexExpr at) {
        const auto name = fresh();
        const auto type = body.buffers[buffer].type;
        block.push_back(Load{name, {buffer, std::move(at), Predicate(true)}, {type, 0}});
        return ScalarExpr::symbol(name, type);
    }
    void store(Block &block, size_t buffer, IndexExpr at, ScalarExpr value) {
        block.push_back(Store{{buffer, std::move(at), Predicate(true)}, std::move(value)});
    }
    ScalarExpr read(Block &block, size_t id, IndexExpr column) {
        const auto at = plan.widths[id] == 1 ? IndexExpr(0) : column;
        if (plan.offsets[id] != SIZE_MAX)
            return load(block, values, IndexExpr(plan.offsets[id]) + at);
        const auto &shape = p.nodes[id].shape;
        const bool perRow = shape.size() == 2 && shape[0] == static_cast<int64_t>(plan.rows);
        return load(block, id, perRow ? row * IndexExpr(plan.widths[id]) + at : at);
    }
    void tree(Block &block, const std::vector<std::pair<size_t, ScalarOp>> &fields) {
        for (size_t stride = 16; stride; stride /= 2) {
            Block merge;
            for (const auto &[buffer, operation] : fields) {
                const auto a = load(merge, buffer, lane),
                           b = load(merge, buffer, lane + IndexExpr(stride));
                store(merge, buffer, lane, op(operation, a, b));
            }
            block.push_back(
                Conditional{Predicate::less(lane, IndexExpr(stride)), std::move(merge)});
            block.push_back(Barrier{});
        }
    }
    void maximum(Block &block, const RowStage &stage) {
        const auto acc = fresh();
        block.push_back(ScalarBinding{acc, ScalarExpr::literal({DType::F32, 0xff800000U})});
        const auto current = ScalarExpr::symbol(acc, DType::F32);
        Block each;
        const auto x = read(each, p.nodes[stage.node].inputs[0], col);
        each.push_back(Assignment{acc, op(ScalarOp::Maximum, current, x)});
        block.push_back(Loop{"k_col", lane, IndexExpr(plan.width), 32, std::move(each)});
        store(block, scratch, lane, current);
        block.push_back(Barrier{});
        tree(block, {{scratch, ScalarOp::Maximum}});
        Block finish;
        const auto result = load(finish, scratch, IndexExpr(0));
        store(finish, values, IndexExpr(plan.offsets[stage.node]), result);
        block.push_back(Conditional{Predicate::equal(lane, IndexExpr(0)), std::move(finish)});
    }
    void sum(Block &block, const RowStage &stage) {
        const auto input = p.nodes[stage.node].inputs[0];
        const auto acc = fresh(), lo = fresh(), hi = fresh();
        block.push_back(ScalarBinding{acc, f64(0)});
        block.push_back(ScalarBinding{lo, i32(4096)});
        block.push_back(ScalarBinding{hi, i32(-4096)});
        const auto a = ScalarExpr::symbol(acc, DType::F64);
        const auto l = ScalarExpr::symbol(lo, ScalarType::I32),
                   h = ScalarExpr::symbol(hi, ScalarType::I32);
        Block each;
        const auto x    = read(each, input, col);
        const auto bits = bind(
            each,
            op(ScalarOp::BitAnd, ScalarExpr::bitcast(ScalarType::U32, x), u32(0x7fffffffU)));
        const auto exponent = bind(each, op(ScalarOp::LogicalShiftRight, bits, u32(23)));
        const auto low = fresh(), high = fresh();
        Block finite              = {ScalarBinding{low, i32(-149)}, ScalarBinding{high, i32(-126)}};
        const auto signedExponent = ScalarExpr::bitcast(ScalarType::I32, exponent);
        finite.push_back(
            Conditional{
                cmp(ScalarCompare::NotEqual, exponent, u32(0)),
                {Assignment{low, op(ScalarOp::Subtract, signedExponent, i32(150))},
                 Assignment{high, op(ScalarOp::Subtract, signedExponent, i32(126))}}});
        finite.push_back(
            Assignment{lo, op(ScalarOp::Minimum, l, ScalarExpr::symbol(low, ScalarType::I32))});
        finite.push_back(
            Assignment{hi, op(ScalarOp::Maximum, h, ScalarExpr::symbol(high, ScalarType::I32))});
        each.push_back(
            Conditional{
                cmp(ScalarCompare::Equal, exponent, u32(255)),
                {Assignment{hi, i32(4096)}},
                {Conditional{cmp(ScalarCompare::NotEqual, bits, u32(0)), std::move(finite)}}});
        each.push_back(Assignment{acc, op(ScalarOp::Add, a, ScalarExpr::cast(DType::F64, x))});
        block.push_back(Loop{"k_col", lane, IndexExpr(plan.width), 32, std::move(each)});
        store(block, sums, lane, a);
        store(block, lower, lane, l);
        store(block, upper, lane, h);
        block.push_back(Barrier{});
        tree(
            block,
            {{sums, ScalarOp::Add}, {lower, ScalarOp::Minimum}, {upper, ScalarOp::Maximum}});

        // A finite f32 is a multiple of 2^lo. If every possible partial sum
        // occupies at most 53 lattice bits, all f64 additions are exact.
        // Otherwise execute the canonical ascending chain, without reassociation.
        size_t countBits = 0;
        while ((size_t(1) << countBits) < plan.width)
            ++countBits;
        Block finish;
        const auto minExponent = load(finish, lower, IndexExpr(0));
        const auto maxExponent = load(finish, upper, IndexExpr(0));
        const auto exact = fresh(), result = fresh();
        finish.push_back(ScalarBinding{exact, i32(0)});
        finish.push_back(ScalarBinding{result, f64(0)});
        const auto span =
            op(ScalarOp::Add,
               op(ScalarOp::Subtract, maxExponent, minExponent),
               i32(static_cast<int32_t>(countBits)));
        finish.push_back(
            Conditional{
                cmp(ScalarCompare::Equal, maxExponent, i32(-4096)),
                {Assignment{exact, i32(1)}},
                {Conditional{
                    cmp(ScalarCompare::Less, maxExponent, i32(1024)),
                    {Conditional{
                        cmp(ScalarCompare::LessEqual, span, i32(53)),
                        {Assignment{exact, i32(1)}}}}}}});
        Block fast;
        const auto subtotal = load(fast, sums, IndexExpr(0));
        fast.push_back(Assignment{result, subtotal});
        Block ordered;
        const auto canonical = read(ordered, input, col);
        const auto total     = ScalarExpr::symbol(result, DType::F64);
        ordered.push_back(
            Assignment{result, op(ScalarOp::Add, total, ScalarExpr::cast(DType::F64, canonical))});
        finish.push_back(
            Conditional{
                cmp(ScalarCompare::NotEqual, ScalarExpr::symbol(exact, ScalarType::I32), i32(0)),
                std::move(fast),
                {Loop{"k_col", IndexExpr(0), IndexExpr(plan.width), 1, std::move(ordered)}}});
        auto output = total;
        if (stage.reduction->finalize == core::sem::LawFinalize::DivideByCount)
            output = op(ScalarOp::Divide, total, f64(static_cast<double>(plan.width)));
        store(
            finish,
            values,
            IndexExpr(plan.offsets[stage.node]),
            ScalarExpr::cast(DType::F32, output));
        block.push_back(Conditional{Predicate::equal(lane, IndexExpr(0)), std::move(finish)});
    }
    KernelBody run(Launch &launch) {
        launch.blockX = 32;
        launch.gridX  = static_cast<unsigned>(std::min<size_t>(plan.rows, 65535));
        Block rows;
        for (const auto &stage : plan.stages) {
            if (stage.reduction) {
                if (stage.reduction->kind == core::sem::LawKind::Max)
                    maximum(rows, stage);
                else
                    sum(rows, stage);
            } else {
                Block each;
                const auto &node = p.nodes[stage.node];
                auto value = stage.expression->substitute("a", read(each, node.inputs[0], col));
                if (node.inputs.size() == 2)
                    value = value.substitute("b", read(each, node.inputs[1], col));
                store(each, values, IndexExpr(plan.offsets[stage.node]) + col, value);
                rows.push_back(
                    Loop{"k_col", lane, IndexExpr(plan.widths[stage.node]), 32, std::move(each)});
            }
            rows.push_back(Barrier{});
        }
        Block output;
        const auto root  = p.outputs[0];
        const auto value = read(output, root, col);
        store(output, root, row * IndexExpr(plan.width) + col, value);
        rows.push_back(Loop{"k_col", lane, IndexExpr(plan.width), 32, std::move(output)});
        rows.push_back(Barrier{});
        body.statements.push_back(
            Loop{
                "k_row",
                IndexExpr::variable("block_x"),
                IndexExpr(plan.rows),
                launch.gridX,
                std::move(rows)});
        validateBody(body);
        return std::move(body);
    }
};
} // namespace
KernelBody rowBody(const Program &p, const RowKernel &plan, Launch &launch) {
    return Lowerer(p, plan).run(launch);
}
} // namespace camel::tiles::kernel
