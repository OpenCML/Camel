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

/*
 * Reverse-mode graph synthesis for small static NN loss graphs.
 *
 * `apply_gradients` is the public training-step macro. It clones the supplied
 * scalar loss graph into a sync step graph, seeds the loss cotangent with 1.0,
 * lowers static helper/layer calls on the output-reachable tape, walks the
 * resulting primitive
 * graph backward through the VJP registry, and appends
 * deterministic SGD updates for every
 * `nn:value(Parameter)` leaf that receives
 * a gradient.
 */

#include "operators.h"

#include "../tensor/type.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_inline.h"
#include "camel/runtime/draft_session.h"
#include "camel/utils/type.h"
#include "runtime.h"
#include "type.h"
#include "vjp_registry.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <memory>
#include <optional>
#include <unordered_set>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::error;
using namespace camel::core::rtdata;
using namespace camel::core::type;
using namespace camel::runtime;

namespace {

Type *tensorType() { return camel::tensor::TensorType::Default(); }

Type *parameterType() { return camel::nn::ParameterType::Default(); }

bool isScalarLossType(FunctionType *funcType) {
    if (!funcType || funcType->modifiers().sync() || funcType->modifiers().macro()) {
        return false;
    }
    return funcType->exitType() == Type::Float64() || funcType->exitType() == Type::Float32();
}

bool typeContainsParameter(Type *type) {
    if (!type) {
        return false;
    }
    if (type->code() == camel::nn::ParameterType::typeCode()) {
        return true;
    }
    if (type->code() == TypeCode::Struct) {
        auto *structType = tt::as_ptr<StructType>(type);
        for (Type *fieldType : structType->types()) {
            if (typeContainsParameter(fieldType)) {
                return true;
            }
        }
    }
    if (type->code() == TypeCode::Tuple) {
        auto *tupleType = tt::as_ptr<TupleType>(type);
        for (Type *elemType : tupleType->types()) {
            if (typeContainsParameter(elemType)) {
                return true;
            }
        }
    }
    return false;
}

FunctionType *gradientStepType(FunctionType *lossType) {
    param_vec_t withParams;
    withParams.reserve(lossType->withTypesCount());
    for (size_t i = 0; i < lossType->withTypesCount(); ++i) {
        Type *type = lossType->withTypeAt(i);
        withParams.emplace_back(type, lossType->withIsVarAt(i) || typeContainsParameter(type));
    }

    param_vec_t normParams;
    normParams.reserve(lossType->normTypesCount() + 1);
    for (size_t i = 0; i < lossType->normTypesCount(); ++i) {
        normParams.emplace_back(lossType->normTypeAt(i), false);
    }
    normParams.emplace_back(Type::Float64(), false);
    return FunctionType::create(withParams, normParams, lossType->exitType(), Modifier::Sync);
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
        if (!isScalarLossType(lossType)) {
            return std::nullopt;
        }
        return FunctionType::create(
            {},
            {{norm[0], false}},
            gradientStepType(lossType),
            Modifier::Macro);
    }

    std::string signature() const override {
        return "(loss: <ctx...> (args...) => float) => "
               "sync <var trainable_ctx...> (args..., lr: float) => float";
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

    std::string signature() const override { return "<rule: Function> (f: Function) => Function"; }
};

gc_node_ref_t addNormPort(GraphDraft &draft, Type *type) {
    const gc_slot_idx_t slot = draft.allocateRuntimeSlot(type);
    const gc_node_ref_t node = draft.addPortNode(type, slot);
    draft.appendNormPort(node);
    return node;
}

gc_node_ref_t addStaticFloat(GraphDraft &draft, double value) {
    return draft.materializeStaticValue(toSlot<Float64>(value), Type::Float64());
}

gc_node_ref_t addOper(
    GraphDraft &draft, Type *type, std::string_view uri,
    std::initializer_list<gc_node_ref_t> normInputs) {
    const gc_node_ref_t node = draft.addOperNode(type, nullptr, uri);
    std::vector<gc_node_ref_t> inputs(normInputs.begin(), normInputs.end());
    draft.setNormInputs(node, inputs);
    return node;
}

gc_node_ref_t resolveGraphExitNode(const GCGraph *graph) {
    if (!graph) {
        return kInvalidNodeRef;
    }
    gc_node_ref_t output = graph->outputNodeRef();
    if (output == kInvalidNodeRef) {
        output = graph->returnNodeRef();
    }
    return output;
}

gc_node_ref_t resolveDraftExitNode(const GraphDraft &draft) {
    gc_node_ref_t output = draft.outputNode();
    if (output == kInvalidNodeRef) {
        output = draft.returnNode();
    }
    return output;
}

std::string_view operUri(GraphDraft &draft, gc_node_ref_t node) {
    auto payload = draft.payloadOf(node);
    if (payload.size_bytes() < sizeof(GCOperBody)) {
        return {};
    }
    const auto *body = reinterpret_cast<const GCOperBody *>(payload.data());
    return body->uri();
}

bool isNonDifferentiableOperUri(std::string_view uri) {
    // Constructors and shape/index helpers can receive cotangents through
    // larger differentiable expressions, but they do not represent trainable
    // paths and should stop gradients rather than report missing VJPs.
    return uri == "tensor:zeros" || uri == "tensor:ones" || uri == "tensor:full" ||
           uri == "tensor:shape" || uri == ":op/idx_arr" || uri == ":op/ltod";
}

std::string nodeKindName(GCNodeKind kind) {
    switch (kind) {
    case GCNodeKind::Data:
        return "DATA";
    case GCNodeKind::Port:
        return "PORT";
    case GCNodeKind::Cast:
        return "CAST";
    case GCNodeKind::Copy:
        return "COPY";
    case GCNodeKind::Fill:
        return "FILL";
    case GCNodeKind::Accs:
        return "ACCS";
    case GCNodeKind::Brch:
        return "BRCH";
    case GCNodeKind::Join:
        return "JOIN";
    case GCNodeKind::Call:
        return "CALL";
    case GCNodeKind::Bind:
        return "BIND";
    case GCNodeKind::Func:
        return "FUNC";
    case GCNodeKind::Oper:
        return "OPER";
    case GCNodeKind::Sync:
        return "SYNC";
    case GCNodeKind::Gate:
        return "GATE";
    case GCNodeKind::Dref:
        return "DREF";
    }
    return "UNKNOWN";
}

void collectReachableForwardTape(
    GraphDraft &draft, gc_node_ref_t node, std::unordered_set<gc_node_ref_t> &seen,
    std::vector<gc_node_ref_t> &tape) {
    if (node == kInvalidNodeRef || !draft.alive(node) || !seen.insert(node).second) {
        return;
    }
    for (gc_node_ref_t input : draft.normInputsOf(node)) {
        collectReachableForwardTape(draft, input, seen, tape);
    }
    for (gc_node_ref_t input : draft.withInputsOf(node)) {
        collectReachableForwardTape(draft, input, seen, tape);
    }
    tape.push_back(node);
}

std::vector<gc_node_ref_t> collectReachableForwardTape(GraphDraft &draft, gc_node_ref_t output) {
    std::unordered_set<gc_node_ref_t> seen;
    std::vector<gc_node_ref_t> tape;
    collectReachableForwardTape(draft, output, seen, tape);
    return tape;
}

void collectReachableForwardTape(
    const GCGraph &graph, gc_node_ref_t node, std::unordered_set<gc_node_ref_t> &seen,
    std::vector<gc_node_ref_t> &tape) {
    if (node == kInvalidNodeRef || graph.node(node) == nullptr || !seen.insert(node).second) {
        return;
    }
    for (gc_node_ref_t input : graph.normInputsOf(node)) {
        collectReachableForwardTape(graph, input, seen, tape);
    }
    for (gc_node_ref_t input : graph.withInputsOf(node)) {
        collectReachableForwardTape(graph, input, seen, tape);
    }
    tape.push_back(node);
}

std::vector<gc_node_ref_t> collectReachableForwardTape(const GCGraph &graph, gc_node_ref_t output) {
    std::unordered_set<gc_node_ref_t> seen;
    std::vector<gc_node_ref_t> tape;
    collectReachableForwardTape(graph, output, seen, tape);
    return tape;
}

// Validate the static helper-call closure before mutating the draft. This walks
// each graph's value-reachable tape, so dead calls do not block autograd, while
// recursive helper paths fail before the inline loop can grow the graph.
void validateStaticHelperCallGraph(
    const GCGraph *graph, std::unordered_set<const GCGraph *> &active,
    std::unordered_set<const GCGraph *> &validated) {
    if (!graph || validated.contains(graph)) {
        return;
    }
    if (!active.insert(graph).second) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::format(
                "apply_gradients does not support recursive helper call graph '{}'.",
                graph->name()));
    }

    const auto *funcType = graph->funcType();
    if (funcType) {
        if (funcType->modifiers().sync()) {
            throwRuntimeFault(
                RuntimeDiag::RuntimeError,
                std::format(
                    "apply_gradients does not support sync helper function '{}'.",
                    graph->name()));
        }
        if (funcType->modifiers().macro()) {
            throwRuntimeFault(
                RuntimeDiag::RuntimeError,
                std::format(
                    "apply_gradients does not support macro helper function '{}'.",
                    graph->name()));
        }
    }

    const gc_node_ref_t exitNode = resolveGraphExitNode(graph);
    if (exitNode == kInvalidNodeRef) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::format(
                "apply_gradients cannot lower helper graph '{}' because it has no output.",
                graph->name()));
    }

    for (gc_node_ref_t node : collectReachableForwardTape(*graph, exitNode)) {
        const GCNode *runtimeNode = graph->node(node);
        if (!runtimeNode) {
            continue;
        }
        if (runtimeNode->kind == GCNodeKind::Call) {
            throwRuntimeFault(
                RuntimeDiag::RuntimeError,
                std::format(
                    "apply_gradients does not support indirect call node {} in helper graph '{}'.",
                    node,
                    graph->name()));
        }
        if (runtimeNode->kind != GCNodeKind::Func) {
            continue;
        }

        const auto *body = graph->nodeBodyAs<GCFuncBody>(node);
        if (!body || !body->calleeGraph) {
            throwRuntimeFault(
                RuntimeDiag::RuntimeError,
                std::format(
                    "apply_gradients found an opaque helper call at node {} in graph '{}'.",
                    node,
                    graph->name()));
        }
        validateStaticHelperCallGraph(body->calleeGraph, active, validated);
    }

    active.erase(graph);
    validated.insert(graph);
}

void validateStaticHelperCallGraph(const GCGraph *graph) {
    std::unordered_set<const GCGraph *> active;
    std::unordered_set<const GCGraph *> validated;
    validateStaticHelperCallGraph(graph, active, validated);
}

// Lower only the trainable, output-reachable portion of the loss graph.
// Any helper call that survives this pass is either genuinely unsupported
// (indirect dispatch, sync/macro helper, opaque callee) or a cycle that would
// otherwise expand forever during clone-and-inline.
void lowerStaticHelperCalls(RuntimeGraphDraftSession &session, GraphDraft &draft) {
    constexpr size_t kLoweringPassBudget = 1024;
    size_t passCount                     = 0;
    while (true) {
        const gc_node_ref_t output = resolveDraftExitNode(draft);
        if (output == kInvalidNodeRef) {
            throwRuntimeFault(
                RuntimeDiag::RuntimeError,
                "apply_gradients loss graph has no output to lower helper calls from");
        }
        bool loweredThisPass                  = false;
        const std::vector<gc_node_ref_t> tape = collectReachableForwardTape(draft, output);
        for (gc_node_ref_t node : tape) {
            const DraftNodeHeader *header = draft.header(node);
            if (!header) {
                continue;
            }
            if (header->kind == GCNodeKind::Call) {
                throwRuntimeFault(
                    RuntimeDiag::RuntimeError,
                    std::format(
                        "apply_gradients cannot lower indirect call node {} in the trainable path.",
                        node));
            }
            if (header->kind != GCNodeKind::Func) {
                continue;
            }

            const auto payload = draft.payloadOf(node);
            if (payload.size_bytes() < sizeof(GCFuncBody)) {
                throwRuntimeFault(
                    RuntimeDiag::RuntimeError,
                    std::format(
                        "apply_gradients found an opaque helper call at node {} in the trainable "
                        "path.",
                        node));
            }
            const auto *body = reinterpret_cast<const GCFuncBody *>(payload.data());
            if (!body || !body->calleeGraph) {
                throwRuntimeFault(
                    RuntimeDiag::RuntimeError,
                    std::format(
                        "apply_gradients found an opaque helper call at node {} in the trainable "
                        "path.",
                        node));
            }
            const std::string calleeName = body->calleeGraph->name();

            const DraftInlineResult inlineResult =
                camel::runtime::inlineCallableInDraft(session, draft, node);
            if (!inlineResult) {
                throwRuntimeFault(
                    RuntimeDiag::RuntimeError,
                    std::format(
                        "apply_gradients failed to lower helper call '{}' at node {}.",
                        calleeName,
                        node));
            }
            loweredThisPass = true;
            break;
        }

        if (!loweredThisPass) {
            return;
        }
        if (++passCount > kLoweringPassBudget) {
            throwRuntimeFault(
                RuntimeDiag::RuntimeError,
                "apply_gradients helper-call lowering did not converge; recursive helper call is "
                "likely.");
        }
    }
}

void propagateSingleInputGradient(
    camel::nn::VjpBuildContext &vjp, GraphDraft &draft, gc_node_ref_t node) {
    auto gradient = vjp.gradientOf(node);
    if (!gradient) {
        return;
    }
    const auto inputs = draft.normInputsOf(node);
    if (!inputs.empty()) {
        vjp.accumulateGradient(inputs.front(), *gradient);
    }
}

void applyNodeVjp(camel::nn::VjpBuildContext &vjp, GraphDraft &draft, gc_node_ref_t node) {
    if (!vjp.gradientOf(node)) {
        return;
    }
    const DraftNodeHeader *header = draft.header(node);
    if (!header) {
        return;
    }

    switch (header->kind) {
    case GCNodeKind::Oper: {
        const std::string key(operUri(draft, node));
        if (isNonDifferentiableOperUri(key)) {
            return;
        }
        camel::nn::applyVjpRule(vjp, key, draft.normInputsOf(node), node);
        return;
    }
    case GCNodeKind::Cast:
    case GCNodeKind::Copy:
    case GCNodeKind::Gate:
        propagateSingleInputGradient(vjp, draft, node);
        return;
    case GCNodeKind::Func:
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::format(
                "apply_gradients encountered an unsupported helper call at node {} after lowering.",
                node));
    case GCNodeKind::Call:
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::format(
                "apply_gradients cannot differentiate through indirect call node {}.",
                node));
    case GCNodeKind::Data:
    case GCNodeKind::Port:
    case GCNodeKind::Accs:
        return;
    default:
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::format(
                "apply_gradients cannot differentiate through {} nodes yet",
                nodeKindName(header->kind)));
    }
}

gc_node_ref_t appendOptimizerUpdates(
    GraphDraft &draft, std::span<const camel::nn::ParameterGradient> gradients, gc_node_ref_t lr) {
    gc_node_ref_t updateTail = kInvalidNodeRef;
    for (const auto &paramGrad : gradients) {
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
    return updateTail;
}

GCGraph *buildAutogradSgdStepGraph(Context &context, GCGraph *lossGraph) {
    static std::atomic<size_t> nextId{0};
    const size_t id = nextId.fetch_add(1, std::memory_order_relaxed);

    validateStaticHelperCallGraph(lossGraph);

    RuntimeGraphDraftSession session(context.shared_from_this(), lossGraph);
    GraphDraft &draft = session.rootDraft();
    auto *lossType    = lossGraph->funcType();
    draft.setFuncType(gradientStepType(lossType));
    const gc_node_ref_t lr = addNormPort(draft, Type::Float64());

    lowerStaticHelperCalls(session, draft);

    gc_node_ref_t loss = resolveDraftExitNode(draft);
    if (loss == kInvalidNodeRef) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "apply_gradients loss graph has no output");
    }

    const std::vector<gc_node_ref_t> tape = collectReachableForwardTape(draft, loss);

    camel::nn::VjpBuildContext vjp(draft);
    vjp.seedGradient(loss, addStaticFloat(draft, 1.0));
    for (auto it = tape.rbegin(); it != tape.rend(); ++it) {
        applyNodeVjp(vjp, draft, *it);
    }

    const std::vector<camel::nn::ParameterGradient> parameterGradients = vjp.parameterGradients();
    const gc_node_ref_t updateTail = appendOptimizerUpdates(draft, parameterGradients, lr);
    if (updateTail == kInvalidNodeRef) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "apply_gradients found no trainable Parameter");
    }

    const auto *lossHeader = draft.header(loss);
    ASSERT(lossHeader != nullptr, "Loss node header is missing.");
    const gc_node_ref_t gatedLoss = draft.addGateNode(lossHeader->dataType, lossHeader->dataIndex);
    draft.setNormInputs(gatedLoss, std::span<const gc_node_ref_t>(&loss, 1));
    draft.setCtrlInputs(gatedLoss, std::span<const gc_node_ref_t>(&updateTail, 1));

    draft.setOutputNode(gatedLoss);
    draft.setExitNode(gatedLoss);
    draft.setReturnNode(gatedLoss, GCReturnKind::Self);

    const std::string suffix = std::format("${}", id);
    return draft.encode(
        "nn.apply_gradients.autograd_sgd" + suffix,
        "nn.apply_gradients.autograd_sgd" + suffix,
        "nn::autograd_sgd_step" + suffix);
}

} // namespace

std::unordered_map<std::string, operator_t> getNnOpsMap() {
    return {
        {"apply_gradients", __nn_apply_gradients__},
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
            {{"nn:apply_gradients", std::make_shared<ApplyGradientsResolver>()}}),
        OperatorGroup::create("vjp", {{"nn:vjp", std::make_shared<VjpDecoratorResolver>()}}),
        OperatorGroup::create(
            "parameter",
            {{"nn:parameter",
              StaticFuncTypeResolver::create({}, {{tensorType(), false}}, parameterType())}}),
        OperatorGroup::create(
            "value",
            {{"nn:value",
              StaticFuncTypeResolver::create({}, {{parameterType(), false}}, tensorType())}}),
        OperatorGroup::create(
            "grad",
            {{"nn:grad",
              StaticFuncTypeResolver::create({}, {{parameterType(), false}}, tensorType())}}),
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
    auto *loss = norm.get<Function *>(0);
    if (!loss || !loss->graph()) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "apply_gradients requires a static loss graph");
    }
    auto *lossType = loss->graph()->funcType();
    if (!isScalarLossType(lossType)) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "apply_gradients expects a static non-sync scalar loss function");
    }

    camel::nn::ensureBuiltinVjpRulesRegistered();
    GCGraph *stepGraph = buildAutogradSgdStepGraph(ctx, loss->graph());
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
