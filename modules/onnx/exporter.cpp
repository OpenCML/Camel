/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The partial evaluator behind onnx.export_model (see exporter.h).
 */

#include "exporter.h"

#include "../tensor/interop.h"
#include "../tensor/ops/registry.h"
#include "../tensor/tensor.h"
#include "emitter.h"
#include "lowering.h"

#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/executor.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/runtime/graph.h"

#include <format>
#include <unordered_map>

namespace camel::onnx {

using camel::runtime::gc_node_ref_t;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using tensor::ops::TensorFacts;
using type::TypeCode;

namespace {

/// Operator arguments backed by plain arrays (no VM frame).
class ExportArgsView final : public ArgsView {
  public:
    ExportArgsView(std::vector<slot_t> slots, std::vector<type::Type *> types)
        : slots_(std::move(slots)), types_(std::move(types)) {}

    size_t size() const override { return slots_.size(); }
    slot_t slot(size_t index) const override { return slots_[index]; }
    void setSlot(size_t index, slot_t value) override { slots_[index] = value; }
    TypeCode code(size_t index) const override { return types_[index]->code(); }
    type::Type *type(size_t index) const override { return types_[index]; }

  private:
    std::vector<slot_t> slots_;
    std::vector<type::Type *> types_;
};

ExportArgsView constantArgs(std::span<const Value> values) {
    std::vector<slot_t> slots;
    std::vector<type::Type *> types;
    for (const Value &v : values) {
        slots.push_back(v.slot);
        types.push_back(v.ty);
    }
    return ExportArgsView(std::move(slots), std::move(types));
}

bool allConstant(std::span<const Value> values) {
    return std::ranges::all_of(values, [](const Value &v) { return v.isConstant(); });
}

ValueInfo valueInfoOf(const std::string &name, const TensorFacts &facts) {
    if (!facts.dtype) {
        throw ExportError(std::format("the dtype of '{}' is not known statically", name));
    }
    ValueInfo info{.name = name, .type = elemTypeOf(*facts.dtype), .shape = std::nullopt};
    if (facts.shape) {
        std::vector<Dim> dims;
        for (size_t i = 0; i < facts.shape->size(); ++i) {
            const int64_t d = (*facts.shape)[i];
            dims.push_back(d == tensor::kUnknownDim ? Dim{std::format("{}_d{}", name, i)} : Dim{d});
        }
        info.shape = std::move(dims);
    }
    return info;
}

class Evaluator {
  public:
    Evaluator(core::context::Context &ctx, Emitter &emitter, const ExportOptions &options)
        : ctx_(ctx), emitter_(emitter), options_(options) {}

    /// Evaluates `graph` with its ports bound to the given values and returns its result.
    Value call(
        GCGraph *graph, std::span<const Value> with, std::span<const Value> norm,
        std::span<const Value> closure);

  private:
    struct Activation {
        GCGraph *graph;
        std::unordered_map<gc_node_ref_t, Value> ports;
        std::unordered_map<gc_node_ref_t, Value> memo;
    };

    Value eval(Activation &act, gc_node_ref_t ref);
    Value evalNode(Activation &act, gc_node_ref_t ref);
    Value evalOper(Activation &act, gc_node_ref_t ref);
    Value evalJoin(Activation &act, gc_node_ref_t ref);
    Value evalFill(Activation &act, gc_node_ref_t ref);
    Value evalAccs(Activation &act, gc_node_ref_t ref);
    Value
    callFunctionValue(const Value &fn, std::span<const Value> with, std::span<const Value> norm);

    std::vector<Value> evalAll(Activation &act, std::span<const gc_node_ref_t> refs);
    Value keep(slot_t slot, type::Type *type);
    [[noreturn]] void unsupported(Activation &act, std::string_view what);

    core::context::Context &ctx_;
    Emitter &emitter_;
    const ExportOptions &options_;
    size_t depth_ = 0;
};

Value Evaluator::call(
    GCGraph *graph, std::span<const Value> with, std::span<const Value> norm,
    std::span<const Value> closure) {
    if (++depth_ > options_.maxCallDepth) {
        throw ExportError(std::format(
            "call depth exceeds {} while inlining '{}'; recursion whose termination depends on "
            "the model input cannot be exported",
            options_.maxCallDepth,
            graph->name()));
    }
    const auto withPorts    = graph->withPorts();
    const auto normPorts    = graph->normPorts();
    const auto closureNodes = graph->closureNodes();
    if (with.size() != withPorts.size() || norm.size() != normPorts.size() ||
        closure.size() != closureNodes.size()) {
        throw ExportError(std::format("argument count mismatch calling '{}'", graph->name()));
    }

    Activation act{.graph = graph, .ports = {}, .memo = {}};
    for (size_t i = 0; i < with.size(); ++i) {
        act.ports.emplace(withPorts[i], with[i]);
    }
    for (size_t i = 0; i < norm.size(); ++i) {
        act.ports.emplace(normPorts[i], norm[i]);
    }
    for (size_t i = 0; i < closure.size(); ++i) {
        act.ports.emplace(closureNodes[i], closure[i]);
    }

    Value result;
    const auto *exitType = graph->funcType()->exitType();
    if (graph->returnKind() == camel::runtime::GCReturnKind::None ||
        (exitType && exitType->code() == TypeCode::Void)) {
        result = Value::constant(NullSlot, type::Type::Void());
    } else {
        result = eval(act, execute::resolveRuntimeTailValueRef(graph));
    }
    --depth_;
    return result;
}

Value Evaluator::eval(Activation &act, gc_node_ref_t ref) {
    if (auto found = act.memo.find(ref); found != act.memo.end()) {
        return found->second;
    }
    Value v = evalNode(act, ref);
    act.memo.emplace(ref, v);
    return v;
}

std::vector<Value> Evaluator::evalAll(Activation &act, std::span<const gc_node_ref_t> refs) {
    std::vector<Value> values;
    values.reserve(refs.size());
    for (gc_node_ref_t ref : refs) {
        values.push_back(eval(act, ref));
    }
    return values;
}

Value Evaluator::keep(slot_t slot, type::Type *type) {
    if (type && type::isGCTraced(type->code()) && slot != NullSlot) {
        emitter_.retain(rtdata::fromSlot<rtdata::Object *>(slot), type);
    }
    return Value::constant(slot, type);
}

void Evaluator::unsupported(Activation &act, std::string_view what) {
    throw ExportError(std::format("in '{}': {}", act.graph->name(), what));
}

Value Evaluator::evalNode(Activation &act, gc_node_ref_t ref) {
    GCGraph *g    = act.graph;
    const auto *n = g->node(ref);
    switch (n->kind) {
    case GCNodeKind::Data: {
        const auto index = n->dataIndex;
        if (index >= 0) {
            unsupported(act, "DATA node without a static value");
        }
        const slot_t slot = g->staticArea()->get<slot_t>(static_cast<size_t>(-index));
        type::Type *ty    = g->staticDataType()->typeAt(static_cast<size_t>(-index));
        return Value::constant(slot, ty ? ty : n->dataType);
    }
    case GCNodeKind::Port: {
        auto it = act.ports.find(ref);
        if (it == act.ports.end()) {
            unsupported(act, "unbound parameter");
        }
        return it->second;
    }
    case GCNodeKind::Cast: {
        Value src = eval(act, g->normInputsOf(ref).front());
        if (src.isConstant()) {
            return keep(n->dataType->castSlotFrom(src.slot, src.ty), n->dataType);
        }
        if (!tensor::asTensorType(n->dataType)) {
            unsupported(
                act,
                std::format("cast of a model-dependent tensor to '{}'", n->dataType->toString()));
        }
        return src;
    }
    case GCNodeKind::Copy:
        // Exported values are never mutated in place, so a copy can share its source.
        return eval(act, g->normInputsOf(ref).front());
    case GCNodeKind::Gate: {
        const auto norm = g->normInputsOf(ref);
        return eval(act, norm.empty() ? g->withInputsOf(ref).back() : norm.back());
    }
    case GCNodeKind::Fill:
        return evalFill(act, ref);
    case GCNodeKind::Accs:
        return evalAccs(act, ref);
    case GCNodeKind::Oper:
        return evalOper(act, ref);
    case GCNodeKind::Join:
        return evalJoin(act, ref);
    case GCNodeKind::Func: {
        auto with = evalAll(act, g->withInputsOf(ref));
        auto norm = evalAll(act, g->normInputsOf(ref));
        return call(g->directCalleeGraphOf(ref), with, norm, {});
    }
    case GCNodeKind::Call: {
        const auto withRefs = g->withInputsOf(ref);
        Value fn            = eval(act, withRefs.front());
        auto with           = evalAll(act, withRefs.subspan(1));
        auto norm           = evalAll(act, g->normInputsOf(ref));
        return callFunctionValue(fn, with, norm);
    }
    default:
        unsupported(
            act,
            std::format("graph node kind {} has no export semantics", static_cast<int>(n->kind)));
    }
}

Value Evaluator::callFunctionValue(
    const Value &fn, std::span<const Value> with, std::span<const Value> norm) {
    if (!fn.isConstant() || !fn.ty || fn.ty->code() != TypeCode::Function) {
        throw ExportError("the callee of an indirect call must be known at export time");
    }
    auto *func     = rtdata::fromSlot<::Function *>(fn.slot);
    GCGraph *graph = func->graph();
    std::vector<Value> closure;
    if (const ::Tuple *captured = func->tuple()) {
        const auto *layout = func->tupleType();
        for (size_t i = 0; i < captured->size(); ++i) {
            closure.push_back(Value::constant(captured->get<slot_t>(i), layout->typeAt(i)));
        }
    }
    return call(graph, with, norm, closure);
}

Value Evaluator::evalOper(Activation &act, gc_node_ref_t ref) {
    GCGraph *g       = act.graph;
    const auto *body = g->nodeBodyAs<camel::runtime::GCOperBody>(ref);
    const std::string uri(body->uri());
    std::vector<Value> norm = evalAll(act, g->normInputsOf(ref));
    std::vector<Value> with = evalAll(act, g->withInputsOf(ref));
    const auto *def         = tensor::ops::OpRegistry::instance().find(uri);

    if (allConstant(norm) && allConstant(with)) {
        if (def && !def->traits.pure) {
            unsupported(act, std::format("impure operator '{}' cannot be exported", uri));
        }
        operator_t op = body->op;
        if (!op) {
            auto found = ctx_.execMgr().find(uri);
            if (!found) {
                unsupported(
                    act,
                    std::format("operator '{}' cannot be evaluated at export time", uri));
            }
            op = *found;
        }
        ExportArgsView withView = constantArgs(with);
        ExportArgsView normView = constantArgs(norm);
        const slot_t result     = (*op)(withView, normView, ctx_);
        return keep(result, g->node(ref)->dataType);
    }

    const Lowering *lowering = LoweringRegistry::instance().find(uri, emitter_.opset());
    if (!lowering || !with.empty()) {
        unsupported(
            act,
            std::format(
                "operator '{}' has no ONNX lowering at opset {} but receives a value that depends "
                "on the model input",
                uri,
                emitter_.opset()));
    }

    TensorFacts facts;
    if (def && def->infer) {
        std::vector<type::Type *> types;
        std::vector<std::optional<tensor::ops::ConstArg>> constants;
        for (const Value &v : norm) {
            types.push_back(inferenceTypeOf(v));
            constants.push_back(constArgOf(v));
        }
        try {
            if (auto inferred = def->infer(tensor::ops::InferContext(types, constants))) {
                if (const auto *t = tensor::asTensorType(*inferred)) {
                    facts = {t->dtype(), t->shape()};
                }
            }
        } catch (const tensor::ShapeError &e) {
            unsupported(act, std::format("'{}': {}", uri, e.what()));
        }
    }
    return lowering->lower(LowerContext(emitter_, uri, norm, std::move(facts)));
}

Value Evaluator::evalJoin(Activation &act, gc_node_ref_t join) {
    GCGraph *g               = act.graph;
    const gc_node_ref_t brch = g->normInputsOf(join).front();
    const auto arms          = g->withInputsOf(join);
    const auto caseRefs      = g->withInputsOf(brch);
    const Value cond         = eval(act, g->normInputsOf(brch).front());
    if (!cond.isConstant()) {
        // Lowered values are tensors while Camel conditions are scalars; a model-dependent
        // condition needs a model-dependent scalar (e.g. an element read), which has no lowering.
        unsupported(act, "a branch condition that depends on the model input cannot be exported");
    }

    // Same selection rule as the VMs: if-then-else on a bool, or the first matching case.
    size_t arm = 0;
    if (caseRefs.empty()) {
        arm = tensor::scalarToBool(cond.ty->code(), cond.slot) ? 0 : 1;
    } else {
        arm = caseRefs.size();
        for (size_t i = 0; i < caseRefs.size() && arm == caseRefs.size(); ++i) {
            const Value c = eval(act, caseRefs[i]);
            if (!c.isConstant()) {
                unsupported(act, "a match case that depends on the model input cannot be exported");
            }
            const bool equal = type::isGCTraced(cond.ty->code())
                                   ? rtdata::fromSlot<rtdata::Object *>(cond.slot)->equals(
                                         rtdata::fromSlot<rtdata::Object *>(c.slot),
                                         cond.ty,
                                         false)
                                   : cond.slot == c.slot;
            if (equal) {
                arm = i;
            }
        }
    }
    if (arm >= arms.size()) {
        unsupported(act, "branch selects a missing arm");
    }
    return eval(act, arms[arm]);
}

Value Evaluator::evalFill(Activation &act, gc_node_ref_t ref) {
    GCGraph *g                = act.graph;
    const Value source        = eval(act, g->normInputsOf(ref).front());
    std::vector<Value> values = evalAll(act, g->withInputsOf(ref));
    if (!source.isConstant() || !allConstant(values)) {
        unsupported(
            act,
            "a value that depends on the model input is stored in a tuple, struct, array or "
            "closure; pass it as a function argument instead");
    }
    type::Type *targetType = g->node(ref)->dataType;
    auto *object           = rtdata::fromSlot<rtdata::Object *>(source.slot)
                       ->clone(core::mm::autoSpace(), targetType, false);
    emitter_.retain(object, targetType);
    std::vector<slot_t> slots;
    for (const Value &v : values) {
        slots.push_back(v.slot);
    }
    execute::writeRuntimeFillSlots(
        object,
        targetType,
        g->nodeBodyAs<camel::runtime::GCFillBody>(ref),
        slots);
    return Value::constant(rtdata::toSlot(object), targetType);
}

Value Evaluator::evalAccs(Activation &act, gc_node_ref_t ref) {
    GCGraph *g         = act.graph;
    const Value source = eval(act, g->normInputsOf(ref).front());
    if (!source.isConstant()) {
        unsupported(act, "field access on a value that depends on the model input");
    }
    const auto *body   = g->nodeBodyAs<camel::runtime::GCAccsBody>(ref);
    type::Type *result = g->node(ref)->dataType;
    if (body->accsKind == camel::runtime::GCAccsKind::TupleIndex) {
        auto *tuple = rtdata::fromSlot<::Tuple *>(source.slot);
        Value v     = Value::constant(tuple->get<slot_t>(body->value), result);
        v.label     = std::format("{}_{}", source.label.empty() ? "t" : source.label, body->value);
        return v;
    }
    const std::string key(body->key());
    auto *object = rtdata::fromSlot<::Struct *>(source.slot);
    Value v      = Value::constant(object->get<slot_t>(key, source.ty), result);
    v.label      = key;
    return v;
}

} // namespace

Model exportFunction(
    core::context::Context &ctx, ::Function *fn, const tensor::TensorObject *example,
    const ExportOptions &options) {
    if (!fn || !fn->graph()) {
        throw ExportError("expected a function value");
    }
    if (!example) {
        throw ExportError("expected an example tensor");
    }
    GCGraph *graph = fn->graph();
    if (graph->withPorts().size() != 0 || graph->normPorts().size() != 1) {
        throw ExportError(std::format(
            "the exported function must take exactly one tensor argument; '{}' takes {}",
            graph->name(),
            graph->withPorts().size() + graph->normPorts().size()));
    }

    Emitter emitter(options.opset);
    const tensor::StaticShape inputShape(example->shapeSpan().begin(), example->shapeSpan().end());
    const Value input    = Value::symbolic(options.inputName, example->dtype(), inputShape);
    emitter.graph().name = options.graphName;
    emitter.graph().inputs.push_back(valueInfoOf(options.inputName, factsOf(input)));

    Evaluator evaluator(ctx, emitter, options);
    std::vector<Value> closure;
    if (const ::Tuple *captured = fn->tuple()) {
        const auto *layout = fn->tupleType();
        for (size_t i = 0; i < captured->size(); ++i) {
            closure.push_back(Value::constant(captured->get<slot_t>(i), layout->typeAt(i)));
        }
    }
    const Value inputs[] = {input};
    const Value result   = evaluator.call(graph, {}, inputs, closure);

    const TensorFacts facts = factsOf(result);
    if (!facts.dtype) {
        throw ExportError("the exported function must return a tensor");
    }
    const std::string output =
        emitter.node("Identity", {emitter.operand(result)}, {}, options.outputName);
    emitter.graph().outputs.push_back(valueInfoOf(output, facts));

    Model model;
    model.graph = std::move(emitter.graph());
    model.opset = options.opset;
    return model;
}

} // namespace camel::onnx
