/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "vjp_registry.h"

#include "../tensor/type.h"

#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/conv.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/array.h"
#include "camel/runtime/draft.h"

#include <array>
#include <format>
#include <mutex>
#include <stdexcept>

namespace camel::nn {

namespace {

namespace rt = camel::runtime;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;
using camel::core::rtdata::Float64;
using camel::core::rtdata::toSlot;
using camel::core::type::ArrayType;
using camel::core::type::Type;

Type *tensorType() { return camel::tensor::TensorType::Default(); }

bool isTensorType(Type *type) {
    return type && type->code() == camel::tensor::TensorType::typeCode();
}

bool isFloat64(Type *type) { return type && type->equals(Type::Float64()); }

void requireInputCount(const VjpPrimitiveCall &call, size_t expected) {
    if (call.inputs.size() != expected) {
        throw std::runtime_error(
            std::string("VJP rule input-count mismatch for ") + std::string(call.key));
    }
}

void matmulVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 2);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }

    const rt::gc_node_ref_t lhs = call.inputs[0];
    const rt::gc_node_ref_t rhs = call.inputs[1];

    std::array<rt::gc_node_ref_t, 1> rhsTransposeInputs{rhs};
    const rt::gc_node_ref_t rhsT =
        ctx.addOper(tensorType(), "tensor:transpose", rhsTransposeInputs);
    std::array<rt::gc_node_ref_t, 2> lhsGradInputs{*dy, rhsT};
    ctx.accumulateGradient(lhs, ctx.addOper(tensorType(), "tensor:matmul", lhsGradInputs));

    std::array<rt::gc_node_ref_t, 1> lhsTransposeInputs{lhs};
    const rt::gc_node_ref_t lhsT =
        ctx.addOper(tensorType(), "tensor:transpose", lhsTransposeInputs);
    std::array<rt::gc_node_ref_t, 2> rhsGradInputs{lhsT, *dy};
    ctx.accumulateGradient(rhs, ctx.addOper(tensorType(), "tensor:matmul", rhsGradInputs));
}

void subtractVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 2);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }

    const rt::gc_node_ref_t lhs = call.inputs[0];
    const rt::gc_node_ref_t rhs = call.inputs[1];
    if (isTensorType(ctx.nodeType(lhs)) || isFloat64(ctx.nodeType(lhs))) {
        ctx.accumulateGradient(lhs, *dy);
    }
    if (isTensorType(ctx.nodeType(rhs))) {
        const rt::gc_node_ref_t minusOne = ctx.addStaticFloat(-1.0);
        std::array<rt::gc_node_ref_t, 2> negInputs{minusOne, *dy};
        ctx.accumulateGradient(rhs, ctx.addOper(tensorType(), "tensor:multiply", negInputs));
    } else if (isFloat64(ctx.nodeType(rhs))) {
        const rt::gc_node_ref_t zero = ctx.addStaticFloat(0.0);
        std::array<rt::gc_node_ref_t, 2> negInputs{zero, *dy};
        ctx.accumulateGradient(rhs, ctx.addOper(Type::Float64(), ":op/sub_d", negInputs));
    }
}

void multiplyVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 2);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }

    const rt::gc_node_ref_t lhs = call.inputs[0];
    const rt::gc_node_ref_t rhs = call.inputs[1];
    if (isTensorType(ctx.nodeType(lhs))) {
        std::array<rt::gc_node_ref_t, 2> lhsGradInputs{*dy, rhs};
        ctx.accumulateGradient(lhs, ctx.addOper(tensorType(), "tensor:multiply", lhsGradInputs));
    }
    if (isTensorType(ctx.nodeType(rhs))) {
        std::array<rt::gc_node_ref_t, 2> rhsGradInputs{*dy, lhs};
        ctx.accumulateGradient(rhs, ctx.addOper(tensorType(), "tensor:multiply", rhsGradInputs));
    }
}

void sumVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 1);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }

    const rt::gc_node_ref_t input = call.inputs[0];
    std::array<rt::gc_node_ref_t, 1> shapeInputs{input};
    const rt::gc_node_ref_t shape =
        ctx.addOper(ArrayType::create(Type::Int64()), "tensor:shape", shapeInputs);
    std::array<rt::gc_node_ref_t, 2> fullInputs{shape, *dy};
    ctx.accumulateGradient(input, ctx.addOper(tensorType(), "tensor:full", fullInputs));
}

void divScalarVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 2);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }

    const rt::gc_node_ref_t numerator   = call.inputs[0];
    const rt::gc_node_ref_t denominator = call.inputs[1];
    std::array<rt::gc_node_ref_t, 2> numeratorGradInputs{*dy, denominator};
    ctx.accumulateGradient(
        numerator, ctx.addOper(Type::Float64(), ":op/div_d", numeratorGradInputs));
}

void valueVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 1);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }
    ctx.accumulateParameterGradient(call.inputs[0], *dy);
}

void registerGraphAliases(VjpRegistry &registry, rt::GCGraph *target, rt::GCGraph *vjpGraph) {
    if (!target || !vjpGraph) {
        return;
    }
    registry.registerFunction(target->stableId(), vjpGraph);
    registry.registerFunction(target->mangledName(), vjpGraph);
    registry.registerFunction(target->name(), vjpGraph);
}

} // namespace

VjpBuildContext::VjpBuildContext(rt::GraphDraft &draft) : draft_(draft) {}

Type *VjpBuildContext::nodeType(rt::gc_node_ref_t node) const {
    const auto *header = draft_.header(node);
    if (!header || !header->dataType) {
        throw std::runtime_error("VJP node type lookup failed.");
    }
    return header->dataType;
}

rt::gc_node_ref_t VjpBuildContext::addStaticFloat(double value) {
    return draft_.materializeStaticValue(toSlot<Float64>(value), Type::Float64());
}

rt::gc_node_ref_t VjpBuildContext::addOper(
    Type *type, std::string_view uri, std::span<const rt::gc_node_ref_t> normInputs) {
    const rt::gc_node_ref_t node = draft_.addOperNode(type, nullptr, uri);
    draft_.setNormInputs(node, normInputs);
    return node;
}

void VjpBuildContext::seedGradient(rt::gc_node_ref_t primal, rt::gc_node_ref_t gradient) {
    gradients_[primal] = gradient;
}

void VjpBuildContext::accumulateGradient(rt::gc_node_ref_t primal, rt::gc_node_ref_t gradient) {
    auto existing = gradients_.find(primal);
    if (existing == gradients_.end()) {
        gradients_.emplace(primal, gradient);
        return;
    }

    Type *primalType = nodeType(primal);
    if (isTensorType(primalType)) {
        std::array<rt::gc_node_ref_t, 2> addInputs{existing->second, gradient};
        existing->second = addOper(tensorType(), "tensor:add", addInputs);
        return;
    }
    if (isFloat64(primalType)) {
        std::array<rt::gc_node_ref_t, 2> addInputs{existing->second, gradient};
        existing->second = addOper(Type::Float64(), ":op/add_d", addInputs);
        return;
    }

    throw std::runtime_error("VJP gradient accumulation does not support this primal type.");
}

std::optional<rt::gc_node_ref_t> VjpBuildContext::gradientOf(rt::gc_node_ref_t primal) const {
    auto found = gradients_.find(primal);
    if (found == gradients_.end()) {
        return std::nullopt;
    }
    return found->second;
}

void VjpBuildContext::accumulateParameterGradient(
    rt::gc_node_ref_t parameter, rt::gc_node_ref_t gradient) {
    auto existing = parameterGradients_.find(parameter);
    if (existing == parameterGradients_.end()) {
        parameterGradients_.emplace(parameter, gradient);
        return;
    }
    std::array<rt::gc_node_ref_t, 2> addInputs{existing->second, gradient};
    existing->second = addOper(tensorType(), "tensor:add", addInputs);
}

std::vector<ParameterGradient> VjpBuildContext::parameterGradients() const {
    std::vector<ParameterGradient> result;
    result.reserve(parameterGradients_.size());
    for (const auto &[parameter, gradient] : parameterGradients_) {
        result.push_back(ParameterGradient{.parameter = parameter, .gradient = gradient});
    }
    return result;
}

VjpRegistry &VjpRegistry::instance() {
    static VjpRegistry registry;
    return registry;
}

void VjpRegistry::registerBuiltin(std::string key, BuiltinVjpRule rule, std::string label) {
    rules_[std::move(key)] = VjpRule{
        .kind          = VjpRuleKind::Builtin,
        .builtin       = rule,
        .functionGraph = nullptr,
        .label         = std::move(label),
    };
}

void VjpRegistry::registerFunction(std::string key, rt::GCGraph *functionGraph) {
    if (key.empty() || !functionGraph) {
        return;
    }
    rules_[std::move(key)] = VjpRule{
        .kind          = VjpRuleKind::Function,
        .builtin       = nullptr,
        .functionGraph = functionGraph,
        .label         = functionGraph->name(),
    };
}

const VjpRule *VjpRegistry::lookup(std::string_view key) const {
    auto found = rules_.find(std::string(key));
    if (found == rules_.end()) {
        return nullptr;
    }
    return &found->second;
}

void ensureBuiltinVjpRulesRegistered() {
    static std::once_flag once;
    std::call_once(once, [] {
        auto &registry = VjpRegistry::instance();
        registry.registerBuiltin("tensor:matmul", matmulVjp, "matmul_vjp");
        registry.registerBuiltin("tensor:subtract", subtractVjp, "subtract_vjp");
        registry.registerBuiltin("tensor:multiply", multiplyVjp, "multiply_vjp");
        registry.registerBuiltin("tensor:sum", sumVjp, "sum_vjp");
        registry.registerBuiltin(":op/div_d", divScalarVjp, "div_d_vjp");
        registry.registerBuiltin("nn:value", valueVjp, "parameter_value_vjp");
    });
}

void applyVjpRule(
    VjpBuildContext &ctx, std::string_view key, std::initializer_list<rt::gc_node_ref_t> inputs,
    rt::gc_node_ref_t output) {
    ensureBuiltinVjpRulesRegistered();
    const VjpRule *rule = VjpRegistry::instance().lookup(key);
    if (!rule || rule->kind != VjpRuleKind::Builtin || !rule->builtin) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::format("No builtin VJP rule registered for '{}'", std::string(key)));
    }
    std::vector<rt::gc_node_ref_t> inputVec(inputs);
    VjpPrimitiveCall call{
        .key    = key,
        .inputs = std::span<const rt::gc_node_ref_t>(inputVec),
        .output = output,
    };
    rule->builtin(ctx, call);
}

void registerFunctionVjp(::Function *target, ::Function *vjp) {
    if (!target || !target->graph() || !vjp || !vjp->graph()) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "vjp decorator requires function values");
    }
    registerGraphAliases(VjpRegistry::instance(), target->graph(), vjp->graph());
}

} // namespace camel::nn
