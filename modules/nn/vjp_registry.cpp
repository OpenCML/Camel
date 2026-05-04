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
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "vjp_registry.h"

#include "../tensor/type.h"

#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/conv.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/array.h"
#include "camel/runtime/draft.h"

#include <algorithm>
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

void addVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
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
    if (isTensorType(ctx.nodeType(rhs)) || isFloat64(ctx.nodeType(rhs))) {
        ctx.accumulateGradient(rhs, *dy);
    }
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

void divideVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 2);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }

    const rt::gc_node_ref_t lhs = call.inputs[0];
    const rt::gc_node_ref_t rhs = call.inputs[1];
    if (isTensorType(ctx.nodeType(lhs))) {
        std::array<rt::gc_node_ref_t, 2> lhsGradInputs{*dy, rhs};
        ctx.accumulateGradient(lhs, ctx.addOper(tensorType(), "tensor:divide", lhsGradInputs));
    }
    if (isTensorType(ctx.nodeType(rhs))) {
        const rt::gc_node_ref_t zero = ctx.addStaticFloat(0.0);
        std::array<rt::gc_node_ref_t, 2> negDyInputs{zero, *dy};
        const rt::gc_node_ref_t negDy = ctx.addOper(tensorType(), "tensor:subtract", negDyInputs);
        std::array<rt::gc_node_ref_t, 2> rhsSquaredInputs{rhs, rhs};
        const rt::gc_node_ref_t rhsSquared =
            ctx.addOper(tensorType(), "tensor:multiply", rhsSquaredInputs);
        std::array<rt::gc_node_ref_t, 2> numeratorInputs{negDy, lhs};
        const rt::gc_node_ref_t numerator =
            ctx.addOper(tensorType(), "tensor:multiply", numeratorInputs);
        std::array<rt::gc_node_ref_t, 2> rhsGradInputs{numerator, rhsSquared};
        ctx.accumulateGradient(rhs, ctx.addOper(tensorType(), "tensor:divide", rhsGradInputs));
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

void transposeVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 1);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }
    std::array<rt::gc_node_ref_t, 1> inputs{*dy};
    ctx.accumulateGradient(call.inputs[0], ctx.addOper(tensorType(), "tensor:transpose", inputs));
}

void reshapeVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 2);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }
    std::array<rt::gc_node_ref_t, 1> shapeInputs{call.inputs[0]};
    const rt::gc_node_ref_t originalShape =
        ctx.addOper(ArrayType::create(Type::Int64()), "tensor:shape", shapeInputs);
    std::array<rt::gc_node_ref_t, 2> reshapeInputs{*dy, originalShape};
    ctx.accumulateGradient(
        call.inputs[0],
        ctx.addOper(tensorType(), "tensor:reshape", reshapeInputs));
}

void expVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 1);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }
    std::array<rt::gc_node_ref_t, 2> inputs{*dy, call.output};
    ctx.accumulateGradient(call.inputs[0], ctx.addOper(tensorType(), "tensor:multiply", inputs));
}

void logVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 1);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }
    std::array<rt::gc_node_ref_t, 2> inputs{*dy, call.inputs[0]};
    ctx.accumulateGradient(call.inputs[0], ctx.addOper(tensorType(), "tensor:divide", inputs));
}

void sigmoidVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 1);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }
    const rt::gc_node_ref_t one = ctx.addStaticFloat(1.0);
    std::array<rt::gc_node_ref_t, 2> oneMinusYInputs{one, call.output};
    const rt::gc_node_ref_t oneMinusY =
        ctx.addOper(tensorType(), "tensor:subtract", oneMinusYInputs);
    std::array<rt::gc_node_ref_t, 2> yTimesInputs{call.output, oneMinusY};
    const rt::gc_node_ref_t yTimesOneMinusY =
        ctx.addOper(tensorType(), "tensor:multiply", yTimesInputs);
    std::array<rt::gc_node_ref_t, 2> gradInputs{*dy, yTimesOneMinusY};
    ctx.accumulateGradient(
        call.inputs[0],
        ctx.addOper(tensorType(), "tensor:multiply", gradInputs));
}

void tanhVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 1);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }
    const rt::gc_node_ref_t one = ctx.addStaticFloat(1.0);
    std::array<rt::gc_node_ref_t, 2> ySquaredInputs{call.output, call.output};
    const rt::gc_node_ref_t ySquared = ctx.addOper(tensorType(), "tensor:multiply", ySquaredInputs);
    std::array<rt::gc_node_ref_t, 2> factorInputs{one, ySquared};
    const rt::gc_node_ref_t factor = ctx.addOper(tensorType(), "tensor:subtract", factorInputs);
    std::array<rt::gc_node_ref_t, 2> gradInputs{*dy, factor};
    ctx.accumulateGradient(
        call.inputs[0],
        ctx.addOper(tensorType(), "tensor:multiply", gradInputs));
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
        numerator,
        ctx.addOper(Type::Float64(), ":op/div_d", numeratorGradInputs));
}

void addScalarVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 2);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }
    ctx.accumulateGradient(call.inputs[0], *dy);
    ctx.accumulateGradient(call.inputs[1], *dy);
}

void subScalarVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 2);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }
    ctx.accumulateGradient(call.inputs[0], *dy);
    const rt::gc_node_ref_t zero = ctx.addStaticFloat(0.0);
    std::array<rt::gc_node_ref_t, 2> negInputs{zero, *dy};
    ctx.accumulateGradient(call.inputs[1], ctx.addOper(Type::Float64(), ":op/sub_d", negInputs));
}

void mulScalarVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 2);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }
    std::array<rt::gc_node_ref_t, 2> lhsGradInputs{*dy, call.inputs[1]};
    ctx.accumulateGradient(
        call.inputs[0],
        ctx.addOper(Type::Float64(), ":op/mul_d", lhsGradInputs));
    std::array<rt::gc_node_ref_t, 2> rhsGradInputs{*dy, call.inputs[0]};
    ctx.accumulateGradient(
        call.inputs[1],
        ctx.addOper(Type::Float64(), ":op/mul_d", rhsGradInputs));
}

void valueVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 1);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }
    ctx.accumulateParameterGradient(call.inputs[0], *dy);
}

void softmaxCrossEntropyVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 2);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }

    std::array<rt::gc_node_ref_t, 3> inputs{call.inputs[0], call.inputs[1], *dy};
    ctx.accumulateGradient(
        call.inputs[0],
        ctx.addOper(tensorType(), "nn:softmax_cross_entropy_grad", inputs));
}

void embeddingVjp(VjpBuildContext &ctx, const VjpPrimitiveCall &call) {
    requireInputCount(call, 2);
    auto dy = ctx.gradientOf(call.output);
    if (!dy) {
        return;
    }

    std::array<rt::gc_node_ref_t, 3> inputs{call.inputs[0], call.inputs[1], *dy};
    ctx.accumulateGradient(
        call.inputs[0],
        ctx.addOper(tensorType(), "nn:embedding_table_grad", inputs));
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

std::string VjpBuildContext::parameterAliasKey(rt::gc_node_ref_t parameter) const {
    // Helper lowering clones structural access nodes at every call site. The
    // trainable leaf is the root object plus its field/index path, not the
    // particular ACCS node that happened to feed one nn:value read.
    auto unwrapGate = [this](rt::gc_node_ref_t node) {
        while (node != rt::kInvalidNodeRef) {
            const auto *header = draft_.header(node);
            if (!header || header->kind != rt::GCNodeKind::Gate) {
                return node;
            }
            const auto normInputs = draft_.normInputsOf(node);
            if (!normInputs.empty()) {
                node = normInputs.front();
                continue;
            }
            const auto withInputs = draft_.withInputsOf(node);
            if (!withInputs.empty()) {
                node = withInputs.front();
                continue;
            }
            return node;
        }
        return node;
    };

    std::vector<std::string> accessPath;
    rt::gc_node_ref_t current = unwrapGate(parameter);
    while (current != rt::kInvalidNodeRef) {
        current            = unwrapGate(current);
        const auto *header = draft_.header(current);
        if (!header || header->kind != rt::GCNodeKind::Accs) {
            break;
        }

        const auto payload = draft_.payloadOf(current);
        if (payload.size_bytes() < sizeof(rt::GCAccsBody)) {
            break;
        }
        const auto *body = reinterpret_cast<const rt::GCAccsBody *>(payload.data());
        if (body->accsKind == rt::GCAccsKind::StructKey) {
            accessPath.push_back(std::format(".{}", std::string(body->key())));
        } else {
            accessPath.push_back(std::format("[{}]", body->value));
        }

        const auto inputs = draft_.normInputsOf(current);
        if (inputs.empty()) {
            break;
        }
        current = inputs.front();
    }

    current         = unwrapGate(current);
    std::string key = std::format("root#{}", current);
    for (auto it = accessPath.rbegin(); it != accessPath.rend(); ++it) {
        key += *it;
    }
    return key;
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
    const std::string key = parameterAliasKey(parameter);
    auto existing         = parameterGradients_.find(key);
    if (existing == parameterGradients_.end()) {
        parameterGradients_.emplace(
            key,
            ParameterGradient{
                .parameter = parameter,
                .gradient  = gradient,
            });
        return;
    }
    std::array<rt::gc_node_ref_t, 2> addInputs{existing->second.gradient, gradient};
    existing->second.gradient = addOper(tensorType(), "tensor:add", addInputs);
}

std::vector<ParameterGradient> VjpBuildContext::parameterGradients() const {
    std::vector<ParameterGradient> result;
    result.reserve(parameterGradients_.size());
    for (const auto &entry : parameterGradients_) {
        result.push_back(entry.second);
    }
    std::ranges::sort(result, {}, &ParameterGradient::parameter);
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
        registry.registerBuiltin("tensor:add", addVjp, "add_vjp");
        registry.registerBuiltin("tensor:matmul", matmulVjp, "matmul_vjp");
        registry.registerBuiltin("tensor:subtract", subtractVjp, "subtract_vjp");
        registry.registerBuiltin("tensor:multiply", multiplyVjp, "multiply_vjp");
        registry.registerBuiltin("tensor:divide", divideVjp, "divide_vjp");
        registry.registerBuiltin("tensor:sum", sumVjp, "sum_vjp");
        registry.registerBuiltin("tensor:transpose", transposeVjp, "transpose_vjp");
        registry.registerBuiltin("tensor:reshape", reshapeVjp, "reshape_vjp");
        registry.registerBuiltin("tensor:exp", expVjp, "exp_vjp");
        registry.registerBuiltin("tensor:log", logVjp, "log_vjp");
        registry.registerBuiltin("tensor:sigmoid", sigmoidVjp, "sigmoid_vjp");
        registry.registerBuiltin("tensor:tanh", tanhVjp, "tanh_vjp");
        registry.registerBuiltin(":op/add_d", addScalarVjp, "add_d_vjp");
        registry.registerBuiltin(":op/sub_d", subScalarVjp, "sub_d_vjp");
        registry.registerBuiltin(":op/mul_d", mulScalarVjp, "mul_d_vjp");
        registry.registerBuiltin(":op/div_d", divScalarVjp, "div_d_vjp");
        registry.registerBuiltin("nn:value", valueVjp, "parameter_value_vjp");
        registry.registerBuiltin(
            "nn:softmax_cross_entropy",
            softmaxCrossEntropyVjp,
            "softmax_cross_entropy_vjp");
        registry.registerBuiltin("nn:embedding", embeddingVjp, "embedding_vjp");
    });
}

void applyVjpRule(
    VjpBuildContext &ctx, std::string_view key, std::span<const rt::gc_node_ref_t> inputs,
    rt::gc_node_ref_t output) {
    ensureBuiltinVjpRulesRegistered();
    const VjpRule *rule = VjpRegistry::instance().lookup(key);
    if (!rule || rule->kind != VjpRuleKind::Builtin || !rule->builtin) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::format("No builtin VJP rule registered for '{}'", std::string(key)));
    }
    VjpPrimitiveCall call{
        .key    = key,
        .inputs = inputs,
        .output = output,
    };
    rule->builtin(ctx, call);
}

void applyVjpRule(
    VjpBuildContext &ctx, std::string_view key, std::initializer_list<rt::gc_node_ref_t> inputs,
    rt::gc_node_ref_t output) {
    std::vector<rt::gc_node_ref_t> inputVec(inputs);
    applyVjpRule(ctx, key, std::span<const rt::gc_node_ref_t>(inputVec), output);
}

void registerFunctionVjp(::Function *target, ::Function *vjp) {
    if (!target || !target->graph() || !vjp || !vjp->graph()) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "vjp decorator requires function values");
    }
    registerGraphAliases(VjpRegistry::instance(), target->graph(), vjp->graph());
}

} // namespace camel::nn
