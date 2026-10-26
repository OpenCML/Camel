// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Structured scalar chains with explicit device fault and early-return nodes.

#include "tiles/internal/kernel_ir.h"
#include "tiles/program.h"
#include <cstdint>
#include <string>
#include <unordered_map>

namespace camel::tiles::kernel {
KernelBody scalarBody(const Program &p, Launch &launch) {
    launch.blockX = launch.gridX = 1;
    KernelBody kernel;
    kernel.faultSlot = p.nodes.size();
    for (size_t i = 0; i < p.nodes.size(); ++i)
        kernel.buffers.push_back(
            {MemorySpace::Global,
             p.nodes[i].dtype,
             i,
             elements(p.nodes[i].shape),
             i == launch.node});
    std::unordered_map<size_t, ScalarExpr> values;
    auto operand = [&](size_t id) {
        if (auto it = values.find(id); it != values.end())
            return it->second;
        const auto name = "k_input" + std::to_string(id);
        // A load's fallback is the value's own width: it is the predicate's value when the
        // access is masked off, and the checker holds it to the buffer's type.
        const DType type = p.nodes[id].dtype;
        kernel.statements.push_back(
            Load{name,
                 {id, IndexExpr(0), Predicate(true)},
                 type == DType::F32 ? ScalarValue::f32(0) : ScalarValue::f64(0)});
        auto value = ScalarExpr::symbol(name, type);
        values.emplace(id, value);
        return value;
    };
    for (auto id : launch.members) {
        const auto &n     = p.nodes[id];
        const auto name   = "k_value" + std::to_string(id);
        const auto zero   = n.dtype == DType::F32 ? ScalarValue::f32(0) : ScalarValue::f64(0);
        ScalarExpr result = ScalarExpr::literal(zero);
        if (n.op == "constant64")
            result = ScalarExpr::literal({DType::F64, static_cast<uint64_t>(n.attributes.at(0))});
        else {
            auto a               = operand(n.inputs.at(0));
            const auto operation = n.op == "scalar_add"   ? "add"
                                   : n.op == "scalar_sub" ? "subtract"
                                   : n.op == "scalar_mul" ? "multiply"
                                   : n.op == "scalar_div" ? "divide"
                                                          : "neg";
            result               = elementwiseExpression(operation, n.dtype).substitute("a", a);
            if (n.inputs.size() == 2) {
                auto b = operand(n.inputs[1]);
                if (n.op == "scalar_div")
                    kernel.statements.push_back(
                        Conditional{
                            ScalarPredicate{
                                ScalarCompare::Equal,
                                b,
                                ScalarExpr::literal(zero)},
                            {ReportFault{static_cast<uint32_t>(id)},
                             Store{
                                 {launch.node, IndexExpr(0), Predicate(true)},
                                 ScalarExpr::literal(zero)},
                             Return{}}});
                result = result.substitute("b", b);
            }
        }
        kernel.statements.push_back(ScalarBinding{name, result});
        values.emplace(id, ScalarExpr::symbol(name, n.dtype));
    }
    kernel.statements.push_back(
        Store{{launch.node, IndexExpr(0), Predicate(true)}, values.at(launch.node)});
    validateBody(kernel);
    return kernel;
}
} // namespace camel::tiles::kernel
