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

#include "../tensor/dtype.h"
#include "../tensor/interop.h"
#include "../tensor/ops/registry.h"
#include "../tensor/tensor.h"
#include "emitter.h"
#include "lowering.h"

#include "camel/core/mm/root_handle.h"
#include "camel/core/source/manager.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/executor.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/runtime/draft_inline.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "execute/macro/macro.h"
#include "passes/opt/generic/generic.h"
#include "passes/opt/inline/config.h"
#include "passes/trans/dot/graphviz.h"

#include <cstdlib>
#include <format>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

namespace camel::onnx {

using camel::runtime::gc_node_ref_t;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using tensor::ops::TensorFacts;
using type::TypeCode;

namespace {

/// CAMEL_ONNX_TRACE=1 prints every lowered call with the facts of its arguments to stderr.
bool traceEnabled() {
    static const bool enabled = [] {
        const char *v = std::getenv("CAMEL_ONNX_TRACE");
        return v && *v && std::string_view(v) != "0";
    }();
    return enabled;
}

/// CAMEL_ONNX_DUMP=1 prints the simplified graph that is translated (as std::rgir does).
bool dumpEnabled() {
    static const bool enabled = [] {
        const char *v = std::getenv("CAMEL_ONNX_DUMP");
        return v && *v && std::string_view(v) != "0";
    }();
    return enabled;
}

std::string describe(const Value &v) {
    const TensorFacts f = factsOf(v);
    std::string shape   = "?";
    if (f.shape) {
        shape = "[";
        for (size_t i = 0; i < f.shape->size(); ++i) {
            const int64_t d = (*f.shape)[i];
            shape +=
                (i ? "," : "") + (d == tensor::kUnknownDim ? std::string("?") : std::to_string(d));
        }
        shape += "]";
    }
    const char *kind = v.isConstant() ? "const" : "sym";
    return std::format(
        "{} {}{}",
        kind,
        v.ty && v.isConstant() ? v.ty->toString() : "tensor",
        shape);
}

bool isIntArrayType(type::Type *t) {
    if (!t || t->code() != TypeCode::Array) {
        return false;
    }
    type::Type *elem = static_cast<type::ArrayType *>(t)->elemType();
    return elem && (elem->code() == TypeCode::Int32 || elem->code() == TypeCode::Int64);
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

/// The elements of the constant tuple or struct `slot` of type `ty`.
std::vector<Value> elementsOf(slot_t slot, type::Type *ty) {
    std::vector<Value> fields;
    if (ty->code() == TypeCode::Tuple) {
        auto *tupleType = static_cast<type::TupleType *>(ty);
        auto *tuple     = rtdata::fromSlot<::Tuple *>(slot);
        for (size_t i = 0; i < tupleType->size(); ++i) {
            fields.push_back(Value::constant(tuple->get<slot_t>(i), tupleType->typeAt(i)));
        }
    } else {
        auto *structType = static_cast<type::StructType *>(ty);
        auto *object     = rtdata::fromSlot<::Struct *>(slot);
        for (size_t i = 0; i < structType->size(); ++i) {
            Value v = Value::constant(object->get<slot_t>(i), structType->typeAt(i));
            v.label = std::string(structType->fieldName(i));
            fields.push_back(std::move(v));
        }
    }
    return fields;
}

class Evaluator {
  public:
    Evaluator(
        Emitter &emitter, const ExportOptions &options,
        std::unordered_map<slot_t, std::string> names, camel::source::source_context_ptr_t source)
        : emitter_(emitter), options_(options), names_(std::move(names)),
          source_(std::move(source)) {}

    /// Translates `graph`, with its parameters bound to `inputs`, and returns its result.
    Value translate(GCGraph *graph, std::span<const Value> inputs);

  private:
    struct Activation {
        GCGraph *graph;
        std::unordered_map<gc_node_ref_t, Value> ports;
        // Memo layers: the base layer plus one per enclosing If arm being emitted. A value first
        // computed inside an arm lives in that arm's subgraph and is only visible there.
        std::vector<std::unordered_map<gc_node_ref_t, Value>> memo;
    };

    Value eval(Activation &act, gc_node_ref_t ref);
    Value evalNode(Activation &act, gc_node_ref_t ref);
    Value evalOper(Activation &act, gc_node_ref_t ref);
    Value evalJoin(Activation &act, gc_node_ref_t ref);
    Value evalFill(Activation &act, gc_node_ref_t ref);
    Value symbolicIntArray(
        Activation &act, gc_node_ref_t ref, const Value &source, std::span<const Value> values);
    Value
    evalSymbolicBranch(Activation &act, const Value &cond, std::span<const gc_node_ref_t> arms);
    Value evalAccs(Activation &act, gc_node_ref_t ref);

    std::vector<Value> evalAll(Activation &act, std::span<const gc_node_ref_t> refs);
    Value keep(slot_t slot, type::Type *type);
    [[noreturn]] void unsupported(Activation &act, std::string_view what);
    /// "file:line:col" of a source origin, or empty.
    std::string locationOf(uint64_t origin) const;

    Emitter &emitter_;
    const ExportOptions &options_;
    // Source names of constant objects (the fields of captured structs), to name initializers.
    std::unordered_map<slot_t, std::string> names_;
    camel::source::source_context_ptr_t source_;
};

Value Evaluator::translate(GCGraph *graph, std::span<const Value> inputs) {
    Activation act{.graph = graph, .ports = {}, .memo = {{}}};
    for (size_t i = 0; i < inputs.size(); ++i) {
        act.ports.emplace(graph->normPorts()[i], inputs[i]);
    }
    const auto *exitType = graph->funcType()->exitType();
    if (graph->returnKind() == camel::runtime::GCReturnKind::None ||
        (exitType && exitType->code() == TypeCode::Void)) {
        return Value::constant(NullSlot, type::Type::Void());
    }
    return eval(act, execute::resolveRuntimeTailValueRef(graph));
}

Value Evaluator::eval(Activation &act, gc_node_ref_t ref) {
    for (auto it = act.memo.rbegin(); it != act.memo.rend(); ++it) {
        if (auto found = it->find(ref); found != it->end()) {
            return found->second;
        }
    }
    Value v;
    try {
        v = evalNode(act, ref);
    } catch (const ExportError &e) {
        // The innermost node with a known source position locates the failure.
        if (!e.location().empty()) {
            throw;
        }
        std::string location = locationOf(act.graph->nodeOrigin(ref));
        if (location.empty()) {
            throw;
        }
        throw ExportError(e.what(), std::move(location));
    }
    act.memo.back().emplace(ref, v);
    return v;
}

std::string Evaluator::locationOf(uint64_t origin) const {
    if (!source_ || origin == camel::source::kInvalidOriginId) {
        return {};
    }
    const auto *file = source_->fileForOrigin(origin);
    if (!file) {
        return {};
    }
    const auto range = source_->resolveOrigin(origin);
    return std::format("{}:{}:{}", file->path, range.start.line + 1, range.start.character + 1);
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

/// A graph or function name worth showing: compiler-generated ones (lambdas, branch arms,
/// specializations of them) mean nothing in the source; the source position says more.
bool isSourceName(std::string_view name) { return !name.empty() && !name.starts_with("__"); }

void Evaluator::unsupported(Activation &act, std::string_view what) {
    const std::string &graph = act.graph->name();
    throw ExportError(
        isSourceName(graph) ? std::format("in '{}': {}", graph, what) : std::string(what));
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
        Value v           = Value::constant(slot, ty ? ty : n->dataType);
        if (auto it = names_.find(slot); it != names_.end()) {
            v.label = it->second;
        }
        return v;
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
        if (tensor::asTensorType(n->dataType)) {
            return src;
        }
        if (src.form == Value::Form::Scalar &&
            tensor::isSupportedTensorScalar(n->dataType->code())) {
            const TypeCode target = tensor::normalizeTensorDType(n->dataType->code());
            return Value::symbolicScalar(
                emitter_.node(
                    "Cast",
                    {src.name},
                    {Attribute::makeInt("to", static_cast<int64_t>(elemTypeOf(target)))}),
                n->dataType);
        }
        unsupported(
            act,
            std::format("cast of a model-dependent value to '{}'", n->dataType->toString()));
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
        const std::string &callee = g->directCalleeGraphOf(ref)->name();
        unsupported(
            act,
            std::format(
                "a call{} remains after simplification; recursion whose depth depends on the "
                "model input cannot be exported",
                isSourceName(callee) ? std::format(" to '{}'", callee.substr(0, callee.find('$')))
                                     : std::string()));
    }
    case GCNodeKind::Call:
        unsupported(
            act,
            "an indirect call remains after simplification; its callee must be known at export "
            "time");
    default:
        unsupported(
            act,
            std::format("graph node kind {} has no export semantics", static_cast<int>(n->kind)));
    }
}

Value Evaluator::evalOper(Activation &act, gc_node_ref_t ref) {
    GCGraph *g       = act.graph;
    const auto *body = g->nodeBodyAs<camel::runtime::GCOperBody>(ref);
    const std::string uri(body->uri());
    std::vector<Value> norm = evalAll(act, g->normInputsOf(ref));
    std::vector<Value> with = evalAll(act, g->withInputsOf(ref));
    const auto *def         = tensor::ops::OpRegistry::instance().find(uri);

    // Simplification has evaluated every pure operator whose arguments are constants; what is
    // left is translated. Side effects have no ONNX counterpart.
    if (!core::OperatorTraitsRegistry::instance().isPure(uri)) {
        unsupported(act, std::format("impure operator '{}' cannot be exported", uri));
    }
    const Lowering *lowering = LoweringRegistry::instance().find(uri, emitter_.opset());
    if (!lowering || !with.empty()) {
        unsupported(
            act,
            std::format(
                "operator '{}' has no ONNX lowering at opset {}{}",
                uri,
                emitter_.opset(),
                allConstant(norm) && allConstant(with)
                    ? " and was not folded"
                    : " but receives a value that depends on the model input"));
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
    if (traceEnabled()) {
        std::string args;
        for (const Value &v : norm) {
            args += (args.empty() ? "" : ", ") + describe(v);
        }
        std::cerr << std::format("[onnx] {} {}({})\n", act.graph->name(), uri, args);
    }
    try {
        Value result = lowering->lower(
            LowerContext(emitter_, uri, norm, std::move(facts), g->node(ref)->dataType));
        if (traceEnabled()) {
            std::cerr << std::format("[onnx]   -> {}\n", describe(result));
        }
        return result;
    } catch (const ExportError &e) {
        // Lowerings do not know where they are called from; name the function.
        unsupported(act, e.what());
    }
}

Value Evaluator::evalJoin(Activation &act, gc_node_ref_t join) {
    GCGraph *g               = act.graph;
    const gc_node_ref_t brch = g->normInputsOf(join).front();
    const auto arms          = g->withInputsOf(join);
    const auto caseRefs      = g->withInputsOf(brch);
    const Value cond         = eval(act, g->normInputsOf(brch).front());
    if (!cond.isConstant()) {
        if (!caseRefs.empty() || arms.size() != 2 || cond.form != Value::Form::Scalar) {
            unsupported(
                act,
                "a match on a value that depends on the model input cannot be exported");
        }
        return evalSymbolicBranch(act, cond, arms);
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

/// if-then-else on a condition that depends on the input: ONNX If with one subgraph per arm.
/// Both arms must yield values of the same form and dtype.
Value Evaluator::evalSymbolicBranch(
    Activation &act, const Value &cond, std::span<const gc_node_ref_t> arms) {
    const std::string condName = emitter_.operand(cond, TypeCode::Bool);
    Graph bodies[2];
    Value results[2];
    for (size_t i = 0; i < 2; ++i) {
        emitter_.pushScope(i == 0 ? "then_branch" : "else_branch");
        act.memo.emplace_back();
        try {
            results[i]          = eval(act, arms[i]);
            const TensorFacts f = factsOf(results[i]);
            const std::string out =
                emitter_.node("Identity", {emitter_.operand(results[i])}, {}, "arm_out");
            act.memo.pop_back();
            bodies[i] = emitter_.popScope();
            bodies[i].outputs.push_back(valueInfoOf(out, f));
        } catch (...) {
            act.memo.pop_back();
            emitter_.popScope();
            throw;
        }
    }
    const TensorFacts a = factsOf(results[0]), b = factsOf(results[1]);
    const bool sameForm =
        results[0].isConstant() || results[1].isConstant() || results[0].form == results[1].form;
    if (!a.dtype || a.dtype != b.dtype || !sameForm) {
        unsupported(
            act,
            "the arms of a branch on a model-dependent condition yield different types");
    }
    // Extents that agree survive; others become dynamic.
    std::optional<tensor::StaticShape> shape;
    if (a.shape && b.shape && a.shape->size() == b.shape->size()) {
        shape = a.shape;
        for (size_t d = 0; d < shape->size(); ++d) {
            if ((*a.shape)[d] != (*b.shape)[d]) {
                (*shape)[d] = tensor::kUnknownDim;
            }
        }
    }
    const std::string out = emitter_.node(
        "If",
        {condName},
        {Attribute::makeGraph("then_branch", std::move(bodies[0])),
         Attribute::makeGraph("else_branch", std::move(bodies[1]))});
    // The If stands for a Camel scalar when the arms do (symbolic scalars or scalar constants).
    const auto scalarTypeOf = [](const Value &v) -> type::Type * {
        if (v.isSymbolic()) {
            return v.form == Value::Form::Scalar ? v.camelType : nullptr;
        }
        return tensor::asTensorType(v.ty) ? nullptr : v.ty;
    };
    for (const Value &r : results) {
        if (r.isSymbolic() && r.form == Value::Form::IntArray) {
            unsupported(act, "a branch on a model-dependent condition cannot yield an int array");
        }
    }
    if (type::Type *scalar = scalarTypeOf(results[0])) {
        return Value::symbolicScalar(out, scalar);
    }
    return Value::symbolic(out, a.dtype, shape);
}

Value Evaluator::evalFill(Activation &act, gc_node_ref_t ref) {
    GCGraph *g                = act.graph;
    const Value source        = eval(act, g->normInputsOf(ref).front());
    std::vector<Value> values = evalAll(act, g->withInputsOf(ref));
    type::Type *targetType    = g->node(ref)->dataType;
    if (source.isConstant() && !allConstant(values) && isIntArrayType(targetType)) {
        return symbolicIntArray(act, ref, source, values);
    }
    if (source.isConstant() && !allConstant(values) &&
        (targetType->code() == TypeCode::Tuple || targetType->code() == TypeCode::Struct)) {
        // A tuple or struct of input-dependent values (e.g. gradients) exists only at export
        // time; its elements are read back by accesses or become graph outputs.
        std::vector<Value> fields = elementsOf(source.slot, targetType);
        const auto slots = g->nodeBodyAs<camel::runtime::GCFillBody>(ref)->slots();
        for (size_t k = 0; k < slots.size(); ++k) {
            fields[static_cast<size_t>(slots[k])] = values[k];
        }
        return Value::aggregate(targetType, std::move(fields));
    }
    if (!source.isConstant() || !allConstant(values)) {
        unsupported(
            act,
            "a value that depends on the model input is stored in an array or a closure; pass "
            "it as a function argument instead");
    }
    unsupported(act, "a tuple, struct or array of constants was not folded");
}

/// An int[] literal with elements that depend on the input (e.g. [shape(x)[0], 128]) becomes a
/// 1-D int64 tensor: constant and symbolic elements concatenated.
Value Evaluator::symbolicIntArray(
    Activation &act, gc_node_ref_t ref, const Value &source, std::span<const Value> values) {
    auto *templ          = rtdata::fromSlot<::Array *>(source.slot);
    const auto fillSlots = act.graph->nodeBodyAs<camel::runtime::GCFillBody>(ref)->slots();
    std::vector<std::optional<Value>> elems(templ->size());
    for (size_t k = 0; k < fillSlots.size(); ++k) {
        elems[static_cast<size_t>(fillSlots[k])] = values[k];
    }
    std::vector<std::string> parts;
    std::vector<int64_t> known;
    for (size_t i = 0; i < elems.size(); ++i) {
        if (!elems[i] || elems[i]->isConstant()) {
            const int64_t v = elems[i] ? tensor::scalarToInt64(elems[i]->ty->code(), elems[i]->slot)
                                       : templ->get<rtdata::Int64>(i);
            const int64_t one[] = {v};
            parts.push_back(emitter_.int64s(one));
            known.push_back(v);
            continue;
        }
        if (elems[i]->form != Value::Form::Scalar) {
            unsupported(act, "an int array element that depends on the model input must be an int");
        }
        const int64_t axis[] = {0};
        parts.push_back(emitter_.node(
            "Unsqueeze",
            {emitter_.operand(*elems[i], TypeCode::Int64), emitter_.int64s(axis)}));
        known.push_back(tensor::kUnknownDim);
    }
    return Value::symbolicIntArray(
        emitter_.node("Concat", std::move(parts), {Attribute::makeInt("axis", 0)}),
        std::move(known));
}

Value Evaluator::evalAccs(Activation &act, gc_node_ref_t ref) {
    GCGraph *g         = act.graph;
    const Value source = eval(act, g->normInputsOf(ref).front());
    const auto *body   = g->nodeBodyAs<camel::runtime::GCAccsBody>(ref);
    type::Type *result = g->node(ref)->dataType;
    if (source.isAggregate()) {
        if (body->accsKind == camel::runtime::GCAccsKind::TupleIndex) {
            return source.fields.at(body->value);
        }
        const auto field =
            static_cast<type::StructType *>(source.camelType)->findField(body->key());
        if (!field) {
            unsupported(act, std::format("no field '{}'", body->key()));
        }
        return source.fields[*field];
    }
    if (!source.isConstant()) {
        unsupported(act, "field access on a value that depends on the model input");
    }
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


/// Names the objects reachable from `slot` through struct fields and tuple elements.
void collectNames(
    slot_t slot, type::Type *ty, const std::string &name,
    std::unordered_map<slot_t, std::string> &names) {
    if (!ty || !type::isGCTraced(ty->code()) || slot == NullSlot) {
        return;
    }
    if (!name.empty()) {
        names.try_emplace(slot, name);
    }
    if (ty->code() == TypeCode::Struct) {
        auto *strct  = static_cast<type::StructType *>(ty);
        auto *object = rtdata::fromSlot<::Struct *>(slot);
        for (size_t i = 0; i < strct->size(); ++i) {
            const std::string key(strct->fieldName(i));
            collectNames(object->get<slot_t>(key, ty), strct->typeAt(i), key, names);
        }
    } else if (ty->code() == TypeCode::Tuple) {
        auto *tuple  = static_cast<type::TupleType *>(ty);
        auto *object = rtdata::fromSlot<::Tuple *>(slot);
        for (size_t i = 0; i < tuple->size() && i < object->size(); ++i) {
            collectNames(
                object->get<slot_t>(i),
                tuple->typeAt(i),
                std::format("{}_{}", name.empty() ? "t" : name, i),
                names);
        }
    }
}

/// Frees the graphs an export produced when it ends, however it ends.
struct DetachedGraphsGuard {
    core::context::Context &ctx;
    ~DetachedGraphsGuard() { ctx.releaseDetachedRuntimeGraphs(); }
};

/// Binds the exported function's parameters to graph inputs shaped like the examples.
class InputBinder {
  public:
    InputBinder(Emitter &emitter, const ExportOptions &options)
        : emitter_(emitter), options_(options) {}

    /// The input value of a parameter like `example`, and the exact type that goes with it.
    std::pair<Value, type::Type *> bind(const Example &example, const std::string &name, bool top) {
        type::Type *ty = example.type;
        if (const auto *tensorType = tensor::asTensorType(ty); tensorType || isTensor(example)) {
            const auto *t = rtdata::fromSlot<tensor::TensorObject *>(example.value);
            tensor::StaticShape shape(t->shapeSpan().begin(), t->shapeSpan().end());
            std::vector<size_t> dynamic;
            if (top) {
                // Dynamic axes apply to tensor arguments (data), not to parameter trees.
                const auto rank = static_cast<int64_t>(shape.size());
                for (int64_t axis : options_.dynamicAxes) {
                    const int64_t a = axis < 0 ? axis + rank : axis;
                    if (a < 0 || a >= rank) {
                        throw ExportError(std::format(
                            "dynamic axis {} is out of range for a rank-{} input",
                            axis,
                            rank));
                    }
                    shape[static_cast<size_t>(a)] = tensor::kUnknownDim;
                    dynamic.push_back(static_cast<size_t>(a));
                }
            }
            if (!names_.insert(name).second) {
                throw ExportError(std::format("two inputs are named '{}'", name));
            }
            const Value input   = Value::symbolic(name, t->dtype(), shape);
            ValueInfo inputInfo = valueInfoOf(name, factsOf(input));
            for (size_t a : dynamic) {
                (*inputInfo.shape)[a] =
                    Dim{a == 0 ? std::string("batch") : std::format("{}_dim{}", name, a)};
            }
            emitter_.graph().inputs.push_back(std::move(inputInfo));
            return {input, tensor::TensorType::get(t->dtype(), shape)};
        }
        if (ty->code() == TypeCode::Struct) {
            auto *structType = static_cast<type::StructType *>(ty);
            auto *object     = rtdata::fromSlot<::Struct *>(example.value);
            type::StructTypeFactory factory;
            std::vector<Value> fields;
            for (size_t i = 0; i < structType->size(); ++i) {
                const std::string key(structType->fieldName(i));
                auto [field, fieldType] = bind(
                    {object->get<slot_t>(i), structType->typeAt(i)},
                    top ? key : name + "_" + key,
                    false);
                fields.push_back(std::move(field));
                factory.add(key, fieldType);
            }
            type::Type *exact = factory.build();
            return {Value::aggregate(exact, std::move(fields)), exact};
        }
        if (ty->code() == TypeCode::Tuple) {
            auto *tupleType = static_cast<type::TupleType *>(ty);
            auto *tuple     = rtdata::fromSlot<::Tuple *>(example.value);
            std::vector<type::Type *> types;
            std::vector<Value> fields;
            for (size_t i = 0; i < tupleType->size(); ++i) {
                auto [field, fieldType] = bind(
                    {tuple->get<slot_t>(i), tupleType->typeAt(i)},
                    std::format("{}_{}", name, i),
                    false);
                fields.push_back(std::move(field));
                types.push_back(fieldType);
            }
            type::Type *exact = type::TupleType::create(std::move(types));
            return {Value::aggregate(exact, std::move(fields)), exact};
        }
        throw ExportError(std::format(
            "an exported function's arguments must be tensors or tuples/structs of tensors, not "
            "'{}'",
            ty->toString()));
    }

  private:
    static bool isTensor(const Example &example) {
        return example.type && example.type->code() == tensor::TensorType::typeCode();
    }

    Emitter &emitter_;
    const ExportOptions &options_;
    std::unordered_set<std::string> names_;
};

/// Makes the graph outputs of a result: one per tensor or scalar, named by its path in the
/// result (`output`, `output_0`, `output_1_fc1_w`, ...).
void emitOutputs(Emitter &emitter, const Value &result, const std::string &name) {
    if (result.isAggregate()) {
        const bool isStruct = result.camelType->code() == TypeCode::Struct;
        for (size_t i = 0; i < result.fields.size(); ++i) {
            const std::string key =
                isStruct
                    ? std::string(
                          static_cast<type::StructType *>(result.camelType)->fieldName(i))
                    : std::to_string(i);
            emitOutputs(emitter, result.fields[i], name + "_" + key);
        }
        return;
    }
    if (result.isConstant() && result.ty &&
        (result.ty->code() == TypeCode::Tuple || result.ty->code() == TypeCode::Struct)) {
        emitOutputs(emitter, Value::aggregate(result.ty, elementsOf(result.slot, result.ty)), name);
        return;
    }
    const TensorFacts facts = factsOf(result);
    if (!facts.dtype) {
        throw ExportError(std::format(
            "output '{}' must be a tensor or a number, not '{}'",
            name,
            result.ty ? result.ty->toString() : "a value of unknown type"));
    }
    const std::string output = emitter.node("Identity", {emitter.operand(result)}, {}, name);
    emitter.graph().outputs.push_back(valueInfoOf(output, facts));
}

} // namespace

Model exportFunction(
    core::context::Context &ctx, ::Function *fn, std::span<const Example> examples,
    const ExportOptions &options) {
    if (!fn || !fn->graph()) {
        throw ExportError("expected a function value");
    }
    GCGraph *graph = fn->graph();
    if (graph->withPorts().size() != 0 || graph->normPorts().size() != examples.size()) {
        throw ExportError(std::format(
            "'{}' takes {} arguments but {} examples were given",
            graph->name(),
            graph->withPorts().size() + graph->normPorts().size(),
            examples.size()));
    }

    Emitter emitter(options.opset);
    emitter.graph().name = options.graphName;
    InputBinder binder(emitter, options);
    std::vector<Value> inputs;
    std::vector<type::Type *> inputTypes;
    size_t tensorArgs = 0;
    for (const Example &example : examples) {
        tensorArgs += tensor::asTensorType(example.type) ? 1 : 0;
    }
    for (size_t i = 0; i < examples.size(); ++i) {
        const std::string name = tensorArgs == 1 && examples.size() == 1
                                     ? options.inputName
                                     : std::format("{}{}", options.inputName, i);
        auto [input, exactType] = binder.bind(examples[i], name, true);
        inputs.push_back(std::move(input));
        inputTypes.push_back(exactType);
    }

    // The function applied to inputs of the examples' types, with its captures bound, is a
    // program of its own: simplification specializes it for the input shapes, inlines its
    // calls, unrolls recursion of static depth and folds everything that does not depend on the
    // inputs. The running program is left as it is. Simplification allocates and may collect:
    // what is read afterwards stays rooted.
    std::optional<core::mm::RootHandle> captures;
    if (fn->tuple()) {
        captures.emplace(
            core::mm::autoSpace(),
            const_cast<::Tuple *>(fn->tuple()),
            fn->tupleType(),
            "onnx::export");
    }
    DetachedGraphsGuard guard{ctx};
    const auto context = ctx.shared_from_this();
    GCGraph *root      = nullptr;
    {
        camel::runtime::RuntimeGraphDraftSession session(context, graph);
        camel::runtime::bindFunctionCall(session.rootDraft(), fn, inputTypes);
        root = session.commit();
    }
    OptimizeRewriteConfig config;
    // Everything that is not recursive is inlined; ONNX has no calls.
    config.inlineConfig.smallSubgraphMaxNonDataPortNodes = std::numeric_limits<size_t>::max();
    std::ostringstream log;
    // Recursion unrolls about one level per round; one of dynamic depth stops changing instead.
    constexpr size_t kMaxRounds = 4096;
    // Macros (grad, value_and_grad) expand once their arguments are constants, which
    // simplification may be what makes them.
    root = MacroRewritePass(context).apply(root, log);
    while (true) {
        root                 = simplifyGraph(context, root, config, log, kMaxRounds);
        GCGraph *expanded    = MacroRewritePass(context).apply(root, log);
        if (expanded == root) {
            break;
        }
        root = expanded;
    }
    if (dumpEnabled()) {
        GraphVizDumpPass(context, GraphVizDumpConfig{.readableOnly = true}).apply(root, std::cerr);
    }

    std::unordered_map<slot_t, std::string> names;
    if (captures) {
        const auto *captured = captures->getAs<::Tuple>();
        const auto *layout   = static_cast<const type::TupleType *>(captures->type());
        for (size_t i = 0; i < captured->size(); ++i) {
            collectNames(captured->get<slot_t>(i), layout->typeAt(i), "", names);
        }
    }
    Evaluator evaluator(emitter, options, std::move(names), ctx.sourceContext());
    emitOutputs(emitter, evaluator.translate(root, inputs), options.outputName);

    Model model;
    model.graph = std::move(emitter.graph());
    model.opset = options.opset;
    return model;
}

} // namespace camel::onnx
