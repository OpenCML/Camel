/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Lane-local recurrences: owned body inputs become registers inside one physical loop.
#include "tiles/internal/kernel_ir.h"
#include "tiles/program.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace camel::tiles::kernel {
namespace {
void require(bool valid, const char *message) {
    if (!valid)
        throw std::runtime_error(std::string("tiles loop: ") + message);
}
bool scalar(const std::string &op) {
    return op == "scalar_add" || op == "scalar_sub" || op == "scalar_mul" || op == "scalar_div" ||
           op == "scalar_neg";
}
std::string operation(const std::string &op) {
    if (op == "scalar_add")
        return "add";
    if (op == "scalar_sub")
        return "subtract";
    if (op == "scalar_mul")
        return "multiply";
    if (op == "scalar_div")
        return "divide";
    if (op == "scalar_neg")
        return "neg";
    return op;
}
} // namespace
void validateLoopRegion(const Program &outer, const Node &node) {
    require(node.op == "loop" && node.loop, "missing owned recurrence");
    const auto &region = *node.loop;
    require(
        !region.stateInputs.empty() && region.nextStates.size() == region.stateInputs.size() &&
            node.results.size() == region.stateInputs.size(),
        "state/result arity mismatch");
    require(
        node.inputs.size() == region.stateInputs.size() + region.captureInputs.size(),
        "capture/input arity");
    const auto &body = region.body;
    for (const auto &value : body.nodes)
        require(!value.loop, "nested device loops are not supported");
    if (!region.domain.empty() && elements(node.shape) == 0)
        for (const auto &value : body.nodes)
            require(value.op != "scalar_div", "zero-lane loop would discard a scalar fault");
    validate(body, InputBindings::Deferred); // Includes every body contract and numeric grant.
    require(body.outputs == region.nextStates, "next-state output mismatch");
    std::vector<size_t> ports = region.stateInputs;
    ports.insert(ports.end(), region.captureInputs.begin(), region.captureInputs.end());
    require(
        std::unordered_set<size_t>(ports.begin(), ports.end()).size() == ports.size(),
        "duplicate body input binding");
    for (size_t i = 0; i < ports.size(); ++i) {
        require(
            ports[i] < body.nodes.size() && node.inputs[i] < outer.nodes.size(),
            "invalid loop binding");
        const auto &inner = body.nodes[ports[i]], &source = outer.nodes[node.inputs[i]];
        require(
            inner.op == "input" && inner.shape == source.shape && inner.dtype == source.dtype,
            "body binding shape/type mismatch");
        require(
            inner.data.empty() && !inner.scalarBits && !inner.resident,
            "body port retains an invocation value");
    }
    const auto &state = body.nodes[region.stateInputs[0]];
    require(node.shape == state.shape && node.dtype == state.dtype, "state result type");
    for (size_t i = 0; i < region.stateInputs.size(); ++i) {
        const auto &input = body.nodes[region.stateInputs[i]];
        require(
            input.shape == state.shape && input.dtype == state.dtype,
            "states have incompatible lane domains or dtypes");
        require(region.nextStates[i] < body.nodes.size(), "invalid next-state binding");
        const auto &next = body.nodes[region.nextStates[i]];
        require(next.shape == input.shape && next.dtype == input.dtype, "next-state type changed");
        require(
            node.results[i].shape == input.shape && node.results[i].dtype == input.dtype,
            "result descriptor type mismatch");
    }
    for (size_t id = 0; id < body.nodes.size(); ++id) {
        const auto &value = body.nodes[id];
        require(!value.loop, "nested device loops are not supported");
        if (value.op == "input") {
            if (std::find(ports.begin(), ports.end(), id) == ports.end())
                require(
                    value.dtype == DType::F32 && value.shape.empty() && value.data.size() == 1,
                    "only scalar embedded f32 body constants are supported");
        } else
            require(
                value.op == "loop_index_f64" || value.op == "constant64" ||
                    value.op == "cast_f32" || scalar(value.op) || elementwise(value.op),
                "body contains a non-pointwise operation");
        require(value.shape.empty() || value.shape == state.shape, "body changes the lane domain");
        if (elementwise(value.op))
            require(canonicalPointwise(body, value), "body index map is not lane-local");
    }
}
KernelBody loopBody(const Program &p, Launch &launch) {
    const auto &root = p.nodes.at(launch.node);
    validateLoopRegion(p, root);
    const auto &region = *root.loop;
    const auto &body   = region.body;
    KernelBody kernel;
    for (size_t i = 0; i < p.nodes.size(); ++i)
        kernel.buffers.push_back(
            {MemorySpace::Global,
             p.nodes[i].dtype,
             i,
             elements(p.nodes[i].shape),
             std::find(launch.outputs.begin(), launch.outputs.end(), i) != launch.outputs.end()});
    kernel.faultSlot = p.nodes.size();
    const auto table = kernel.buffers.size();
    if (!region.domain.empty()) {
        Buffer values{MemorySpace::Constant, ScalarType::I64, table, region.domain.size(), false};
        for (auto value : region.domain)
            values.constants.push_back(ScalarValue::i64(value));
        kernel.buffers.push_back(std::move(values));
    }
    const auto lane = IndexExpr::variable("k_lane");
    std::unordered_map<size_t, ScalarExpr> values;
    Block laneBody;
    std::vector<size_t> ports = region.stateInputs;
    ports.insert(ports.end(), region.captureInputs.begin(), region.captureInputs.end());
    for (size_t i = 0; i < ports.size(); ++i) {
        const auto &input = p.nodes[root.inputs[i]];
        const auto name   = "k_loop_input" + std::to_string(i);
        laneBody.push_back(
            Load{
                name,
                {root.inputs[i], input.shape.empty() ? IndexExpr(0) : lane, Predicate(true)},
                ScalarValue{input.dtype, 0}});
        values.emplace(ports[i], ScalarExpr::symbol(name, input.dtype));
    }
    Block iteration;
    if (!region.domain.empty())
        iteration.push_back(
            Load{
                "k_element",
                {table, IndexExpr::variable("k_iteration"), Predicate(true)},
                ScalarValue::i64(0)});
    for (size_t id = 0; id < body.nodes.size(); ++id) {
        if (values.contains(id))
            continue;
        const auto &node = body.nodes[id];
        auto expr        = ScalarExpr::literal(ScalarValue{node.dtype, 0});
        if (node.op == "input")
            expr = ScalarExpr::literal(ScalarValue::f32(node.data.at(0)));
        else if (node.op == "constant64")
            expr = ScalarExpr::literal({DType::F64, static_cast<uint64_t>(node.attributes.at(0))});
        else if (node.op == "loop_index_f64")
            expr = ScalarExpr::cast(DType::F64, ScalarExpr::symbol("k_element", ScalarType::I64));
        else {
            const auto a = values.at(node.inputs.at(0));
            expr         = elementwiseExpression(operation(node.op), node.dtype).substitute("a", a);
            if (node.inputs.size() == 2) {
                const auto b = values.at(node.inputs[1]);
                if (node.op == "scalar_div")
                    iteration.push_back(
                        Conditional{
                            ScalarPredicate{
                                ScalarCompare::Equal,
                                b,
                                ScalarExpr::literal(ScalarValue::f64(0))},
                            {ReportFault{static_cast<uint32_t>(launch.node)}, Return{}}});
                expr = expr.substitute("b", b);
            }
        }
        const auto name = "k_loop_value" + std::to_string(id);
        iteration.push_back(ScalarBinding{name, expr});
        values.emplace(id, ScalarExpr::symbol(name, node.dtype));
    }
    // Compute every next-state expression before updating any current state.
    for (size_t i = 0; i < region.stateInputs.size(); ++i)
        iteration.push_back(
            ScalarBinding{"k_next" + std::to_string(i), values.at(region.nextStates[i])});
    for (size_t i = 0; i < region.stateInputs.size(); ++i)
        iteration.push_back(
            Assignment{
                "k_loop_input" + std::to_string(i),
                ScalarExpr::symbol(
                    "k_next" + std::to_string(i),
                    body.nodes[region.stateInputs[i]].dtype)});
    if (!region.domain.empty())
        laneBody.push_back(
            Loop{
                "k_iteration",
                IndexExpr(0),
                IndexExpr(region.domain.size()),
                1,
                std::move(iteration)});
    require(launch.outputs.size() == region.stateInputs.size(), "launch result arity");
    for (size_t i = 0; i < launch.outputs.size(); ++i)
        laneBody.push_back(
            Store{
                {launch.outputs[i], lane, Predicate(true)},
                ScalarExpr::symbol("k_loop_input" + std::to_string(i), root.results[i].dtype)});
    launch.blockX = root.dtype == DType::F64 ? 1 : 256;
    launch.gridX  = static_cast<unsigned>(
        std::clamp<size_t>((elements(root.shape) + launch.blockX - 1) / launch.blockX, 1, 65535));
    kernel.statements.push_back(
        Loop{
            "k_lane",
            IndexExpr::variable("block_x") * IndexExpr(launch.blockX) +
                IndexExpr::variable("thread_x"),
            IndexExpr(elements(root.shape)),
            size_t(launch.blockX) * launch.gridX,
            std::move(laneBody)});
    validateBody(kernel);
    return kernel;
}
} // namespace camel::tiles::kernel
