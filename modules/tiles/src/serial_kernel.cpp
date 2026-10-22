// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Structured per-output loops, fused scalar DAGs and canonical serial reductions.

#include "tiles/internal/kernel_ir.h"
#include "tiles/program.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace camel::tiles::kernel {
namespace {
IndexExpr broadcast(const Shape &input, const Shape &output, const IndexExpr &at) {
    if (input == output)
        return at;
    if (input.size() > output.size())
        throw std::runtime_error("invalid broadcast rank");
    size_t inStride = 1, outStride = 1;
    IndexExpr result(0);
    for (size_t d = output.size(); d-- > 0;) {
        if (d >= output.size() - input.size()) {
            const auto j = d - (output.size() - input.size());
            if (input[j] != 1 && input[j] != output[d])
                throw std::runtime_error("invalid broadcast shape");
            if (input[j] > 1)
                result = result +
                         ((at / IndexExpr(outStride)) % IndexExpr(input[j])) * IndexExpr(inStride);
            inStride *= std::max<int64_t>(input[j], 1);
        }
        outStride *= std::max<int64_t>(output[d], 1);
    }
    return result;
}
IndexExpr mapped(const Node &n, const IndexExpr &at) {
    IndexExpr result(n.offset);
    size_t stride = 1;
    for (size_t d = n.shape.size(); elements(n.shape) && d-- > 0;) {
        result =
            result + ((at / IndexExpr(stride)) % IndexExpr(n.shape[d])) * IndexExpr(n.strides[d]);
        stride *= n.shape[d];
    }
    return result;
}
class Lowerer {
    const Program &p;
    const Launch &launch;
    size_t next = 0;

  public:
    size_t reduction = SIZE_MAX;
    Lowerer(const Program &program, const Launch &stage) : p(program), launch(stage) {}
    std::string name() { return "k_value" + std::to_string(next++); }
    ScalarExpr bind(Block &body, ScalarExpr expr) {
        auto id = name();
        body.push_back(ScalarBinding{id, expr});
        return ScalarExpr::symbol(id, expr.type());
    }
    ScalarExpr load(Block &body, size_t input, const IndexExpr &at) {
        const auto id = name();
        body.push_back(
            Load{id, {input, at, Predicate(true)}, ScalarValue{p.nodes[input].dtype, 0}});
        return ScalarExpr::symbol(id, p.nodes[input].dtype);
    }
    bool member(size_t id) const {
        return std::find(launch.members.begin(), launch.members.end(), id) != launch.members.end();
    }
    bool contract(const Node &n) const {
        if (!(n.permissions & core::sem::kContract) || n.semantic >= p.contracts.size())
            return false;
        const auto *decl = p.contracts[n.semantic].declaration;
        return decl && decl->numeric.cls == core::sem::NumericClass::CorrectlyRounded &&
               decl->numeric.compute == core::type::TypeCode::Float32 &&
               (decl->numeric.sensitiveTo & core::sem::kContract);
    }
    IndexExpr operand(const Node &n, size_t side, const IndexExpr &at) {
        if (elementwise(n.op) && n.indexing)
            return pointwiseAddress(p, n, side, at);
        if (n.op == "reindex" && n.indexing)
            return coreViewAddress(p, n, at);
        return n.op == "reindex"   ? mapped(n, at)
               : n.op == "reshape" ? at
                                   : broadcast(p.nodes[n.inputs.at(side)].shape, n.shape, at);
    }
    ScalarExpr value(Block &body, size_t id, const IndexExpr &at) {
        if (id == reduction)
            return ScalarExpr::symbol("k_reduced", DType::F32);
        if (!member(id))
            return load(body, id, at);
        const auto &n         = p.nodes[id];
        const auto coordinate = name();
        body.push_back(IndexBinding{coordinate, at});
        const auto q = IndexExpr::variable(coordinate);
        auto input   = [&](size_t side) {
            return value(body, n.inputs.at(side), operand(n, side, q));
        };
        // Keep lexical permission checks on both nodes of a contracted multiply/add.
        if (n.op == "add" && contract(n))
            for (size_t side = 0; side < 2; ++side) {
                const auto productId = n.inputs[side];
                const auto &product  = p.nodes[productId];
                if (product.op != "multiply" || !member(productId) || !contract(product))
                    continue;
                const auto atProduct = operand(n, side, q);
                auto a = value(body, product.inputs[0], operand(product, 0, atProduct));
                auto b = value(body, product.inputs[1], operand(product, 1, atProduct));
                auto c = input(1 - side);
                return bind(body, ScalarExpr::apply(ScalarOp::Fma, {a, b, c}));
            }
        auto expr = elementwiseExpression(n.op).substitute("a", input(0));
        if (n.inputs.size() == 2)
            expr = expr.substitute("b", input(1));
        return bind(body, std::move(expr));
    }
};
} // namespace
KernelBody serialBody(const Program &p, Launch &launch) {
    const auto &root   = p.nodes.at(launch.node);
    const size_t count = elements(root.shape);
    KernelBody kernel;
    for (size_t i = 0; i < p.nodes.size(); ++i)
        kernel.buffers.push_back(
            {MemorySpace::Global,
             p.nodes[i].dtype,
             i,
             elements(p.nodes[i].shape),
             i == launch.node});
    Lowerer lower(p, launch);
    const auto row = IndexExpr::variable("k_row"), r = IndexExpr::variable("k_r");
    Block body;
    const Node *reducer = &root;
    if (launch.implementation == "ordered_reduce") {
        for (auto member : launch.members)
            if (p.nodes[member].op == "sum_axis" || p.nodes[member].op == "mean_axis") {
                lower.reduction = member;
                reducer         = &p.nodes[member];
            }
        if (lower.reduction == SIZE_MAX)
            throw std::runtime_error("missing ordered reducer");
    }
    const auto &op    = reducer->op;
    ScalarExpr result = ScalarExpr::literal(ScalarValue::f32(0));
    if (op == "constant64") {
        result = ScalarExpr::literal({DType::F64, static_cast<uint64_t>(root.attributes.at(0))});
    } else if (
        op == "sum_axis" || op == "mean_axis" || op == "max_axis" || op == "sum_all" ||
        op == "mean_all" || op == "sum_to") {
        const auto access = reductionAccess(p, *reducer, row, r);
        const auto width  = access.count;
        const auto &at    = access.address;
        const auto dtype  = (op == "max_axis" || op == "sum_to") ? DType::F32 : DType::F64;
        const auto acc    = ScalarExpr::symbol("k_acc", dtype);
        body.push_back(
            ScalarBinding{
                "k_acc",
                ScalarExpr::literal({dtype, op == "max_axis" ? 0xff800000ULL : 0})});
        Block terms;
        const auto input = lower.reduction == SIZE_MAX ? lower.load(terms, reducer->inputs[0], at)
                                                       : lower.value(terms, reducer->inputs[0], at);
        terms.push_back(
            Assignment{
                "k_acc",
                ScalarExpr::apply(
                    op == "max_axis" ? ScalarOp::Maximum : ScalarOp::Add,
                    {acc, ScalarExpr::cast(dtype, input)})});
        body.push_back(Loop{"k_r", IndexExpr(0), IndexExpr(width), 1, std::move(terms)});
        result = acc;
        if (op == "mean_axis" || op == "mean_all")
            result = ScalarExpr::apply(
                ScalarOp::Divide,
                {result, ScalarExpr::literal(ScalarValue::f64(static_cast<double>(width)))});
        result = ScalarExpr::cast(reducer->dtype, result);
        if (lower.reduction != SIZE_MAX) {
            body.push_back(ScalarBinding{"k_reduced", result});
            result = lower.value(body, launch.node, row);
        }
    } else
        result = lower.value(body, launch.node, row);
    body.push_back(Store{{launch.node, row, Predicate(true)}, result});
    launch.blockX = launch.implementation == "ordered_reduce"             ? 32
                    : (root.dtype == DType::F64 || root.op == "cast_f32") ? 1
                                                                          : 256;
    launch.gridX  = static_cast<unsigned>(
        std::clamp<size_t>((count + launch.blockX - 1) / launch.blockX, 1, 65535));
    const auto begin =
        IndexExpr::variable("block_x") * IndexExpr(launch.blockX) + IndexExpr::variable("thread_x");
    kernel.statements.push_back(
        Loop{
            "k_row",
            begin,
            IndexExpr(count),
            size_t(launch.blockX) * launch.gridX,
            std::move(body)});
    validateBody(kernel);
    return kernel;
}
} // namespace camel::tiles::kernel
