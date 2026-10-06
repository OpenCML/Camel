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
 * Updated: Oct. 03, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The partial evaluator behind onnx.export_model (see exporter.h).
 */

#include "exporter.h"

#include "../nn/state.h"
#include "../tensor/dtype.h"
#include "../tensor/interop.h"
#include "../tensor/ops/registry.h"
#include "../tensor/tensor.h"
#include "capability.h"
#include "emitter.h"
#include "lowering.h"

#include "camel/core/mm/root_handle.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/source/manager.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/executor.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/runtime/draft_inline.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/passes/macro/macro.h"
#include "camel/passes/opt/generic.h"
#include "camel/passes/opt/config.h"
#include "camel/passes/trans/dot/graphviz.h"

#include <cstdlib>
#include <format>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

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

bool isFloatArrayType(type::Type *t) {
    if (!t || t->code() != TypeCode::Array) {
        return false;
    }
    type::Type *elem = static_cast<type::ArrayType *>(t)->elemType();
    return elem && (elem->code() == TypeCode::Float32 || elem->code() == TypeCode::Float64);
}

bool allConstant(std::span<const Value> values) {
    return std::ranges::all_of(values, [](const Value &v) { return v.isConstant(); });
}

/// A deferred closure element of an aggregate (see evalFill): no value, only its function type.
bool isDeferredClosure(const Value &v) {
    return v.isConstant() && v.slot == NullSlot && v.ty && v.ty->code() == TypeCode::Function;
}

/// The Camel scalar type a branch output stands for when the arm values stand for one
/// (symbolic scalars or scalar constants), mirroring the single-output branch path.
type::Type *scalarCamelTypeOf(const Value &v) {
    if (v.isSymbolic()) {
        return v.form == Value::Form::Scalar ? v.camelType : nullptr;
    }
    return tensor::asTensorType(v.ty) ? nullptr : v.ty;
}

/// Operator arguments backed by constant values, for the exporter's constant folds (the local
/// analog of the simplifier's StaticArgsView).
class FoldArgsView final : public ArgsView {
  public:
    FoldArgsView(std::vector<slot_t> slots, std::vector<type::Type *> types)
        : slots_(std::move(slots)), types_(std::move(types)) {}
    size_t size() const override { return slots_.size(); }
    slot_t slot(size_t index) const override { return slots_[index]; }
    void setSlot(size_t index, slot_t value) override { slots_[index] = value; }
    type::TypeCode code(size_t index) const override {
        return types_[index] ? types_[index]->code() : type::TypeCode::Void;
    }
    type::Type *type(size_t index) const override { return types_[index]; }

  private:
    std::vector<slot_t> slots_;
    std::vector<type::Type *> types_;
};

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
        Emitter &emitter, const ExportOptions &options, core::context::Context &ctx,
        std::unordered_map<slot_t, std::string> names, camel::source::source_context_ptr_t source)
        : emitter_(emitter), options_(options), ctx_(ctx), names_(std::move(names)),
          source_(std::move(source)) {}

    /// Translates `graph`, with its parameters bound to `inputs`, and returns its result.
    Value translate(GCGraph *graph, std::span<const Value> inputs);

    /// Refuses `graph` when an impure operator is reachable from its exit through data or
    /// control edges, in `graph` itself or in a function it calls or holds as a constant.
    /// translate() only follows data dependencies, so an operator that runs for its effect
    /// alone (a `println` statement of a sync function) would otherwise be left out of the
    /// model without a word.
    void refuseEffects(GCGraph *graph);

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
    Value evalVariableRead(Activation &act, gc_node_ref_t ref);
    Value evalJoin(Activation &act, gc_node_ref_t ref);
    Value evalFill(Activation &act, gc_node_ref_t ref);
    Value symbolicFloatArray(
        Activation &act, gc_node_ref_t ref, const Value &source, std::span<const Value> values);
    Value symbolicIntArray(
        Activation &act, gc_node_ref_t ref, const Value &source, std::span<const Value> values);
    Value
    evalSymbolicBranch(Activation &act, const Value &cond, std::span<const gc_node_ref_t> arms);
    /// A branch on a model-dependent condition whose JOIN is tuple/struct-typed: the arms
    /// yield aggregates, so the If gets one output per leaf value.
    Value evalSymbolicAggregateBranch(
        Activation &act, const std::string &condName, std::span<const gc_node_ref_t> arms);
    struct ArmLeaf {
        Value arms[2];
    };
    /// Flattens both arm results of an aggregate-typed branch leaf by leaf, checking that the
    /// structure matches. A constant tuple/struct arm reads as its elements.
    void
    collectArmLeaves(Activation &act, const Value &a, const Value &b, std::vector<ArmLeaf> &leaves);
    /// Rebuilds the branch's aggregate from the combined leaf values, in collectArmLeaves' order.
    Value rebuildArmAggregate(type::Type *camelType, std::span<const Value> combined, size_t &next);
    Value evalAccs(Activation &act, gc_node_ref_t ref);

    // ---- calls that simplification leaves: tail recursion as an ONNX Loop (see translateLoop)
    /// One function being translated as a loop: its ports and what they are bound to while
    /// its body is evaluated.
    struct LoopFrame {
        GCGraph *fn = nullptr;
        std::vector<gc_node_ref_t> ports;
        std::vector<Value> inputs;
        std::vector<bool> changed; // set when a tail call passes something else for the port
    };
    /// What evaluating the function body from one node amounts to.
    struct BodyOutcome {
        // Whether the function calls itself again: a constant, or a bool value by name.
        std::optional<bool> contConst;
        std::string contName;
        std::vector<Value> next;     // the arguments of that call; the current ones on a return
        std::optional<Value> result; // the value returned, on the paths that return
    };
    Value evalCall(Activation &act, gc_node_ref_t site);
    Value translateLoop(Activation &act, GCGraph *fn, std::vector<Value> args);
    BodyOutcome evalLoopBody(Activation &act, gc_node_ref_t ref, LoopFrame &loop);
    BodyOutcome
    mergeOutcomes(Activation &act, const Value &cond, BodyOutcome a, BodyOutcome b, LoopFrame &loop);
    Value selectValue(Activation &act, const std::string &cond, const Value &a, const Value &b);
    std::vector<Value> callArguments(Activation &act, gc_node_ref_t site, GCGraph *callee);
    Activation activationOf(GCGraph *callee, std::span<const Value> args);
    bool nodeReachesCall(const GCGraph *g, gc_node_ref_t ref, const GCGraph *target);
    std::vector<LoopFrame *> loops_;

    std::vector<Value> evalAll(Activation &act, std::span<const gc_node_ref_t> refs);
    Value keep(slot_t slot, type::Type *type);
    /// A pure operator whose arguments are all constants, evaluated as the VM would evaluate
    /// it. Simplification folds these only where replacement is execution-safe; inside a branch
    /// arm (ctrl-waiting on the BRCH) they survive, and the exported value DAG folds them here.
    std::optional<Value> foldConstantOper(
        std::string_view uri, std::span<const Value> norm, std::span<const Value> with,
        type::Type *resultType);
    [[noreturn]] void unsupported(
        Activation &act, std::string_view what, ExportError::Kind kind = ExportError::Kind::Other);
    /// "file:line:col" of a source origin, or empty.
    std::string locationOf(uint64_t origin) const;

    Emitter &emitter_;
    const ExportOptions &options_;
    core::context::Context &ctx_;
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

/// A graph or function name worth showing: compiler-generated ones (lambdas, branch arms,
/// specializations of them) mean nothing in the source; the source position says more.
bool isSourceName(std::string_view name) { return !name.empty() && !name.starts_with("__"); }

/// The operator of a node, or an empty view for any other node kind.
std::string_view operUriOf(const GCGraph *g, gc_node_ref_t ref) {
    const auto *n = g->node(ref);
    if (n == nullptr || n->kind != GCNodeKind::Oper) {
        return {};
    }
    return g->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri();
}

/// A variable declared in `g` itself (`var v = e` is the operator `:var/new`): its content
/// never leaves the function, so reads and assignments of it are not effects of the model.
bool isLocalVariable(const GCGraph *g, gc_node_ref_t cell) {
    return operUriOf(g, cell) == ":var/new";
}

/// Why a variable that is not local to the graph using it cannot be exported.
constexpr const char *kOuterVariable =
    "a variable is read or assigned outside the function that declares it (a var parameter, a "
    "captured variable, a variable used in a branch arm): its content is state that an ONNX "
    "model cannot hold; only a variable declared, read and assigned directly in one function "
    "can be exported. Pass the value as an argument and return the new value instead";

/// Whether running `from` is a precondition of running `to` (data or control edges).
bool runsBefore(const GCGraph *g, gc_node_ref_t from, gc_node_ref_t to) {
    if (from == to) {
        return false;
    }
    std::unordered_set<gc_node_ref_t> seen{from};
    std::vector<gc_node_ref_t> work{from};
    while (!work.empty()) {
        const gc_node_ref_t ref = work.back();
        work.pop_back();
        for (const auto users : {g->normOutputsOf(ref), g->withOutputsOf(ref), g->ctrlOutputsOf(ref)}) {
            for (gc_node_ref_t user : users) {
                if (user == to) {
                    return true;
                }
                if (seen.insert(user).second) {
                    work.push_back(user);
                }
            }
        }
    }
    return false;
}

void Evaluator::refuseEffects(GCGraph *root) {
    std::unordered_set<GCGraph *> seenGraphs{root};
    std::vector<GCGraph *> graphs{root};
    while (!graphs.empty()) {
        GCGraph *g = graphs.back();
        graphs.pop_back();
        const gc_node_ref_t exit = g->exitNodeRef();
        if (!g->hasNodePayload() || exit == camel::runtime::kInvalidNodeRef) {
            continue;
        }
        std::unordered_set<gc_node_ref_t> seen{exit};
        std::vector<gc_node_ref_t> work{exit};
        while (!work.empty()) {
            const gc_node_ref_t ref = work.back();
            work.pop_back();
            const auto *n       = g->node(ref);
            GCGraph *referenced = nullptr;
            if (n->kind == GCNodeKind::Oper) {
                const std::string uri(g->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri());
                // Declaring, reading and assigning a variable of the function itself is no
                // effect of the model (evalVariableRead translates the reads).
                const bool onLocalVariable =
                    uri == ":var/new" ||
                    ((uri == ":var/get" || uri == ":op/assn") && !g->normInputsOf(ref).empty() &&
                     isLocalVariable(g, g->normInputsOf(ref).front()));
                if (!onLocalVariable && !core::OperatorTraitsRegistry::instance().isPure(uri)) {
                    const uint64_t origin = g->nodeOrigin(ref);
                    const std::string what =
                        uri == ":var/get" || uri == ":op/assn"
                            ? std::string(kOuterVariable)
                            : std::format("impure operator '{}' cannot be exported", uri);
                    throw ExportError(
                        isSourceName(g->name()) ? std::format("in '{}': {}", g->name(), what)
                                                : what,
                        locationOf(origin),
                        origin,
                        ExportError::Kind::Other);
                }
            } else if (n->kind == GCNodeKind::Func) {
                referenced = g->directCalleeGraphOf(ref);
            } else if (
                n->kind == GCNodeKind::Data && n->dataIndex < 0 && n->dataType &&
                n->dataType->code() == TypeCode::Function) {
                // A function constant: the callee of an indirect call, or a closure template.
                const auto *fn = rtdata::fromSlot<::Function *>(
                    g->staticArea()->get<slot_t>(static_cast<size_t>(-n->dataIndex)));
                referenced = fn ? fn->graph() : nullptr;
            }
            if (referenced && seenGraphs.insert(referenced).second) {
                graphs.push_back(referenced);
            }
            for (const auto inputs :
                 {g->normInputsOf(ref), g->withInputsOf(ref), g->ctrlInputsOf(ref)}) {
                for (gc_node_ref_t input : inputs) {
                    if (seen.insert(input).second) {
                        work.push_back(input);
                    }
                }
            }
        }
    }
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
        const uint64_t origin = act.graph->nodeOrigin(ref);
        std::string location  = locationOf(origin);
        if (location.empty()) {
            throw;
        }
        throw ExportError(e.what(), std::move(location), origin, e.kind());
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

void Evaluator::unsupported(Activation &act, std::string_view what, ExportError::Kind kind) {
    // The source name: a specialization's suffix ("$tspec0") means nothing to the reader.
    const std::string graph = act.graph->name().substr(0, act.graph->name().find('$'));
    throw ExportError(
        isSourceName(graph) ? std::format("in '{}': {}", graph, what) : std::string(what),
        kind);
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
    case GCNodeKind::Func:
        return evalCall(act, ref);
    case GCNodeKind::Call:
        unsupported(
            act,
            "an indirect call remains after simplification; its callee must be known at export "
            "time",
            ExportError::Kind::CallRemains);
    default:
        unsupported(
            act,
            std::format("graph node kind {} has no export semantics", static_cast<int>(n->kind)));
    }
}

// ---------------------------------------------------------------- tail recursion as a Loop
//
// Simplification inlines every call that is not recursive and unrolls a recursion whose depth
// is known at export time. A call that is left is a recursion whose depth depends on the model
// input. When every recursive call is the last thing its path does (tail position), the
// function is a loop over its arguments:
//
//     state = arguments
//     do { (again, state) = one evaluation of the body at state } while (again)
//     result = the value the body returns at the final state
//
// and that is an ONNX Loop whose carried values are the arguments that change. The body is the
// function's own graph evaluated once with the carried arguments as inputs. Its decision tree
// (the branches that separate "return" from "call again") is merged with Where instead of If:
// both sides are evaluated and the condition selects, which is the same value because every
// exported operator is pure and total. Branches without a recursive call stay Ifs. The result is
// computed once more after the Loop, at the final state, so no carried value needs an initial
// result of a known shape.
//
// Refused, with a diagnostic: a recursive call that is not in tail position, a carried value
// whose dtype or shape changes between iterations, a tuple, struct or array argument that
// changes, a recursion that never returns.

namespace {

/// Whether a direct call chain leads from `from` to `to` (`from` itself included).
bool callReaches(const GCGraph *from, const GCGraph *to) {
    std::unordered_set<const GCGraph *> seen;
    std::vector<const GCGraph *> work{from};
    while (!work.empty()) {
        const GCGraph *g = work.back();
        work.pop_back();
        if (g == nullptr || !seen.insert(g).second || !g->hasNodePayload()) {
            continue;
        }
        for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
            if (*it == nullptr || (*it)->kind != GCNodeKind::Func) {
                continue;
            }
            const GCGraph *callee = g->directCalleeGraphOf(it.ref());
            if (callee == to) {
                return true;
            }
            work.push_back(callee);
        }
    }
    return false;
}

/// The same export-time value: one constant, or one symbolic value by name.
bool sameValue(const Value &a, const Value &b) {
    if (a.isConstant() != b.isConstant()) {
        return false;
    }
    if (a.isConstant()) {
        return a.slot == b.slot && a.ty == b.ty;
    }
    if (a.isAggregate() != b.isAggregate()) {
        return false;
    }
    if (a.isAggregate()) {
        if (a.fields.size() != b.fields.size()) {
            return false;
        }
        for (size_t i = 0; i < a.fields.size(); ++i) {
            if (!sameValue(a.fields[i], b.fields[i])) {
                return false;
            }
        }
        return true;
    }
    return a.form == b.form && !a.name.empty() && a.name == b.name;
}

/// Whether an argument can be a carried value of a Loop: a tensor or a numeric / bool scalar.
bool isCarriable(const Value &v) {
    if (v.isSymbolic()) {
        return v.form == Value::Form::Tensor || v.form == Value::Form::Scalar;
    }
    if (v.ty == nullptr) {
        return false;
    }
    return tensor::asTensorType(v.ty) != nullptr || tensor::isSupportedTensorScalar(v.ty->code());
}

/// A symbolic value named `name` with the form, dtype and shape of `like`.
Value placeholderLike(const Value &like, std::string name) {
    if (type::Type *scalar = scalarCamelTypeOf(like)) {
        return Value::symbolicScalar(std::move(name), scalar);
    }
    const TensorFacts facts = factsOf(like);
    return Value::symbolic(std::move(name), facts.dtype, facts.shape);
}

} // namespace

bool Evaluator::nodeReachesCall(const GCGraph *g, gc_node_ref_t ref, const GCGraph *target) {
    std::unordered_set<gc_node_ref_t> seen{ref};
    std::vector<gc_node_ref_t> work{ref};
    while (!work.empty()) {
        const gc_node_ref_t node = work.back();
        work.pop_back();
        if (g->node(node)->kind == GCNodeKind::Func) {
            const GCGraph *callee = g->directCalleeGraphOf(node);
            if (callee == target || callReaches(callee, target)) {
                return true;
            }
        }
        for (const auto inputs : {g->normInputsOf(node), g->withInputsOf(node)}) {
            for (gc_node_ref_t input : inputs) {
                if (seen.insert(input).second) {
                    work.push_back(input);
                }
            }
        }
    }
    return false;
}

std::vector<Value>
Evaluator::callArguments(Activation &act, gc_node_ref_t site, GCGraph *callee) {
    GCGraph *g      = act.graph;
    const auto with = g->withInputsOf(site);
    const auto norm = g->normInputsOf(site);
    if (with.size() != callee->withPorts().size() || norm.size() != callee->normPorts().size()) {
        unsupported(act, "a call does not pass one argument per parameter of its callee");
    }
    std::vector<Value> args = evalAll(act, with);
    for (Value &v : evalAll(act, norm)) {
        args.push_back(std::move(v));
    }
    return args;
}

Evaluator::Activation Evaluator::activationOf(GCGraph *callee, std::span<const Value> args) {
    Activation inner{.graph = callee, .ports = {}, .memo = {{}}};
    size_t k = 0;
    for (gc_node_ref_t port : callee->withPorts()) {
        inner.ports.emplace(port, args[k++]);
    }
    for (gc_node_ref_t port : callee->normPorts()) {
        inner.ports.emplace(port, args[k++]);
    }
    return inner;
}

Value Evaluator::evalCall(Activation &act, gc_node_ref_t site) {
    GCGraph *callee = act.graph->directCalleeGraphOf(site);
    const std::string name =
        isSourceName(callee->name())
            ? std::format(" to '{}'", callee->name().substr(0, callee->name().find('$')))
            : std::string();
    for (const LoopFrame *loop : loops_) {
        if (loop->fn == callee || callReaches(callee, loop->fn)) {
            unsupported(
                act,
                std::format(
                    "a call{} remains after simplification and is not the last action of the "
                    "function (a recursion whose depth depends on the model input can only be "
                    "exported as a loop when every recursive call is in tail position)",
                    name),
                ExportError::Kind::CallRemains);
        }
    }
    std::vector<Value> args = callArguments(act, site, callee);
    if (!callReaches(callee, callee)) {
        // Not recursive: its value is its body's (simplification inlines these; one that is
        // left is evaluated in place).
        Activation inner     = activationOf(callee, args);
        const auto *exitType = callee->funcType()->exitType();
        if (callee->returnKind() == camel::runtime::GCReturnKind::None ||
            (exitType && exitType->code() == TypeCode::Void)) {
            return Value::constant(NullSlot, type::Type::Void());
        }
        return eval(inner, execute::resolveRuntimeTailValueRef(callee));
    }
    try {
        return translateLoop(act, callee, std::move(args));
    } catch (const ExportError &e) {
        if (!e.location().empty()) {
            throw;
        }
        // A refusal about the loop as a whole is reported at the call.
        const uint64_t origin = act.graph->nodeOrigin(site);
        throw ExportError(e.what(), locationOf(origin), origin, e.kind());
    }
}

Value Evaluator::selectValue(
    Activation &act, const std::string &cond, const Value &a, const Value &b) {
    if (sameValue(a, b)) {
        return a;
    }
    if (a.isAggregate() || b.isAggregate() || !isCarriable(a) || !isCarriable(b)) {
        unsupported(
            act,
            "a tuple, struct or array differs between the paths of a recursion whose depth "
            "depends on the model input; only tensors and scalars can be carried by a loop");
    }
    const TensorFacts fa = factsOf(a), fb = factsOf(b);
    if (!fa.dtype || fa.dtype != fb.dtype) {
        unsupported(
            act,
            "the paths of a recursion whose depth depends on the model input yield values of "
            "different types");
    }
    const std::string out = emitter_.node(
        "Where",
        {cond, emitter_.operand(a, *fa.dtype), emitter_.operand(b, *fa.dtype)});
    if (type::Type *scalar = scalarCamelTypeOf(a)) {
        return Value::symbolicScalar(out, scalar);
    }
    std::optional<tensor::StaticShape> shape;
    if (fa.shape && fb.shape && *fa.shape == *fb.shape) {
        shape = fa.shape;
    }
    return Value::symbolic(out, fa.dtype, shape);
}

Evaluator::BodyOutcome Evaluator::mergeOutcomes(
    Activation &act, const Value &cond, BodyOutcome a, BodyOutcome b, LoopFrame &loop) {
    const std::string c = emitter_.operand(cond, TypeCode::Bool);
    BodyOutcome out;
    if (a.contConst && b.contConst) {
        if (*a.contConst == *b.contConst) {
            out.contConst = a.contConst;
        } else {
            out.contName = *a.contConst ? c : emitter_.node("Not", {c});
        }
    } else {
        const auto operandOf = [&](const BodyOutcome &o) {
            return o.contConst ? emitter_.operand(
                                     Value::constant(
                                         rtdata::toSlot<bool>(*o.contConst),
                                         type::Type::Bool()),
                                     TypeCode::Bool)
                               : o.contName;
        };
        // (c and a) or (not c and b): ONNX Runtime has no Where kernel for bool tensors.
        out.contName = emitter_.node(
            "Or",
            {emitter_.node("And", {c, operandOf(a)}),
             emitter_.node("And", {emitter_.node("Not", {c}), operandOf(b)})});
    }
    out.next.reserve(loop.ports.size());
    for (size_t k = 0; k < loop.ports.size(); ++k) {
        out.next.push_back(selectValue(act, c, a.next[k], b.next[k]));
    }
    if (a.result && b.result) {
        out.result = selectValue(act, c, *a.result, *b.result);
    } else {
        out.result = a.result ? a.result : b.result;
    }
    return out;
}

Evaluator::BodyOutcome
Evaluator::evalLoopBody(Activation &act, gc_node_ref_t ref, LoopFrame &loop) {
    GCGraph *g    = act.graph;
    const auto *n = g->node(ref);
    switch (n->kind) {
    case GCNodeKind::Copy:
        return evalLoopBody(act, g->normInputsOf(ref).front(), loop);
    case GCNodeKind::Gate: {
        const auto norm = g->normInputsOf(ref);
        return evalLoopBody(act, norm.empty() ? g->withInputsOf(ref).back() : norm.back(), loop);
    }
    case GCNodeKind::Func: {
        GCGraph *callee = g->directCalleeGraphOf(ref);
        if (callee == loop.fn) {
            // The recursive call in tail position: the next state.
            BodyOutcome out;
            out.contConst = true;
            out.next      = callArguments(act, ref, callee);
            for (size_t k = 0; k < loop.ports.size(); ++k) {
                if (!sameValue(out.next[k], loop.inputs[k])) {
                    loop.changed[k] = true;
                }
            }
            return out;
        }
        if (callReaches(callee, loop.fn)) {
            // A function (a branch arm, a helper) on the way to the recursive call.
            const std::vector<Value> args = callArguments(act, ref, callee);
            Activation inner              = activationOf(callee, args);
            return evalLoopBody(inner, execute::resolveRuntimeTailValueRef(callee), loop);
        }
        break;
    }
    case GCNodeKind::Join: {
        const gc_node_ref_t brch = g->normInputsOf(ref).front();
        const auto arms          = g->withInputsOf(ref);
        bool recursive           = false;
        for (gc_node_ref_t arm : arms) {
            recursive = recursive || nodeReachesCall(g, arm, loop.fn);
        }
        if (!recursive) {
            break; // an ordinary branch: a value
        }
        const Value cond = eval(act, g->normInputsOf(brch).front());
        if (!g->withInputsOf(brch).empty() || arms.size() != 2) {
            unsupported(
                act,
                "a match whose arms call the function again cannot be exported as a loop; "
                "write the recursion with if / else");
        }
        if (cond.isConstant()) {
            const size_t arm = tensor::scalarToBool(cond.ty->code(), cond.slot) ? 0 : 1;
            return evalLoopBody(act, arms[arm], loop);
        }
        if (cond.form != Value::Form::Scalar) {
            unsupported(act, "the condition of a recursion is not a scalar");
        }
        BodyOutcome a = evalLoopBody(act, arms[0], loop);
        BodyOutcome b = evalLoopBody(act, arms[1], loop);
        return mergeOutcomes(act, cond, std::move(a), std::move(b), loop);
    }
    default:
        break;
    }
    // A path that returns: the state stays, the value is the result.
    BodyOutcome out;
    out.contConst = false;
    out.next      = loop.inputs;
    out.result    = eval(act, ref);
    return out;
}

Value Evaluator::translateLoop(Activation &act, GCGraph *fn, std::vector<Value> args) {
    LoopFrame loop;
    loop.fn = fn;
    loop.ports.assign(fn->withPorts().begin(), fn->withPorts().end());
    loop.ports.insert(loop.ports.end(), fn->normPorts().begin(), fn->normPorts().end());
    const size_t count          = loop.ports.size();
    const gc_node_ref_t exitRef = execute::resolveRuntimeTailValueRef(fn);

    // One evaluation of the body with `inputs` as the state, in the current scope.
    const auto evaluate = [&](std::vector<Value> inputs) {
        loop.inputs = std::move(inputs);
        loop.changed.assign(count, false);
        Activation body = activationOf(fn, loop.inputs);
        loops_.push_back(&loop);
        try {
            BodyOutcome outcome = evalLoopBody(body, exitRef, loop);
            loops_.pop_back();
            return outcome;
        } catch (...) {
            loops_.pop_back();
            throw;
        }
    };
    const auto withPlaceholders = [&](const std::vector<bool> &carried, std::string_view hint) {
        std::vector<Value> inputs = args;
        for (size_t k = 0; k < count; ++k) {
            if (carried[k]) {
                inputs[k] = placeholderLike(args[k], emitter_.fresh(hint));
            }
        }
        return inputs;
    };

    // Pass 1, discarded: which arguments does a recursive call change? Everything that can be
    // carried is a placeholder here, so that nothing folds on the first call's values (a
    // counter that starts at the constant 0).
    std::vector<bool> carriable(count, false);
    for (size_t k = 0; k < count; ++k) {
        carriable[k] = isCarriable(args[k]);
    }
    std::vector<bool> carried(count, false);
    {
        emitter_.pushScope("loop_probe");
        try {
            const BodyOutcome probe = evaluate(withPlaceholders(carriable, "probe"));
            emitter_.popScope();
            if (!probe.result) {
                unsupported(act, "a recursion that never returns cannot be exported");
            }
            for (size_t k = 0; k < count; ++k) {
                if (loop.changed[k] && !carriable[k]) {
                    unsupported(
                        act,
                        "a tuple, struct or array argument changes across a recursion whose "
                        "depth depends on the model input; only tensors and scalars can be "
                        "carried by a loop");
                }
                carried[k] = loop.changed[k];
            }
        } catch (...) {
            emitter_.popScope();
            throw;
        }
    }
    if (std::ranges::none_of(carried, [](bool c) { return c; })) {
        unsupported(
            act,
            "a recursion calls the function again with the arguments it was called with: it "
            "never ends, or it is no loop");
    }

    // Pass 2: the Loop. Arguments that never change stay what they are outside (constants keep
    // folding inside the body).
    emitter_.pushScope("loop_body");
    Graph bodyGraph;
    std::vector<ValueInfo> bodyInputs, bodyOutputs;
    std::vector<Value> finals = args;
    try {
        const std::vector<Value> inputs = withPlaceholders(carried, "state");
        const std::string iter          = emitter_.fresh("loop_iter");
        const std::string condIn        = emitter_.fresh("loop_cond_in");
        const BodyOutcome outcome       = evaluate(inputs);
        const std::string condOut       = emitter_.node(
            "Identity",
            {outcome.contConst ? emitter_.operand(
                                     Value::constant(
                                         rtdata::toSlot<bool>(*outcome.contConst),
                                         type::Type::Bool()),
                                     TypeCode::Bool)
                               : outcome.contName},
            {},
            "loop_cond");
        std::vector<ValueInfo> stateIn, stateOut;
        for (size_t k = 0; k < count; ++k) {
            if (!carried[k]) {
                if (!sameValue(outcome.next[k], inputs[k])) {
                    unsupported(act, "the set of values a recursion changes is not stable");
                }
                continue;
            }
            const TensorFacts in  = factsOf(inputs[k]);
            const TensorFacts out = factsOf(outcome.next[k]);
            if (!in.dtype || in.dtype != out.dtype || !in.shape || !out.shape ||
                *in.shape != *out.shape) {
                unsupported(
                    act,
                    "the dtype or shape of a value changes from one call of a recursion to the "
                    "next; a loop can only carry values of a fixed dtype and shape");
            }
            stateIn.push_back(valueInfoOf(inputs[k].name, in));
            stateOut.push_back(valueInfoOf(
                emitter_.node(
                    "Identity",
                    {emitter_.operand(outcome.next[k], *in.dtype)},
                    {},
                    "state_out"),
                in));
        }
        const TensorFacts flag{TypeCode::Bool, tensor::StaticShape{}};
        stateIn.insert(stateIn.begin(), valueInfoOf(condIn, flag));
        stateIn.insert(
            stateIn.begin(),
            valueInfoOf(iter, TensorFacts{TypeCode::Int64, tensor::StaticShape{}}));
        stateOut.insert(stateOut.begin(), valueInfoOf(condOut, flag));
        bodyInputs  = std::move(stateIn);
        bodyOutputs = std::move(stateOut);
    } catch (...) {
        emitter_.popScope();
        throw;
    }
    bodyGraph         = emitter_.popScope();
    bodyGraph.inputs  = std::move(bodyInputs);
    bodyGraph.outputs = std::move(bodyOutputs);
    // "" is the trip count (none); the body always runs once, as a call of the function does.
    std::vector<std::string> loopInputs{
        std::string(),
        emitter_.operand(Value::constant(rtdata::toSlot<bool>(true), type::Type::Bool()), TypeCode::Bool)};
    std::vector<size_t> carriedIndex;
    for (size_t k = 0; k < count; ++k) {
        if (carried[k]) {
            loopInputs.push_back(emitter_.operand(args[k], factsOf(args[k]).dtype));
            carriedIndex.push_back(k);
        }
    }
    const std::vector<std::string> outputs = emitter_.nodeN(
        "Loop",
        std::move(loopInputs),
        {Attribute::makeGraph("body", std::move(bodyGraph))},
        carriedIndex.size(),
        "loop_state");
    for (size_t i = 0; i < carriedIndex.size(); ++i) {
        finals[carriedIndex[i]] = placeholderLike(args[carriedIndex[i]], outputs[i]);
    }

    // The result: the body once more, at the final state, where it returns.
    const BodyOutcome last = evaluate(finals);
    if (!last.result) {
        unsupported(act, "a recursion that never returns cannot be exported");
    }
    return *last.result;
}

std::optional<Value> Evaluator::foldConstantOper(
    std::string_view uri, std::span<const Value> norm, std::span<const Value> with,
    type::Type *resultType) {
    auto found = ctx_.execMgr().find(std::string(uri));
    if (!found) {
        return std::nullopt;
    }
    std::vector<slot_t> withSlots, normSlots;
    std::vector<type::Type *> withTypes, normTypes;
    for (const Value &v : with) {
        withSlots.push_back(v.slot);
        withTypes.push_back(v.ty);
    }
    for (const Value &v : norm) {
        normSlots.push_back(v.slot);
        normTypes.push_back(v.ty);
    }
    FoldArgsView withView(std::move(withSlots), std::move(withTypes));
    FoldArgsView normView(std::move(normSlots), std::move(normTypes));
    slot_t result;
    try {
        result = (*found)(withView, normView, ctx_);
    } catch (...) {
        return std::nullopt; // leave the failure to be reported when the program runs
    }
    return keep(result, resultType);
}

// A variable of the exported function. ONNX has no cells; a variable that lives and dies inside
// one function is only a name for a sequence of values, so a read is translated as the value of
// the assignment that reaches it (the declaration's initial value when none does). That is
// decided from the graph: the assignments of this variable that must run before the read form a
// chain, and the last of the chain is the one read. Everything this cannot decide is refused: a
// variable that comes from outside the function (its content is state outside the model), one
// that a call, a branch arm or a closure receives, an assignment not ordered with the read.
Value Evaluator::evalVariableRead(Activation &act, gc_node_ref_t ref) {
    GCGraph *g               = act.graph;
    const gc_node_ref_t cell = g->normInputsOf(ref).front();
    if (!isLocalVariable(g, cell)) {
        unsupported(act, kOuterVariable);
    }
    std::vector<gc_node_ref_t> before;
    for (const auto users : {g->normOutputsOf(cell), g->withOutputsOf(cell)}) {
        for (gc_node_ref_t user : users) {
            const std::string_view uri = operUriOf(g, user);
            if (uri == ":var/get") {
                continue;
            }
            if (uri != ":op/assn" || g->normInputsOf(user).size() != 2 ||
                g->normInputsOf(user).front() != cell) {
                unsupported(
                    act,
                    "a variable of the exported function is passed to a call, a branch arm or "
                    "a closure, or is updated by a compound assignment; only a variable that "
                    "is read and assigned directly in one function can be exported");
            }
            if (runsBefore(g, user, ref)) {
                before.push_back(user);
            } else if (!runsBefore(g, ref, user)) {
                unsupported(
                    act,
                    "an assignment of a variable is not ordered with a read of it, so the "
                    "value the read sees is not determined by the graph");
            }
        }
    }
    if (before.empty()) {
        return eval(act, g->normInputsOf(cell).front());
    }
    for (gc_node_ref_t last : before) {
        bool isLast = true;
        for (gc_node_ref_t other : before) {
            isLast = isLast && (other == last || runsBefore(g, other, last));
        }
        if (isLast) {
            return eval(act, g->normInputsOf(last)[1]);
        }
    }
    unsupported(
        act,
        "the assignments of a variable before a read of it are not ordered among themselves, "
        "so the value the read sees is not determined by the graph");
}

Value Evaluator::evalOper(Activation &act, gc_node_ref_t ref) {
    GCGraph *g       = act.graph;
    const auto *body = g->nodeBodyAs<camel::runtime::GCOperBody>(ref);
    const std::string uri(body->uri());
    if (uri == ":var/get") {
        return evalVariableRead(act, ref);
    }
    std::vector<Value> norm = evalAll(act, g->normInputsOf(ref));
    std::vector<Value> with = evalAll(act, g->withInputsOf(ref));
    const auto *def         = tensor::ops::OpRegistry::instance().find(uri);

    // Simplification has evaluated every pure operator whose arguments are constants; what is
    // left is translated. Side effects have no ONNX counterpart.
    if (!core::OperatorTraitsRegistry::instance().isPure(uri)) {
        unsupported(act, std::format("impure operator '{}' cannot be exported", uri));
    }
    if (allConstant(norm) && allConstant(with) && !std::ranges::any_of(norm, isDeferredClosure) &&
        !std::ranges::any_of(with, isDeferredClosure)) {
        if (auto folded = foldConstantOper(uri, norm, with, g->node(ref)->dataType)) {
            return *folded;
        }
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
        type::Type *joinType = g->node(join)->dataType;
        if (joinType &&
            (joinType->code() == TypeCode::Tuple || joinType->code() == TypeCode::Struct)) {
            return evalSymbolicAggregateBranch(act, emitter_.operand(cond, TypeCode::Bool), arms);
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
    // Arms that already agree need no selection: the same constant (duplicated constant nodes
    // fold to the same slot, e.g. a loop counter carried across a decision tree) or the same
    // symbolic value. Emitting an If here would needlessly make the result input-dependent.
    if (results[0].isConstant() && results[1].isConstant() && results[0].slot == results[1].slot &&
        results[0].ty == results[1].ty) {
        return results[0];
    }
    if (results[0].isSymbolic() && results[1].isSymbolic() && results[0].form == results[1].form &&
        results[0].name == results[1].name && !results[0].name.empty()) {
        return results[0];
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
    for (const Value &r : results) {
        if (r.isSymbolic() && r.form == Value::Form::IntArray) {
            unsupported(act, "a branch on a model-dependent condition cannot yield an int array");
        }
    }
    if (type::Type *scalar = scalarCamelTypeOf(results[0])) {
        return Value::symbolicScalar(out, scalar);
    }
    return Value::symbolic(out, a.dtype, shape);
}

void Evaluator::collectArmLeaves(
    Activation &act, const Value &a, const Value &b, std::vector<ArmLeaf> &leaves) {
    const auto isAggregateForm = [](const Value &v) {
        return v.isAggregate() ||
               (v.isConstant() && v.ty &&
                (v.ty->code() == TypeCode::Tuple || v.ty->code() == TypeCode::Struct));
    };
    const auto asAggregate = [](const Value &v) {
        if (v.isAggregate()) {
            return v;
        }
        return Value::aggregate(v.ty, elementsOf(v.slot, v.ty));
    };
    if (isAggregateForm(a) != isAggregateForm(b)) {
        unsupported(
            act,
            "the arms of a branch on a model-dependent condition yield different types");
    }
    if (isAggregateForm(a)) {
        const Value va = asAggregate(a), vb = asAggregate(b);
        if (va.camelType != vb.camelType || va.fields.size() != vb.fields.size()) {
            unsupported(
                act,
                "the arms of a branch on a model-dependent condition yield different types");
        }
        for (size_t i = 0; i < va.fields.size(); ++i) {
            collectArmLeaves(act, va.fields[i], vb.fields[i], leaves);
        }
        return;
    }
    leaves.push_back({{a, b}});
}

Value Evaluator::rebuildArmAggregate(
    type::Type *camelType, std::span<const Value> combined, size_t &next) {
    const bool isTuple = camelType->code() == TypeCode::Tuple;
    const size_t count = isTuple ? static_cast<type::TupleType *>(camelType)->size()
                                 : static_cast<type::StructType *>(camelType)->size();
    const auto typeAt  = [&](size_t i) {
        return isTuple ? static_cast<type::TupleType *>(camelType)->typeAt(i)
                       : static_cast<type::StructType *>(camelType)->typeAt(i);
    };
    std::vector<Value> fields;
    fields.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        type::Type *fieldType = typeAt(i);
        if (fieldType->code() == TypeCode::Tuple || fieldType->code() == TypeCode::Struct) {
            fields.push_back(rebuildArmAggregate(fieldType, combined, next));
        } else {
            fields.push_back(combined[next++]);
        }
    }
    return Value::aggregate(camelType, std::move(fields));
}

/// if-then-else on a condition that depends on the input, with tuple/struct-typed arms: one
/// multi-output If whose subgraphs yield every leaf value (a deferred closure element produces
/// no output; two identical constants need no selection).
Value Evaluator::evalSymbolicAggregateBranch(
    Activation &act, const std::string &condName, std::span<const gc_node_ref_t> arms) {
    Graph bodies[2];
    Value results[2];
    for (size_t i = 0; i < 2; ++i) {
        emitter_.pushScope(i == 0 ? "then_branch" : "else_branch");
        act.memo.emplace_back();
        try {
            results[i] = eval(act, arms[i]);
            act.memo.pop_back();
            bodies[i] = emitter_.popScope();
        } catch (...) {
            act.memo.pop_back();
            emitter_.popScope();
            throw;
        }
    }
    std::vector<ArmLeaf> leaves;
    collectArmLeaves(act, results[0], results[1], leaves);

    std::vector<Value> combined(leaves.size());
    std::vector<size_t> selected;
    for (size_t j = 0; j < leaves.size(); ++j) {
        const Value &a = leaves[j].arms[0];
        const Value &b = leaves[j].arms[1];
        if (isDeferredClosure(a) || isDeferredClosure(b)) {
            combined[j] = isDeferredClosure(a) ? a : b;
            continue;
        }
        if (a.isConstant() && b.isConstant() && a.slot == b.slot && a.ty == b.ty) {
            combined[j] = a;
            continue;
        }
        // Arms that already agree need no selection (see evalSymbolicBranch).
        if (a.isSymbolic() && b.isSymbolic() && a.form == b.form && a.name == b.name &&
            !a.name.empty()) {
            combined[j] = a;
            continue;
        }
        const TensorFacts fa = factsOf(a), fb = factsOf(b);
        const bool sameForm = a.isConstant() || b.isConstant() || a.form == b.form;
        if (!fa.dtype || fa.dtype != fb.dtype || !sameForm) {
            unsupported(
                act,
                "the arms of a branch on a model-dependent condition yield different types");
        }
        if ((a.isSymbolic() && a.form == Value::Form::IntArray) ||
            (b.isSymbolic() && b.form == Value::Form::IntArray)) {
            unsupported(act, "a branch on a model-dependent condition cannot yield an int array");
        }
        selected.push_back(j);
    }
    for (size_t i = 0; i < 2; ++i) {
        for (size_t j : selected) {
            const Value &leaf = leaves[j].arms[i];
            const std::string out =
                emitter_.appendIdentity(bodies[i], emitter_.operand(leaf), "arm_out");
            bodies[i].outputs.push_back(valueInfoOf(out, factsOf(leaf)));
        }
    }
    const std::vector<std::string> outputs = emitter_.nodeN(
        "If",
        {condName},
        {Attribute::makeGraph("then_branch", std::move(bodies[0])),
         Attribute::makeGraph("else_branch", std::move(bodies[1]))},
        selected.size());
    for (size_t k = 0; k < selected.size(); ++k) {
        const size_t j       = selected[k];
        const Value &a       = leaves[j].arms[0];
        const Value &b       = leaves[j].arms[1];
        const TensorFacts fa = factsOf(a), fb = factsOf(b);
        // Extents that agree survive; others become dynamic.
        std::optional<tensor::StaticShape> shape;
        if (fa.shape && fb.shape && fa.shape->size() == fb.shape->size()) {
            shape = fa.shape;
            for (size_t d = 0; d < shape->size(); ++d) {
                if ((*fa.shape)[d] != (*fb.shape)[d]) {
                    (*shape)[d] = tensor::kUnknownDim;
                }
            }
        }
        if (type::Type *scalar = scalarCamelTypeOf(a)) {
            combined[j] = Value::symbolicScalar(outputs[k], scalar);
        } else {
            combined[j] = Value::symbolic(outputs[k], fa.dtype, shape);
        }
    }
    size_t next           = 0;
    type::Type *camelType = results[0].isAggregate() ? results[0].camelType : results[0].ty;
    return rebuildArmAggregate(camelType, combined, next);
}

Value Evaluator::evalFill(Activation &act, gc_node_ref_t ref) {
    GCGraph *g             = act.graph;
    const Value source     = eval(act, g->normInputsOf(ref).front());
    type::Type *targetType = g->node(ref)->dataType;
    const auto slots       = g->nodeBodyAs<camel::runtime::GCFillBody>(ref)->slots();
    const auto refs        = g->withInputsOf(ref);
    const auto elemTypeAt  = [&](size_t k) -> type::Type  *{
        if (targetType->code() == TypeCode::Tuple) {
            return static_cast<type::TupleType *>(targetType)->typeAt(slots[k]);
        }
        if (targetType->code() == TypeCode::Struct) {
            return static_cast<type::StructType *>(targetType)->typeAt(slots[k]);
        }
        return nullptr;
    };
    const bool isAggregate =
        targetType->code() == TypeCode::Tuple || targetType->code() == TypeCode::Struct;
    std::vector<Value> values(refs.size());
    bool allConst    = true;
    bool sawDeferred = false;
    for (size_t k = 0; k < refs.size(); ++k) {
        // A closure element of a tuple or struct that simplification did not fold captures a
        // value that depends on the model input (e.g. a branch arm's (value, pullback) tuple
        // whose pullback is never called on the exported path). It has no ONNX representation
        // and emits nothing, so it is deferred instead of failing the whole aggregate: the
        // element is evaluated only if an access reads it back. The marker carries the function
        // type; anything that tries to use it as a value fails on the type.
        type::Type *elemType = isAggregate ? elemTypeAt(k) : nullptr;
        if (elemType && elemType->code() == TypeCode::Function && source.isConstant()) {
            values[k]   = Value::constant(NullSlot, elemType);
            sawDeferred = true;
            continue;
        }
        values[k] = eval(act, refs[k]);
        allConst  = allConst && values[k].isConstant();
    }
    if (source.isConstant() && !allConst && isIntArrayType(targetType)) {
        return symbolicIntArray(act, ref, source, values);
    }
    if (source.isConstant() && !allConst && isFloatArrayType(targetType)) {
        return symbolicFloatArray(act, ref, source, values);
    }
    if (source.isConstant() && (sawDeferred || !allConst) && isAggregate) {
        // A tuple or struct of input-dependent values (e.g. gradients) exists only at export
        // time; its elements are read back by accesses or become graph outputs.
        std::vector<Value> fields = elementsOf(source.slot, targetType);
        for (size_t k = 0; k < slots.size(); ++k) {
            fields[static_cast<size_t>(slots[k])] = values[k];
        }
        return Value::aggregate(targetType, std::move(fields));
    }
    if (!source.isConstant() || !allConst) {
        unsupported(
            act,
            "a value that depends on the model input is stored in an array or a closure; pass "
            "it as a function argument instead");
    }
    unsupported(act, "a tuple, struct or array of constants was not folded");
}

/// An int[] literal with elements that depend on the input (e.g. [shape(x)[0], 128]) becomes a
/// 1-D int64 tensor: constant and symbolic elements concatenated.
// A float array literal with elements that depend on the model input (`[loss]`, as in
// `reshape([loss], [1])`). An array of numbers is a tensor to every tensor operator, with the
// dtype a constant array of that element type gets, so it is emitted as that 1-D tensor: each
// element unsqueezed and the elements concatenated.
Value Evaluator::symbolicFloatArray(
    Activation &act, gc_node_ref_t ref, const Value &source, std::span<const Value> values) {
    auto *templ          = rtdata::fromSlot<::Array *>(source.slot);
    type::Type *elemType = static_cast<type::ArrayType *>(act.graph->node(ref)->dataType)->elemType();
    const TypeCode dtype = tensor::normalizeTensorDType(elemType->code());
    const auto fillSlots = act.graph->nodeBodyAs<camel::runtime::GCFillBody>(ref)->slots();
    std::vector<std::optional<Value>> elems(templ->size());
    for (size_t k = 0; k < fillSlots.size(); ++k) {
        elems[static_cast<size_t>(fillSlots[k])] = values[k];
    }
    if (elems.empty()) {
        unsupported(act, "an empty array cannot be exported");
    }
    const int64_t axis[] = {0};
    std::vector<std::string> parts;
    for (size_t i = 0; i < elems.size(); ++i) {
        const Value elem = elems[i] ? *elems[i] : Value::constant(templ->get<slot_t>(i), elemType);
        if (elem.isSymbolic() && elem.form != Value::Form::Scalar) {
            unsupported(
                act,
                "a float array element that depends on the model input must be a float");
        }
        parts.push_back(
            emitter_.node("Unsqueeze", {emitter_.operand(elem, dtype), emitter_.int64s(axis)}));
    }
    return Value::symbolic(
        emitter_.node("Concat", std::move(parts), {Attribute::makeInt("axis", 0)}),
        dtype,
        tensor::StaticShape{static_cast<int64_t>(elems.size())});
}

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
                        throw ExportError(
                            std::format(
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
            emitter_.addInput(std::move(inputInfo));
            return {input, tensor::TensorType::get(t->dtype(), shape)};
        }
        if (ty->code() == camel::nn::OptimizerStateType::typeCode()) {
            // An optimizer state flattens to its two moment trees (one input per leaf, named
            // adam_m_<path> / adam_v_<path> like a parameter tree) plus the step count as a
            // rank-0 int64 input adam_t.
            auto *state    = rtdata::fromSlot<camel::nn::OptimizerStateObject *>(example.value);
            ::Tuple *trees = state->trees();
            type::TupleType *treesType = state->treesType();
            auto [m, mType] = bind({trees->get<slot_t>(0), treesType->typeAt(0)}, "adam_m", false);
            auto [v, vType] = bind({trees->get<slot_t>(1), treesType->typeAt(1)}, "adam_v", false);
            (void)mType;
            (void)vType;
            Value t = Value::symbolicScalar("adam_t", type::Type::Int64());
            if (!names_.insert("adam_t").second) {
                throw ExportError(std::string("two inputs are named 'adam_t'"));
            }
            emitter_.addInput(valueInfoOf("adam_t", factsOf(t)));
            return {
                optimizerStateValue(std::move(m), std::move(v), std::move(t)),
                camel::nn::OptimizerStateType::Default()};
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
        throw ExportError(
            std::format(
                "an exported function's arguments must be tensors or tuples/structs of tensors, "
                "not "
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
                    ? std::string(static_cast<type::StructType *>(result.camelType)->fieldName(i))
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
        throw ExportError(
            std::format(
                "output '{}' must be a tensor or a number, not '{}'",
                name,
                result.ty ? result.ty->toString() : "a value of unknown type"));
    }
    const std::string output = emitter.node("Identity", {emitter.operand(result)}, {}, name);
    emitter.graph().outputs.push_back(valueInfoOf(output, facts));
}

/// The capability table of the export target, or null when the check is off. Throws on an
/// unknown target.
const CapabilityTable *capabilitiesFor(const ExportOptions &options) {
    if (options.target.empty() || options.target == "none") {
        return nullptr;
    }
    const CapabilityTable *capabilities = CapabilityTable::forTarget(options.target);
    if (!capabilities) {
        std::string known;
        for (const std::string &t : CapabilityTable::targets()) {
            known += (known.empty() ? "" : ", ") + t;
        }
        throw ExportError(
            std::format(
                "unknown export target '{}' (known: {}, or 'none')",
                options.target,
                known));
    }
    return capabilities;
}

void checkArity(GCGraph *graph, size_t examples) {
    if (graph->withPorts().size() != 0 || graph->normPorts().size() != examples) {
        throw ExportError(
            std::format(
                "'{}' takes {} arguments but {} examples were given",
                graph->name(),
                graph->withPorts().size() + graph->normPorts().size(),
                examples));
    }
}

struct BoundInputs {
    std::vector<Value> values;
    std::vector<type::Type *> types; // the exact type of each input (dtypes and shapes)
};

/// Declares one graph input per tensor of `examples` and returns the parameters' values.
BoundInputs
bindInputs(Emitter &emitter, const ExportOptions &options, std::span<const Example> examples) {
    InputBinder binder(emitter, options);
    BoundInputs inputs;
    size_t tensorArgs = 0;
    for (const Example &example : examples) {
        tensorArgs += tensor::asTensorType(example.type) ? 1 : 0;
    }
    for (size_t i = 0; i < examples.size(); ++i) {
        const std::string name  = tensorArgs == 1 && examples.size() == 1
                                      ? options.inputName
                                      : std::format("{}{}", options.inputName, i);
        auto [input, exactType] = binder.bind(examples[i], name, true);
        inputs.values.push_back(std::move(input));
        inputs.types.push_back(exactType);
    }
    return inputs;
}

Model finishModel(Emitter &emitter, const ExportOptions &options) {
    Model model;
    model.graph = std::move(emitter.graph());
    model.opset = options.opset;
    return model;
}

/// How both export entries simplify the bound function: the std::opt::simplify fixpoint with
/// everything that is not recursive inlined, and the export-only split of branched calls.
OptimizeRewriteConfig exportRewriteConfig() {
    OptimizeRewriteConfig config;
    // Everything that is not recursive is inlined; ONNX has no calls.
    config.inlineConfig.smallSubgraphMaxNonDataPortNodes = std::numeric_limits<size_t>::max();
    // The pullback of a data-dependent branch is an indirect call of a closure projected out
    // of the branch's JOIN; split it into per-arm calls with one JOIN per value. The graph is
    // only walked as a value DAG from here on (no execution topo is built), so the sibling
    // JOINs this creates are valid.
    config.enableBranchedCallSplit = true;
    // An export has to reach straight-line code or it is refused, so it is not held to the
    // growth bounds that keep an optimization pass cheap (CAMEL_OPT_GROWTH_BUDGET still applies
    // when set). A graph too large for the runtime representation fails at commit.
    config.growthBudgetNodes          = std::numeric_limits<size_t>::max();
    config.inlineConfig.maxOwnerNodes = std::numeric_limits<size_t>::max();
    return config;
}

// Recursion unrolls about one level per round; one of dynamic depth stops changing instead.
constexpr size_t kMaxExportRounds = 4096;

/// The simplification onnx.export_model performs on the bound function: macro expansion and
/// the export simplification, to a fixpoint of the two.
GCGraph *
simplifyFully(const core::context::context_ptr_t &context, GCGraph *root, std::ostream &log) {
    const OptimizeRewriteConfig config = exportRewriteConfig();
    constexpr size_t kMaxRounds        = kMaxExportRounds;
    // Macros (grad, value_and_grad) expand once their arguments are constants, which
    // simplification may be what makes them.
    root = MacroRewritePass(context).apply(root, log);
    while (true) {
        root              = simplifyGraph(context, root, config, log, kMaxRounds);
        GCGraph *expanded = MacroRewritePass(context).apply(root, log);
        if (expanded == root) {
            break;
        }
        root = expanded;
    }
    return root;
}

/// Binds the closure slots of `draft` to the constants `captures` (a closure the program
/// builds from constants, which exists only as a FILL node until the program runs).
void bindCaptures(camel::runtime::GraphDraft &draft, std::span<const Example> captures) {
    const std::vector<gc_node_ref_t> nodes(
        draft.closureNodes().begin(),
        draft.closureNodes().end());
    if (nodes.size() != captures.size()) {
        throw ExportError(
            std::format(
                "the function captures {} values but {} are bound",
                nodes.size(),
                captures.size()));
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
        const gc_node_ref_t value = draft.materializeStaticValue(
            captures[i].value,
            captures[i].type,
            camel::runtime::kGCNodeFlagConstant);
        draft.replaceAllValueUses(nodes[i], value);
        draft.removeClosureNode(nodes[i]);
        draft.eraseNode(nodes[i]);
    }
}

/// How much of the function's simplification an export performs itself.
enum class Preparation {
    /// onnx.export_model: macro expansion and the whole std::opt::simplify fixpoint.
    Full,
    /// onnx::export: the same simplification without macro expansion, which is the business
    /// of std::macro before the pass (the name is historical: the pass used to fold shapes
    /// only).
    ShapesOnly,
};

Model exportBound(
    core::context::Context &ctx, ::Function *fn, std::span<const Example> captures,
    std::span<const Example> examples, const ExportOptions &options, Preparation preparation) {
    if (!fn || !fn->graph()) {
        throw ExportError("expected a function value");
    }
    GCGraph *graph = fn->graph();
    checkArity(graph, examples.size());

    Emitter emitter(options.opset, capabilitiesFor(options));
    emitter.graph().name                        = options.graphName;
    const BoundInputs bound                     = bindInputs(emitter, options, examples);
    const std::vector<Value> &inputs            = bound.values;
    const std::vector<type::Type *> &inputTypes = bound.types;

    // The function applied to inputs of the examples' types, with its captures bound, is a
    // program of its own: simplification specializes it for the input shapes, inlines its
    // calls, unrolls recursion of static depth and folds everything that does not depend on the
    // inputs. The running program is left as it is. Simplification allocates and may collect:
    // what is read afterwards stays rooted.
    std::optional<core::mm::RootHandle> rooted;
    if (fn->tuple()) {
        rooted.emplace(
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
        if (!captures.empty()) {
            bindCaptures(session.rootDraft(), captures);
        }
        camel::runtime::bindFunctionCall(session.rootDraft(), fn, inputTypes);
        root = session.commit();
    }
    std::ostringstream log;
    if (preparation == Preparation::ShapesOnly) {
        // The pass entry. The preceding passes simplified the program without two things only
        // the export knows: the shapes of the examples (binding their types above re-inferred
        // the node types, so the shape of a tensor and the arithmetic on it are constants
        // now, and a recursion bounded by a shape can unroll) and the split of branched calls,
        // which is valid only for a consumer that never executes the graph. The same
        // simplification as onnx.export_model finishes the job with them, so that the two
        // entries accept the same functions. Macros are not expanded here: that stays the
        // business of std::macro before this pass.
        root = simplifyGraph(context, root, exportRewriteConfig(), log, kMaxExportRounds);
    } else {
        root = simplifyFully(context, root, log);
    }
    if (dumpEnabled()) {
        GraphVizDumpPass(context, GraphVizDumpConfig{.readableOnly = true}).apply(root, std::cerr);
    }

    std::unordered_map<slot_t, std::string> names;
    for (const Example &capture : captures) {
        collectNames(capture.value, capture.type, "", names);
    }
    if (rooted) {
        const auto *captured = rooted->getAs<::Tuple>();
        const auto *layout   = static_cast<const type::TupleType *>(rooted->type());
        for (size_t i = 0; i < captured->size(); ++i) {
            collectNames(captured->get<slot_t>(i), layout->typeAt(i), "", names);
        }
    }
    Evaluator evaluator(emitter, options, ctx, std::move(names), ctx.sourceContext());
    evaluator.refuseEffects(root);
    emitOutputs(emitter, evaluator.translate(root, inputs), options.outputName);
    return finishModel(emitter, options);
}

} // namespace

Model translateFunction(
    core::context::Context &ctx, ::Function *fn, std::span<const Example> captures,
    std::span<const Example> examples, const ExportOptions &options) {
    return exportBound(ctx, fn, captures, examples, options, Preparation::ShapesOnly);
}

Model exportFunction(
    core::context::Context &ctx, ::Function *fn, std::span<const Example> examples,
    const ExportOptions &options) {
    return exportBound(ctx, fn, {}, examples, options, Preparation::Full);
}

} // namespace camel::onnx
