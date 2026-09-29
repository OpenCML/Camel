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
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "vjp_registry.h"

#include "../tensor/ops/registry.h"
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

// Parameter reads have no OpDef; every other operator publishes its rule in the core derivative
// registry.

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

std::optional<slot_t> VjpBuildContext::staticValueOf(rt::gc_node_ref_t node) const {
    const auto *header = draft_.header(node);
    if (!header || header->kind != rt::GCNodeKind::Data || header->dataIndex >= 0) {
        return std::nullopt;
    }
    return draft_.staticSlots()[static_cast<size_t>(-header->dataIndex)];
}

rt::gc_node_ref_t VjpBuildContext::addStatic(slot_t value, Type *type) {
    return draft_.materializeStaticValue(value, type);
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
        registry.registerBuiltin("nn:value", valueVjp, "parameter_value_vjp");
    });
}

void applyVjpRule(
    VjpBuildContext &ctx, std::string_view key, std::span<const rt::gc_node_ref_t> inputs,
    rt::gc_node_ref_t output) {
    ensureBuiltinVjpRulesRegistered();
    // Parameter reads are registered here; every other operator publishes its rule in the core
    // derivative registry.
    if (const VjpRule *rule = VjpRegistry::instance().lookup(key);
        rule && rule->kind == VjpRuleKind::Builtin && rule->builtin) {
        rule->builtin(
            ctx,
            VjpPrimitiveCall{
                .key    = key,
                .inputs = inputs,
                .output = output,
            });
        return;
    }
    if (auto rule = camel::core::DerivativeRegistry::instance().findRule(key)) {
        rule(ctx, camel::core::VjpCall{.uri = key, .inputs = inputs, .output = output});
        return;
    }
    throwRuntimeFault(
        RuntimeDiag::RuntimeError,
        std::format("No builtin VJP rule registered for '{}'", std::string(key)));
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
