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

/*
 * Demo reverse-mode graph synthesis.
 *
 * This is not the final autograd engine. The legacy `apply_gradients` path
 * still emits one training graph for the common linear regression MSE pattern:
 *
 *   pred = X @ w
 *   grad = 2 / m * transpose(X) @ (pred - y)
 *   new_w = w - lr * grad
 *
 * The with-context `compile_step` path now builds the same pattern through the
 * VJP registry, which is the first architectural slice of generic autograd.
 */

#include "operators.h"

#include "../tensor/type.h"
#include "runtime.h"
#include "type.h"
#include "vjp_registry.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft.h"
#include "camel/utils/type.h"

#include <atomic>
#include <format>
#include <memory>
#include <optional>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::error;
using namespace camel::core::rtdata;
using namespace camel::core::type;
using namespace camel::runtime;

namespace {

Type *tensorType() { return camel::tensor::TensorType::Default(); }

Type *parameterType() { return camel::nn::ParameterType::Default(); }

FunctionType *linearSgdStepType() {
    Type *tensor = tensorType();
    return FunctionType::create(
        {},
        {
            {tensor, false},
            {tensor, false},
            {tensor, false},
            {Type::Float64(), false},
        },
        tensor,
        Modifier::None);
}

bool looksLikeScalarLoss(FunctionType *funcType) {
    if (!funcType || funcType->normTypesCount() != 3 || funcType->withTypesCount() != 0) {
        return false;
    }
    return funcType->exitType() == Type::Float64() || funcType->exitType() == Type::Float32();
}

bool looksLikeLinearContextLoss(FunctionType *funcType) {
    if (!funcType || funcType->withTypesCount() != 2 || funcType->normTypesCount() != 1) {
        return false;
    }
    if (funcType->exitType() != Type::Float64() && funcType->exitType() != Type::Float32()) {
        return false;
    }
    if (funcType->withTypeAt(1)->code() != camel::tensor::TensorType::typeCode() ||
        funcType->normTypeAt(0)->code() != camel::tensor::TensorType::typeCode()) {
        return false;
    }
    if (funcType->withTypeAt(0)->code() != TypeCode::Struct) {
        return false;
    }
    auto *modelType = tt::as_ptr<StructType>(funcType->withTypeAt(0));
    auto weight     = modelType->findField("weight");
    return weight.has_value() &&
           modelType->typeAt(*weight)->code() == camel::nn::ParameterType::typeCode();
}

FunctionType *linearContextSgdStepType(FunctionType *lossType) {
    param_vec_t withParams;
    withParams.reserve(lossType->withTypesCount());
    for (size_t i = 0; i < lossType->withTypesCount(); ++i) {
        withParams.emplace_back(lossType->withTypeAt(i), i == 0);
    }

    param_vec_t normParams;
    normParams.reserve(lossType->normTypesCount() + 1);
    for (size_t i = 0; i < lossType->normTypesCount(); ++i) {
        normParams.emplace_back(lossType->normTypeAt(i), false);
    }
    normParams.emplace_back(Type::Float64(), false);
    return FunctionType::create(withParams, normParams, Type::Float64(), Modifier::Sync);
}

class ApplyGradientsResolver final : public FuncTypeResolver {
  public:
    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm,
        const ModifierSet &modifiers) const override {
        (void)modifiers;
        if (!with.empty() || norm.size() != 1 || norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        auto *lossType = tt::as_ptr<FunctionType>(norm[0]);
        if (!looksLikeScalarLoss(lossType)) {
            return std::nullopt;
        }
        return FunctionType::create({}, {{norm[0], false}}, linearSgdStepType(), Modifier::Macro);
    }

    std::string signature() const override {
        return "(loss: (w: Tensor, X: Tensor, y: Tensor) => float) => "
               "(w: Tensor, X: Tensor, y: Tensor, lr: float) => Tensor";
    }
};

class CompileStepResolver final : public FuncTypeResolver {
  public:
    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm,
        const ModifierSet &modifiers) const override {
        (void)modifiers;
        if (!with.empty() || norm.size() != 1 || norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        auto *lossType = tt::as_ptr<FunctionType>(norm[0]);
        if (!looksLikeLinearContextLoss(lossType)) {
            return std::nullopt;
        }
        return FunctionType::create(
            {},
            {{norm[0], false}},
            linearContextSgdStepType(lossType),
            Modifier::Macro);
    }

    std::string signature() const override {
        return "(loss: <layer: { weight: Parameter }, y: Tensor> (x: Tensor) => float) => "
               "sync <var layer: { weight: Parameter }, y: Tensor> "
               "(x: Tensor, lr: float) => float";
    }
};

class VjpDecoratorResolver final : public FuncTypeResolver {
  public:
    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm,
        const ModifierSet &modifiers) const override {
        (void)modifiers;
        if (with.size() != 1 || norm.size() != 1 || with[0]->code() != TypeCode::Function ||
            norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        return FunctionType::create(
            {{with[0], false}},
            {{norm[0], false}},
            norm[0],
            Modifier::Macro);
    }

    std::string signature() const override {
        return "<rule: Function> (f: Function) => Function";
    }
};

gc_node_ref_t addWithPort(GraphDraft &draft, Type *type) {
    const gc_slot_idx_t slot = draft.allocateRuntimeSlot(type);
    const gc_node_ref_t node = draft.addPortNode(type, slot);
    draft.appendWithPort(node);
    return node;
}

gc_node_ref_t addNormPort(GraphDraft &draft, Type *type) {
    const gc_slot_idx_t slot = draft.allocateRuntimeSlot(type);
    const gc_node_ref_t node = draft.addPortNode(type, slot);
    draft.appendNormPort(node);
    return node;
}

gc_node_ref_t addStaticFloat(GraphDraft &draft, double value) {
    return draft.materializeStaticValue(toSlot<Float64>(value), Type::Float64());
}

gc_node_ref_t addStaticInt(GraphDraft &draft, int64_t value) {
    return draft.materializeStaticValue(toSlot<Int64>(value), Type::Int64());
}

gc_node_ref_t addOper(
    GraphDraft &draft, Type *type, std::string_view uri,
    std::initializer_list<gc_node_ref_t> normInputs) {
    const gc_node_ref_t node = draft.addOperNode(type, nullptr, uri);
    std::vector<gc_node_ref_t> inputs(normInputs.begin(), normInputs.end());
    draft.setNormInputs(node, inputs);
    return node;
}

GCGraph *buildLinearMseSgdStepGraph() {
    static std::atomic<size_t> nextId{0};
    const size_t id = nextId.fetch_add(1, std::memory_order_relaxed);

    Type *tensor = tensorType();
    GraphDraft draft;
    draft.setFuncType(linearSgdStepType());
    draft.setClosureType(TupleType::create());

    const gc_node_ref_t w  = addNormPort(draft, tensor);
    const gc_node_ref_t x  = addNormPort(draft, tensor);
    const gc_node_ref_t y  = addNormPort(draft, tensor);
    const gc_node_ref_t lr = addNormPort(draft, Type::Float64());

    const gc_node_ref_t pred = addOper(draft, tensor, "tensor:matmul", {x, w});
    const gc_node_ref_t errors = addOper(draft, tensor, "tensor:subtract", {pred, y});
    const gc_node_ref_t xT     = addOper(draft, tensor, "tensor:transpose", {x});
    const gc_node_ref_t dw     = addOper(draft, tensor, "tensor:matmul", {xT, errors});

    const gc_node_ref_t shape =
        addOper(draft, ArrayType::create(Type::Int64()), "tensor:shape", {x});
    const gc_node_ref_t zero   = addStaticInt(draft, 0);
    const gc_node_ref_t m      = addOper(draft, Type::Int64(), ":op/idx_arr", {shape, zero});
    const gc_node_ref_t mFloat = draft.addCastNode(Type::Float64());
    draft.setNormInputs(mFloat, std::span<const gc_node_ref_t>(&m, 1));

    const gc_node_ref_t two   = addStaticFloat(draft, 2.0);
    const gc_node_ref_t scale = addOper(draft, Type::Float64(), ":op/div_d", {two, mFloat});
    const gc_node_ref_t grad  = addOper(draft, tensor, "tensor:multiply", {scale, dw});
    const gc_node_ref_t step  = addOper(draft, tensor, "tensor:multiply", {lr, grad});
    const gc_node_ref_t newW  = addOper(draft, tensor, "tensor:subtract", {w, step});

    draft.setEntryNode(pred);
    draft.setOutputNode(newW);
    draft.setExitNode(newW);
    draft.setReturnNode(newW, GCReturnKind::Self);

    const std::string suffix = std::format("${}", id);
    return draft.encode(
        "nn.apply_gradients.linear_mse_sgd" + suffix,
        "nn.apply_gradients.linear_mse_sgd" + suffix,
        "nn::linear_mse_sgd_step" + suffix);
}

GCGraph *buildLinearContextMseSgdStepGraph(FunctionType *lossType) {
    static std::atomic<size_t> nextId{0};
    const size_t id = nextId.fetch_add(1, std::memory_order_relaxed);

    Type *tensor = tensorType();
    GraphDraft draft;
    draft.setFuncType(linearContextSgdStepType(lossType));
    draft.setClosureType(TupleType::create());

    const gc_node_ref_t layer = addWithPort(draft, lossType->withTypeAt(0));
    const gc_node_ref_t y     = addWithPort(draft, tensor);
    const gc_node_ref_t x     = addNormPort(draft, tensor);
    const gc_node_ref_t lr    = addNormPort(draft, Type::Float64());

    const gc_node_ref_t weight = draft.addAccsNode(parameterType(), "weight");
    draft.setNormInputs(weight, std::span<const gc_node_ref_t>(&layer, 1));

    const gc_node_ref_t wData = addOper(draft, tensor, "nn:value", {weight});
    const gc_node_ref_t pred  = addOper(draft, tensor, "tensor:matmul", {x, wData});
    const gc_node_ref_t errors = addOper(draft, tensor, "tensor:subtract", {pred, y});
    const gc_node_ref_t squared = addOper(draft, tensor, "tensor:multiply", {errors, errors});
    const gc_node_ref_t lossSum = addOper(draft, Type::Float64(), "tensor:sum", {squared});

    const gc_node_ref_t shape =
        addOper(draft, ArrayType::create(Type::Int64()), "tensor:shape", {x});
    const gc_node_ref_t zero   = addStaticInt(draft, 0);
    const gc_node_ref_t m      = addOper(draft, Type::Int64(), ":op/idx_arr", {shape, zero});
    const gc_node_ref_t mFloat = draft.addCastNode(Type::Float64());
    draft.setNormInputs(mFloat, std::span<const gc_node_ref_t>(&m, 1));
    const gc_node_ref_t loss = addOper(draft, Type::Float64(), ":op/div_d", {lossSum, mFloat});

    camel::nn::VjpBuildContext vjp(draft);
    vjp.seedGradient(loss, addStaticFloat(draft, 1.0));
    camel::nn::applyVjpRule(vjp, ":op/div_d", {lossSum, mFloat}, loss);
    camel::nn::applyVjpRule(vjp, "tensor:sum", {squared}, lossSum);
    camel::nn::applyVjpRule(vjp, "tensor:multiply", {errors, errors}, squared);
    camel::nn::applyVjpRule(vjp, "tensor:subtract", {pred, y}, errors);
    camel::nn::applyVjpRule(vjp, "tensor:matmul", {x, wData}, pred);
    camel::nn::applyVjpRule(vjp, "nn:value", {weight}, wData);

    gc_node_ref_t updateTail = kInvalidNodeRef;
    for (const auto &paramGrad : vjp.parameterGradients()) {
        const gc_node_ref_t zeroGrad =
            addOper(draft, Type::Void(), "nn:zero_grad", {paramGrad.parameter});
        if (updateTail != kInvalidNodeRef) {
            draft.setCtrlInputs(zeroGrad, std::span<const gc_node_ref_t>(&updateTail, 1));
        }

        const gc_node_ref_t addGrad =
            addOper(draft, Type::Void(), "nn:add_grad", {paramGrad.parameter, paramGrad.gradient});
        draft.setCtrlInputs(addGrad, std::span<const gc_node_ref_t>(&zeroGrad, 1));

        const gc_node_ref_t sgd = addOper(draft, Type::Void(), "nn:sgd", {paramGrad.parameter, lr});
        draft.setCtrlInputs(sgd, std::span<const gc_node_ref_t>(&addGrad, 1));
        updateTail = sgd;
    }
    if (updateTail == kInvalidNodeRef) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "compile_step found no trainable Parameter");
    }

    const auto *lossHeader = draft.header(loss);
    ASSERT(lossHeader != nullptr, "Loss node header is missing.");
    const gc_node_ref_t gatedLoss =
        draft.addGateNode(Type::Float64(), lossHeader->dataIndex);
    draft.setNormInputs(gatedLoss, std::span<const gc_node_ref_t>(&loss, 1));
    draft.setCtrlInputs(gatedLoss, std::span<const gc_node_ref_t>(&updateTail, 1));

    draft.setEntryNode(pred);
    draft.setOutputNode(gatedLoss);
    draft.setExitNode(gatedLoss);
    draft.setReturnNode(gatedLoss, GCReturnKind::Self);

    const std::string suffix = std::format("${}", id);
    return draft.encode(
        "nn.compile_step.linear_context_mse_sgd" + suffix,
        "nn.compile_step.linear_context_mse_sgd" + suffix,
        "nn::linear_context_mse_sgd_step" + suffix);
}

} // namespace

std::unordered_map<std::string, operator_t> getNnOpsMap() {
    return {
        {"apply_gradients", __nn_apply_gradients__},
        {"compile_step", __nn_compile_step__},
        {"vjp", __nn_vjp__},
        {"parameter", __nn_parameter__},
        {"value", __nn_value__},
        {"grad", __nn_grad__},
        {"zero_grad", __nn_zero_grad__},
        {"add_grad", __nn_add_grad__},
        {"sgd", __nn_sgd__},
    };
}

const std::vector<oper_group_ptr_t> &getNnOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "apply_gradients",
            {{"nn:apply_gradients",
              std::make_shared<ApplyGradientsResolver>()}}),
        OperatorGroup::create(
            "compile_step",
            {{"nn:compile_step",
              std::make_shared<CompileStepResolver>()}}),
        OperatorGroup::create(
            "vjp",
            {{"nn:vjp",
              std::make_shared<VjpDecoratorResolver>()}}),
        OperatorGroup::create(
            "parameter",
            {{"nn:parameter",
              StaticFuncTypeResolver::create(
                  {},
                  {{tensorType(), false}},
                  parameterType())}}),
        OperatorGroup::create(
            "value",
            {{"nn:value",
              StaticFuncTypeResolver::create(
                  {},
                  {{parameterType(), false}},
                  tensorType())}}),
        OperatorGroup::create(
            "grad",
            {{"nn:grad",
              StaticFuncTypeResolver::create(
                  {},
                  {{parameterType(), false}},
                  tensorType())}}),
        OperatorGroup::create(
            "zero_grad",
            {{"nn:zero_grad",
              StaticFuncTypeResolver::create(
                  {},
                  {{parameterType(), true}},
                  Type::Void(),
                  Modifier::Sync)}}),
        OperatorGroup::create(
            "add_grad",
            {{"nn:add_grad",
              StaticFuncTypeResolver::create(
                  {},
                  {{parameterType(), true}, {tensorType(), false}},
                  Type::Void(),
                  Modifier::Sync)}}),
        OperatorGroup::create(
            "sgd",
            {{"nn:sgd",
              StaticFuncTypeResolver::create(
                  {},
                  {{parameterType(), true}, {Type::Float64(), false}},
                  Type::Void(),
                  Modifier::Sync)}}),
    };
    return groups;
}

slot_t __nn_apply_gradients__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    auto *loss = norm.get<Function *>(0);
    if (!loss || !loss->graph()) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "apply_gradients requires a static loss graph");
    }
    auto *lossType = loss->graph()->funcType();
    if (!looksLikeScalarLoss(lossType)) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "apply_gradients demo expects (w: Tensor, X: Tensor, y: Tensor) => float");
    }

    GCGraph *stepGraph = buildLinearMseSgdStepGraph();
    Function *step     = Function::create(stepGraph, TupleType::create(), mm::autoSpace());
    return toSlot<Function *>(step);
}

slot_t __nn_compile_step__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    camel::nn::ensureBuiltinVjpRulesRegistered();
    auto *loss = norm.get<Function *>(0);
    if (!loss || !loss->graph()) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "compile_step requires a static loss graph");
    }
    auto *lossType = loss->graph()->funcType();
    if (!looksLikeLinearContextLoss(lossType)) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "compile_step demo expects <layer: { weight: Parameter }, y: Tensor> "
            "(x: Tensor) => float");
    }

    GCGraph *stepGraph = buildLinearContextMseSgdStepGraph(lossType);
    Function *step     = Function::create(stepGraph, TupleType::create(), mm::autoSpace());
    return toSlot<Function *>(step);
}

slot_t __nn_vjp__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)ctx;
    auto *rule   = with.get<Function *>(0);
    auto *target = norm.get<Function *>(0);
    camel::nn::registerFunctionVjp(target, rule);
    return toSlot<Function *>(target);
}

slot_t __nn_parameter__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    auto *tensor = norm.get<camel::tensor::TensorObject *>(0);
    return toSlot<camel::nn::ParameterObject *>(
        camel::nn::ParameterObject::create(tensor, mm::autoSpace()));
}

slot_t __nn_value__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    return toSlot<camel::tensor::TensorObject *>(camel::nn::requireParameter(norm, 0)->data());
}

slot_t __nn_grad__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    return toSlot<camel::tensor::TensorObject *>(camel::nn::requireParameter(norm, 0)->grad());
}

slot_t __nn_zero_grad__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    camel::nn::requireParameter(norm, 0)->zeroGrad();
    return NullSlot;
}

slot_t __nn_add_grad__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    auto *param = camel::nn::requireParameter(norm, 0);
    auto *grad  = norm.get<camel::tensor::TensorObject *>(1);
    param->addGrad(grad);
    return NullSlot;
}

slot_t __nn_sgd__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    camel::nn::requireParameter(norm, 0)->sgdUpdate(norm.get<Float64>(1));
    return NullSlot;
}
