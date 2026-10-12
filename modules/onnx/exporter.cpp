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

#include "camel/core/error/runtime.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/source/manager.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/entry_args.h"
#include "camel/execute/executor.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/runtime/draft_inline.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/passes/macro/macro.h"
#include "camel/passes/opt/generic.h"
#include "camel/passes/opt/config.h"
#include "camel/runtime/capacity.h"
#include "camel/passes/trans/dot/graphviz.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <functional>
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
    struct LinearFrame;
    /// How the body of a linear recursion is being evaluated: which arm of its deciding branch
    /// is taken, and what the recursive call stands for.
    struct LinearCtx {
        const LinearFrame *frame = nullptr;
        size_t arm               = 0;
        const Value *hole        = nullptr;
    };
    struct Activation {
        GCGraph *graph;
        std::unordered_map<gc_node_ref_t, Value> ports;
        const LinearCtx *linear = nullptr;
        // Memo layers: the base layer plus one per enclosing If arm being emitted. A value first
        // computed inside an arm lives in that arm's subgraph and is only visible there.
        std::vector<std::unordered_map<gc_node_ref_t, Value>> memo;
    };

    Value eval(Activation &act, gc_node_ref_t ref);
    Value evalNode(Activation &act, gc_node_ref_t ref);
    Value evalOper(Activation &act, gc_node_ref_t ref);
    Value evalVariableRead(Activation &act, gc_node_ref_t ref);
    Value variableValueAt(Activation &act, gc_node_ref_t cell, gc_node_ref_t at);
    Value applyOper(
        Activation &act, const std::string &uri, std::vector<Value> norm, type::Type *resultType,
        std::vector<Value> with = {});
    Value evalJoin(Activation &act, gc_node_ref_t ref);
    Value evalFill(Activation &act, gc_node_ref_t ref);
    Value symbolicFloatArray(
        Activation &act, gc_node_ref_t ref, const Value &source, std::span<const Value> values);
    Value symbolicIntArray(
        Activation &act, gc_node_ref_t ref, const Value &source, std::span<const Value> values);
    Value
    evalSymbolicBranch(Activation &act, const Value &cond, std::span<const gc_node_ref_t> arms);
    /// Refuses a branch on a model-dependent condition whose arms yield functions (of type
    /// `functionType`, when known); eval() locates it at the branch.
    [[noreturn]] void refuseFunctionBranch(Activation &act, type::Type *functionType);
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
        bool wantResult = true;    // false while the loop body is built: only the state matters
    };
    /// What evaluating the function body from one node amounts to.
    struct BodyOutcome {
        // Whether the function calls itself again: a constant, or a bool value by name.
        std::optional<bool> contConst;
        std::string contName;
        std::vector<Value> next;     // the arguments of that call; the current ones on a return
        std::optional<Value> result; // the value returned, on the paths that return
        bool returns = false;        // some path of it returns (the recursion ends)
    };
    Value evalCall(Activation &act, gc_node_ref_t site);
    Value translateLoop(Activation &act, GCGraph *fn, std::vector<Value> args);
    BodyOutcome evalLoopBody(Activation &act, gc_node_ref_t ref, LoopFrame &loop);
    BodyOutcome mergeOutcomes(
        Activation &act, const Value &cond, BodyOutcome sides[2], Graph bodies[2], LoopFrame &loop);
    Value selectValue(Activation &act, const std::string &cond, const Value &a, const Value &b);
    std::vector<Value> callArguments(Activation &act, gc_node_ref_t site, GCGraph *callee);
    Activation activationOf(GCGraph *callee, std::span<const Value> args);
    bool nodeReachesCall(const GCGraph *g, gc_node_ref_t ref, const GCGraph *target);
    std::vector<LoopFrame *> loops_;
    GCGraph *nonTailFn_ = nullptr; // the function whose loop translation met a call outside tail position

    // ---- linear recursion as paired loops, and functions as export-time values (see below)
    struct LinearFrame {
        GCGraph *fn = nullptr;
        std::vector<gc_node_ref_t> ports;
        std::vector<Value> args; // the arguments of the first call
        std::vector<bool> carried;
        gc_node_ref_t site = camel::runtime::kInvalidNodeRef; // the one recursive call
        gc_node_ref_t join = camel::runtime::kInvalidNodeRef; // the branch that decides it
        gc_node_ref_t cond = camel::runtime::kInvalidNodeRef;
        size_t recArm      = 0;
        std::vector<Value> finals; // the arguments of the last call
        Value count;               // the number of recursive calls
        Value base;                // what the last call returns
    };
    struct LoopClosureRef {
        size_t frame;
        size_t leaf;
    };
    using LoopBody = std::function<std::pair<std::optional<std::string>, std::vector<Value>>(
        const Value &iter, const std::vector<Value> &state)>;
    using HoleCall = std::function<Value(size_t leaf, const std::vector<Value> &args)>;
    std::vector<Value> emitLoop(
        Activation &act, const std::optional<std::string> &trip, const std::vector<Value> &init,
        const LoopBody &body);
    Value translateLinear(Activation &act, GCGraph *fn, std::vector<Value> args);
    Value linearBody(const LinearFrame &f, std::span<const Value> state, size_t arm, const Value *hole);
    std::vector<Value> linearNext(const LinearFrame &f, std::span<const Value> state);
    std::string linearCont(Activation &act, const LinearFrame &f, std::span<const Value> state);
    std::vector<Value> linearReplay(Activation &act, const LinearFrame &f, const Value &steps);
    Value evalIndirectCall(Activation &act, gc_node_ref_t ref);
    Value callClosure(Activation &act, const Value &fn, const std::vector<Value> &args, size_t withCount);
    Value callLoopClosure(Activation &act, const Value &fn, const std::vector<Value> &args, size_t withCount);
    /// Refuses a linear recursion for a condition it fails, at its recursive call.
    [[noreturn]] void refuseLinear(const LinearFrame &f, std::string_view what);
    Value mergeValues(Activation &act, const std::string &cond, const Value &a, const Value &b);
    Value evalEagerBranch(Activation &act, const Value &cond, std::span<const gc_node_ref_t> arms);
    std::optional<Value> probeSpecialClosure(Activation &act, gc_node_ref_t ref);
    std::deque<LinearFrame> linearFrames_;
    std::vector<LoopClosureRef> loopClosures_;
    const HoleCall *holeCall_ = nullptr;
    int closuresWanted_       = 0; // > 0 while functions are kept as values instead of deferred

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
    // `with` parameters first, then the ordinary ones (a function exported through
    // export_model has none of the former: checkArity).
    size_t next = 0;
    for (gc_node_ref_t port : graph->withPorts()) {
        if (next < inputs.size()) {
            act.ports.emplace(port, inputs[next++]);
        }
    }
    for (gc_node_ref_t port : graph->normPorts()) {
        if (next < inputs.size()) {
            act.ports.emplace(port, inputs[next++]);
        }
    }
    const auto *exitType = graph->funcType() ? graph->funcType()->exitType() : nullptr;
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

/// An assignment operator, taken apart: `:op/assn[_T]` stores its operand; `:op/assn_<op>[_T]`
/// (`+=`, `-=`, ...) stores `old <op> operand`, where `:op/<op>[_T]` is the binary operator.
struct AssignmentKind {
    bool isAssignment = false;
    std::string binary; // empty for a plain assignment
};

AssignmentKind assignmentKindOf(std::string_view uri) {
    constexpr std::string_view prefix = ":op/assn";
    if (!uri.starts_with(prefix)) {
        return {};
    }
    std::string_view rest = uri.substr(prefix.size()); // "", "_l", "_add", "_add_l", ...
    if (rest.empty()) {
        return {.isAssignment = true, .binary = {}};
    }
    if (rest.front() != '_') {
        return {};
    }
    rest.remove_prefix(1);
    const auto isTypeSuffix = [](std::string_view t) { return t.size() == 1; };
    if (isTypeSuffix(rest)) {
        return {.isAssignment = true, .binary = {}};
    }
    return {.isAssignment = true, .binary = std::format(":op/{}", rest)};
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
                    ((uri == ":var/get" || assignmentKindOf(uri).isAssignment) &&
                     !g->normInputsOf(ref).empty() &&
                     isLocalVariable(g, g->normInputsOf(ref).front()));
                if (!onLocalVariable && !core::OperatorTraitsRegistry::instance().isPure(uri)) {
                    const uint64_t origin = g->nodeOrigin(ref);
                    const std::string what =
                        uri == ":var/get" || assignmentKindOf(uri).isAssignment
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
    // Bound from outside: parameters, and the captured values of a closure being called.
    if (auto bound = act.ports.find(ref); bound != act.ports.end()) {
        return bound->second;
    }
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
        // `e as T` is a checked conversion. Of a constant it is performed now. Of a tensor that
        // depends on the model input it is an assertion about element type and shape, and an
        // ONNX model checks none when it runs: it is exported (as nothing) only when what is
        // known of the value at export time already proves it, and refused otherwise.
        const gc_node_ref_t source = g->normInputsOf(ref).front();
        Value src                  = eval(act, source);
        if (src.isConstant()) {
            try {
                return keep(n->dataType->castSlotFrom(src.slot, src.ty), n->dataType);
            } catch (const core::error::RuntimeFault &fault) {
                // What the conversion says itself ("conversion to 'T' failed: ...").
                unsupported(
                    act,
                    std::format(
                        "this conversion of a constant always fails: {}",
                        fault.formatArgs().empty() ? std::string(fault.what())
                                                   : fault.formatArgs().front()));
            }
        }
        if (tensor::asTensorType(n->dataType)) {
            const TensorFacts facts = factsOf(src);
            type::Type *known       = src.form == Value::Form::Tensor
                                          ? tensor::TensorType::get(facts.dtype, facts.shape)
                                          : inferenceTypeOf(src);
            type::Type *declared    = g->node(source)->dataType;
            const auto safety       = n->dataType->castSafetyFrom(known);
            if (safety == type::CastSafety::Safe ||
                (declared && n->dataType->castSafetyFrom(declared) == type::CastSafety::Safe)) {
                return src;
            }
            const std::string have = known ? known->toString() : std::string("a value of unknown type");
            if (safety == type::CastSafety::Forbidden) {
                unsupported(
                    act,
                    std::format(
                        "this conversion to '{}' always fails: for the exported inputs the "
                        "value is a '{}'",
                        n->dataType->toString(),
                        have));
            }
            unsupported(
                act,
                std::format(
                    "the conversion to '{}' of a value that depends on the model input cannot "
                    "be checked when the model is exported: the value is only known to be a "
                    "'{}', and an ONNX model does not check a shape or an element type when it "
                    "runs. Convert to a type that leaves open what the inputs leave open (a "
                    "negative extent for a dynamic axis), or export with inputs that fix it",
                    n->dataType->toString(),
                    have));
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
        return evalIndirectCall(act, ref);
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
// The function may return several values: a tuple or struct (nested) whose leaves are tensors
// and scalars (float, int, bool), each a carried value or computed from them where the
// recursion returns. The result is handled leaf by leaf (mergeOutcomes), so a counter comes out
// as the int64 scalar it is.
//
// Refused, with a diagnostic: a recursive call that is not in tail position, a carried value
// whose dtype or shape changes between iterations, a tuple, struct or array argument that
// changes, a result leaf that is neither a tensor nor a scalar (a function, an int array built
// from model-dependent values), a recursion that never returns.

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

/// A call site that makes a recursion a tree: a graph reachable from `root` that calls its own
/// recursion in two places on one path (two calls that are not the alternative arms of a
/// branch). Returns the graph and the second such call, or nothing.
std::optional<std::pair<const GCGraph *, gc_node_ref_t>> findTreeRecursion(const GCGraph *root) {
    std::unordered_set<const GCGraph *> seen;
    std::vector<const GCGraph *> work{root};
    while (!work.empty()) {
        const GCGraph *g = work.back();
        work.pop_back();
        if (g == nullptr || !seen.insert(g).second || !g->hasNodePayload()) {
            continue;
        }
        std::unordered_set<gc_node_ref_t> arms;
        for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
            if (*it != nullptr && (*it)->kind == GCNodeKind::Join) {
                for (gc_node_ref_t arm : g->withInputsOf(it.ref())) {
                    arms.insert(arm);
                }
            }
        }
        size_t recursive = 0;
        for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
            if (*it == nullptr || (*it)->kind != GCNodeKind::Func) {
                continue;
            }
            const GCGraph *callee = g->directCalleeGraphOf(it.ref());
            work.push_back(callee);
            if (arms.contains(it.ref())) {
                continue;
            }
            if ((callee == g || callReaches(callee, g)) && ++recursive == 2) {
                return std::pair{g, it.ref()};
            }
        }
    }
    return std::nullopt;
}

/// A call that closes a call cycle: a graph reachable from `root` and a call in it of a
/// function from which that graph is called again. Nothing when no function is recursive.
std::optional<std::pair<const GCGraph *, gc_node_ref_t>> findRecursiveCall(const GCGraph *root) {
    std::unordered_set<const GCGraph *> seen;
    std::vector<const GCGraph *> work{root};
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
            if (callee == g || callReaches(callee, g)) {
                return std::pair{g, it.ref()};
            }
            work.push_back(callee);
        }
    }
    return std::nullopt;
}

/// Size of what is reachable from `root` by direct calls: graphs, nodes, direct and indirect
/// calls, branches. For CAMEL_ONNX_PREP_STATS (how much the export's own simplification does).
std::string reachableStats(const GCGraph *root) {
    size_t graphs = 0, nodes = 0, direct = 0, indirect = 0, branches = 0;
    std::unordered_set<const GCGraph *> seen;
    std::vector<const GCGraph *> work{root};
    while (!work.empty()) {
        const GCGraph *g = work.back();
        work.pop_back();
        if (g == nullptr || !seen.insert(g).second || !g->hasNodePayload()) {
            continue;
        }
        ++graphs;
        for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
            if (*it == nullptr) {
                continue;
            }
            ++nodes;
            switch ((*it)->kind) {
            case GCNodeKind::Func:
                ++direct;
                work.push_back(g->directCalleeGraphOf(it.ref()));
                break;
            case GCNodeKind::Call:
                ++indirect;
                break;
            case GCNodeKind::Brch:
                ++branches;
                break;
            default:
                break;
            }
        }
    }
    return std::format(
        "{{\"graphs\":{},\"nodes\":{},\"direct_calls\":{},\"indirect_calls\":{},\"branches\":{}}}",
        graphs,
        nodes,
        direct,
        indirect,
        branches);
}

/// The same export-time value: one constant, or one symbolic value by name.
bool sameValue(const Value &a, const Value &b) {
    if (a.isConstant() != b.isConstant()) {
        return false;
    }
    if (a.isConstant()) {
        return a.slot == b.slot && a.ty == b.ty;
    }
    if (a.isClosure() || b.isClosure()) {
        if (!a.isClosure() || !b.isClosure() || a.closureKind != b.closureKind ||
            a.closureRef != b.closureRef || a.slot != b.slot || a.name != b.name ||
            a.fields.size() != b.fields.size()) {
            return false;
        }
        for (size_t i = 0; i < a.fields.size(); ++i) {
            if (!sameValue(a.fields[i], b.fields[i])) {
                return false;
            }
        }
        return true;
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

/// Whether a value is, or holds, a function kept as an export-time value.
bool holdsClosure(const Value &v) {
    if (v.isClosure()) {
        return true;
    }
    if (v.isAggregate()) {
        return std::ranges::any_of(v.fields, holdsClosure);
    }
    return false;
}

/// The leaves of a value: itself, or the elements of a tuple / struct, depth first.
void flattenValue(const Value &v, std::vector<Value> &leaves) {
    if (v.isAggregate()) {
        for (const Value &field : v.fields) {
            flattenValue(field, leaves);
        }
        return;
    }
    if (v.isConstant() && v.ty && v.slot != NullSlot &&
        (v.ty->code() == TypeCode::Tuple || v.ty->code() == TypeCode::Struct)) {
        for (const Value &field : elementsOf(v.slot, v.ty)) {
            flattenValue(field, leaves);
        }
        return;
    }
    leaves.push_back(v);
}

/// A value of the structure of `like` with `leaves` in flattenValue's order.
Value rebuildValue(const Value &like, std::span<const Value> leaves, size_t &next) {
    if (like.isAggregate()) {
        std::vector<Value> fields;
        for (const Value &field : like.fields) {
            fields.push_back(rebuildValue(field, leaves, next));
        }
        return Value::aggregate(like.camelType, std::move(fields));
    }
    if (like.isConstant() && like.ty && like.slot != NullSlot &&
        (like.ty->code() == TypeCode::Tuple || like.ty->code() == TypeCode::Struct)) {
        std::vector<Value> fields;
        for (const Value &field : elementsOf(like.slot, like.ty)) {
            fields.push_back(rebuildValue(field, leaves, next));
        }
        return Value::aggregate(like.ty, std::move(fields));
    }
    return leaves[next++];
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
    if (act.linear != nullptr && callee == act.linear->frame->fn && act.graph == callee &&
        site == act.linear->frame->site) {
        if (act.linear->hole == nullptr) {
            unsupported(act, "the result of a recursive call is used on the path that returns");
        }
        return *act.linear->hole;
    }
    for (const LoopFrame *loop : loops_) {
        if (loop->fn == callee || callReaches(callee, loop->fn)) {
            if (loop == loops_.back() && loop->fn == callee) {
                nonTailFn_ = callee;
            }
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
    const auto located = [&](const ExportError &e) {
        // A refusal about the recursion as a whole is reported at the call.
        const uint64_t origin = act.graph->nodeOrigin(site);
        return ExportError(e.what(), locationOf(origin), origin, e.kind());
    };
    nonTailFn_ = nullptr;
    try {
        return translateLoop(act, callee, args);
    } catch (const ExportError &e) {
        if (nonTailFn_ != callee) {
            if (!e.location().empty()) {
                throw;
            }
            throw located(e);
        }
    }
    // The recursive call is not in tail position: a linear recursion, or nothing exportable.
    nonTailFn_ = nullptr;
    try {
        return translateLinear(act, callee, std::move(args));
    } catch (const ExportError &e) {
        if (!e.location().empty()) {
            throw;
        }
        throw located(e);
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
    Activation &act, const Value &cond, BodyOutcome sides[2], Graph bodies[2], LoopFrame &loop) {
    const std::string c = emitter_.operand(cond, TypeCode::Bool);
    BodyOutcome &a      = sides[0];
    BodyOutcome &b      = sides[1];
    BodyOutcome out;
    out.returns = a.returns || b.returns;

    // What the If hands out: per output, the value on each side (a name valid in the subgraph
    // of that side, or outside the If) and the facts it is declared with.
    struct Output {
        std::string names[2];
        TensorFacts facts;
        Value like;
    };
    std::vector<Output> outputs;
    const auto request = [&](const Value &va, const Value &vb) {
        const TensorFacts fa = factsOf(va), fb = factsOf(vb);
        if (!isCarriable(va) || !isCarriable(vb) || !fa.dtype || fa.dtype != fb.dtype) {
            unsupported(
                act,
                "a tuple, struct or array, or values of different types, differ between the "
                "paths of a recursion whose depth depends on the model input");
        }
        outputs.push_back(
            {{emitter_.operand(va, *fa.dtype), emitter_.operand(vb, *fa.dtype)},
             fa,
             va.isSymbolic() ? va : vb});
        return outputs.size() - 1;
    };
    const auto boolOperand = [&](const BodyOutcome &o) {
        return o.contConst ? emitter_.operand(
                                 Value::constant(
                                     rtdata::toSlot<bool>(*o.contConst),
                                     type::Type::Bool()),
                                 TypeCode::Bool)
                           : o.contName;
    };

    std::optional<size_t> contAt;
    if (a.contConst && b.contConst) {
        if (*a.contConst == *b.contConst) {
            out.contConst = a.contConst;
        } else {
            out.contName = *a.contConst ? c : emitter_.node("Not", {c});
        }
    } else {
        outputs.push_back(
            {{boolOperand(a), boolOperand(b)},
             TensorFacts{TypeCode::Bool, tensor::StaticShape{}},
             Value::symbolicScalar("", type::Type::Bool())});
        contAt = outputs.size() - 1;
    }
    std::vector<std::optional<size_t>> nextAt(loop.ports.size());
    out.next.resize(loop.ports.size());
    for (size_t k = 0; k < loop.ports.size(); ++k) {
        if (sameValue(a.next[k], b.next[k])) {
            out.next[k] = a.next[k];
        } else {
            if (!isCarriable(a.next[k]) || !isCarriable(b.next[k])) {
                unsupported(
                    act,
                    std::format(
                        "argument {} of a recursion whose depth depends on the model input "
                        "(counting from 1, `with` parameters first) is a tuple, struct or array "
                        "that a recursive call changes; a loop carries tensors and scalars: pass "
                        "the elements as separate arguments",
                        k + 1));
            }
            nextAt[k] = request(a.next[k], b.next[k]);
        }
    }
    // The result, leaf by leaf: a recursion may return a tuple or struct (nested) of tensors
    // and scalars. A leaf that is the same on both sides, or a constant next to a side that
    // has no result, needs no output of the If.
    std::vector<Value> resultLeaves;
    std::vector<std::optional<size_t>> resultAt;
    const Value *resultLike = nullptr;
    const auto leafKind     = [](const Value &leaf) -> std::string {
        if (leaf.isClosure() || (leaf.isConstant() && leaf.ty && leaf.ty->code() == TypeCode::Function)) {
            return "a function";
        }
        if (leaf.isSymbolic() && leaf.form == Value::Form::IntArray) {
            return "an int array whose elements depend on the model input";
        }
        if (leaf.isConstant() && leaf.ty) {
            return std::format("a value of type '{}'", leaf.ty->toString());
        }
        return "a value that is neither a tensor nor a scalar";
    };
    if (a.result && b.result) {
        std::vector<Value> la, lb;
        flattenValue(*a.result, la);
        flattenValue(*b.result, lb);
        if (la.size() != lb.size()) {
            unsupported(
                act,
                "the paths on which a recursion whose depth depends on the model input returns "
                "yield tuples or structs of different structure");
        }
        resultLike = &*a.result;
        resultAt.resize(la.size());
        for (size_t j = 0; j < la.size(); ++j) {
            if (sameValue(la[j], lb[j])) {
                resultLeaves.push_back(la[j]);
                continue;
            }
            const TensorFacts fa = factsOf(la[j]), fb = factsOf(lb[j]);
            if (!isCarriable(la[j]) || !isCarriable(lb[j]) || !fa.dtype || fa.dtype != fb.dtype) {
                const Value &bad = !isCarriable(la[j]) ? la[j] : lb[j];
                unsupported(
                    act,
                    std::format(
                        "element {} of the result of a recursion whose depth depends on the "
                        "model input differs between the paths that return and is {}; such an "
                        "element must be a tensor or a scalar (float, int, bool) of one type on "
                        "every path",
                        j,
                        !isCarriable(la[j]) || !isCarriable(lb[j]) ? leafKind(bad)
                                                                    : "of different types on them"));
            }
            resultAt[j] = request(la[j], lb[j]);
            resultLeaves.push_back(la[j]);
        }
    } else if (a.result || b.result) {
        // One side calls again and has no result. Its value is never looked at (the result is
        // read where the recursion returned), but an If needs it from both sides: zeros of
        // the same type and rank stand in, one per tensor or scalar the other side returns.
        resultLike = a.result ? &*a.result : &*b.result;
        flattenValue(*resultLike, resultLeaves);
        resultAt.resize(resultLeaves.size());
        for (size_t j = 0; j < resultLeaves.size(); ++j) {
            const Value &real       = resultLeaves[j];
            const TensorFacts facts = factsOf(real);
            if (real.isConstant() && !(real.ty && real.ty->code() == TypeCode::Function)) {
                continue; // known at export time: the same whatever the If selects
            }
            if (!isCarriable(real) || !facts.dtype) {
                unsupported(
                    act,
                    std::format(
                        "a recursion whose depth depends on the model input returns {} (element "
                        "{} of its result) on a path next to a recursive call; it can return "
                        "tensors and scalars (float, int, bool) and tuples or structs of them",
                        leafKind(real),
                        j));
            }
            std::vector<int64_t> dims;
            if (facts.shape) {
                for (int64_t extent : *facts.shape) {
                    dims.push_back(extent == tensor::kUnknownDim ? 1 : extent);
                }
            }
            std::string filler = emitter_.node("ConstantOfShape", {emitter_.int64s(dims)});
            if (*facts.dtype != TypeCode::Float32) {
                filler = emitter_.node(
                    "Cast",
                    {filler},
                    {Attribute::makeInt("to", static_cast<int64_t>(elemTypeOf(*facts.dtype)))});
            }
            const std::string name = emitter_.operand(real, *facts.dtype);
            outputs.push_back({{a.result ? name : filler, a.result ? filler : name}, facts, real});
            resultAt[j] = outputs.size() - 1;
        }
    }

    if (!outputs.empty()) {
        for (size_t i = 0; i < 2; ++i) {
            for (const Output &o : outputs) {
                bodies[i].outputs.push_back(
                    valueInfoOf(
                        emitter_.appendIdentity(bodies[i], o.names[i], "path_out"),
                        o.facts));
            }
        }
        const std::vector<std::string> names = emitter_.nodeN(
            "If",
            {c},
            {Attribute::makeGraph("then_branch", std::move(bodies[0])),
             Attribute::makeGraph("else_branch", std::move(bodies[1]))},
            outputs.size());
        if (contAt) {
            out.contName = names[*contAt];
        }
        for (size_t k = 0; k < loop.ports.size(); ++k) {
            if (nextAt[k]) {
                out.next[k] = placeholderLike(outputs[*nextAt[k]].like, names[*nextAt[k]]);
            }
        }
        for (size_t j = 0; j < resultAt.size(); ++j) {
            if (resultAt[j]) {
                resultLeaves[j] = placeholderLike(outputs[*resultAt[j]].like, names[*resultAt[j]]);
            }
        }
    }
    if (resultLike != nullptr) {
        size_t next = 0;
        out.result  = rebuildValue(*resultLike, resultLeaves, next);
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
        // Each side is evaluated in its own If subgraph, so nothing is computed on the side
        // that is not taken: a step taken speculatively past the end of the data is not
        // harmless (a slice outside its axis is empty, and the reshape after it fails).
        Graph bodies[2];
        BodyOutcome sides[2];
        for (size_t i = 0; i < 2; ++i) {
            emitter_.pushScope(i == 0 ? "then_branch" : "else_branch");
            act.memo.emplace_back();
            try {
                sides[i] = evalLoopBody(act, arms[i], loop);
                act.memo.pop_back();
                bodies[i] = emitter_.popScope();
            } catch (...) {
                act.memo.pop_back();
                emitter_.popScope();
                throw;
            }
        }
        return mergeOutcomes(act, cond, sides, bodies, loop);
    }
    default:
        break;
    }
    // A path that returns: the state stays, the value is the result.
    BodyOutcome out;
    out.contConst = false;
    out.next      = loop.inputs;
    out.returns   = true;
    if (loop.wantResult) {
        out.result = eval(act, ref);
    }
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
            if (!probe.returns) {
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
        loop.wantResult                 = false;
        const BodyOutcome outcome       = evaluate(inputs);
        loop.wantResult                 = true;
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

// ---------------------------------------------------------------- linear recursion as loops
//
// A recursion whose one recursive call is NOT the last action still unrolls into loops when it
// is linear: the function is
//
//     f(a) = E(a, if c(a) then E'(a, f(step(a))) else B(a))    (or with the arms swapped)
//
// with exactly one recursive call, on one side of one two-way branch (E and E' are treated as
// one: the body evaluated with that side forced and the call's result given). Write a_0 for the
// arguments of the first call, a_{i+1} = step(a_i), and n for the first i where the branch
// takes the other arm. Then, by induction on n,
//
//     V_n = E(a_n, B(a_n))        V_i = E(a_i, V_{i+1})        f(a_0) = V_0
//
// which is three loops: a forward Loop that finds a_n and n; the value V_n computed after it;
// and a Loop that runs n times and applies E in reverse order, i = n-1 .. 0, the state a_i
// being recomputed from a_0 by a counted inner Loop (no tape: the cost is quadratic in n).
// Nothing here depends on where the function comes from; the functions autodiff derives for a
// tail-recursive loop have this form (the forward function returns (value, pullback) and wraps
// the pullback of the recursive call in a closure).
//
// Functions as results. When V holds a function (a closure built by E that captures values of
// its call and the function returned by the recursive call), the function cannot be carried
// by a Loop. It is kept as an export-time value and the recursion is unrolled where it is
// CALLED: g_i(x) = P(a_i, x, g_{i+1}(x)), g_n = the closure of the last call, so
// g_0(x) is a bottom call g_n(x) followed by a Loop applying P in reverse order.
//
// Accepted, exactly:
//   - one recursive call in the function's own graph, and one if / else, on a scalar condition
//     that depends on the model input, with the call on exactly one side (branches inside that
//     side that use the call's result on both of their sides belong to E);
//   - the arguments the call changes are tensors or scalars of fixed dtype and shape;
//   - the result's leaves that are not functions are tensors or scalars of fixed dtype and
//     shape; when the result also holds a function, those leaves are passed through unchanged
//     by E (true of autodiff's (value, pullback));
//   - a function in the result calls the function returned by the recursive call exactly once
//     and with its own arguments unchanged, and returns tensors or scalars (tuples of them);
//     its arguments are tensors, scalars, constants, or tuples / structs of these (the pullback
//     of a recursion that returns several values takes one cotangent per differentiable one).
// Everything else is refused with a diagnostic.

std::vector<Value> Evaluator::emitLoop(
    Activation &act, const std::optional<std::string> &trip, const std::vector<Value> &init,
    const LoopBody &body) {
    if (init.empty()) {
        unsupported(act, "a recursion changes none of its values: it never ends, or it is no loop");
    }
    emitter_.pushScope("loop_body");
    std::vector<ValueInfo> inputs, outputs;
    try {
        const std::string iter   = emitter_.fresh("loop_iter");
        const std::string condIn = emitter_.fresh("loop_cond_in");
        std::vector<Value> state;
        for (const Value &v : init) {
            state.push_back(placeholderLike(v, emitter_.fresh("state")));
        }
        const auto [cont, next] = body(Value::symbolicScalar(iter, type::Type::Int64()), state);
        if (next.size() != init.size()) {
            unsupported(act, "the values a recursion carries are not the same on every path");
        }
        const TensorFacts flag{TypeCode::Bool, tensor::StaticShape{}};
        inputs.push_back(valueInfoOf(iter, TensorFacts{TypeCode::Int64, tensor::StaticShape{}}));
        inputs.push_back(valueInfoOf(condIn, flag));
        const std::string always = emitter_.operand(
            Value::constant(rtdata::toSlot<bool>(true), type::Type::Bool()),
            TypeCode::Bool);
        outputs.push_back(
            valueInfoOf(emitter_.node("Identity", {cont ? *cont : always}, {}, "loop_cond"), flag));
        for (size_t k = 0; k < init.size(); ++k) {
            const TensorFacts in  = factsOf(state[k]);
            const TensorFacts out = factsOf(next[k]);
            if (!isCarriable(next[k]) || !in.dtype || in.dtype != out.dtype || !in.shape ||
                !out.shape || *in.shape != *out.shape) {
                unsupported(
                    act,
                    "the dtype or shape of a value changes from one call of a recursion to the "
                    "next; a loop can only carry values of a fixed dtype and shape");
            }
            inputs.push_back(valueInfoOf(state[k].name, in));
            outputs.push_back(valueInfoOf(
                emitter_.node("Identity", {emitter_.operand(next[k], *in.dtype)}, {}, "state_out"),
                in));
        }
    } catch (...) {
        emitter_.popScope();
        throw;
    }
    Graph bodyGraph   = emitter_.popScope();
    bodyGraph.inputs  = std::move(inputs);
    bodyGraph.outputs = std::move(outputs);
    std::vector<std::string> loopInputs{
        trip ? *trip : std::string(),
        emitter_.operand(Value::constant(rtdata::toSlot<bool>(true), type::Type::Bool()), TypeCode::Bool)};
    for (const Value &v : init) {
        loopInputs.push_back(emitter_.operand(v, factsOf(v).dtype));
    }
    const std::vector<std::string> names = emitter_.nodeN(
        "Loop",
        std::move(loopInputs),
        {Attribute::makeGraph("body", std::move(bodyGraph))},
        init.size(),
        "loop_state");
    std::vector<Value> finals;
    for (size_t k = 0; k < init.size(); ++k) {
        finals.push_back(placeholderLike(init[k], names[k]));
    }
    return finals;
}

namespace {

/// The values of `full` at the positions marked in `mask`.
std::vector<Value> pick(std::span<const Value> full, const std::vector<bool> &mask) {
    std::vector<Value> out;
    for (size_t k = 0; k < full.size(); ++k) {
        if (mask[k]) {
            out.push_back(full[k]);
        }
    }
    return out;
}

/// `base` with the positions marked in `mask` replaced by `values`, in order.
std::vector<Value>
place(std::span<const Value> base, const std::vector<bool> &mask, std::span<const Value> values) {
    std::vector<Value> out(base.begin(), base.end());
    size_t next = 0;
    for (size_t k = 0; k < out.size(); ++k) {
        if (mask[k]) {
            out[k] = values[next++];
        }
    }
    return out;
}

} // namespace

Value Evaluator::linearBody(
    const LinearFrame &f, std::span<const Value> state, size_t arm, const Value *hole) {
    Activation body = activationOf(f.fn, state);
    const LinearCtx ctx{.frame = &f, .arm = arm, .hole = hole};
    body.linear = &ctx;
    ++closuresWanted_;
    try {
        Value v = eval(body, execute::resolveRuntimeTailValueRef(f.fn));
        --closuresWanted_;
        return v;
    } catch (...) {
        --closuresWanted_;
        throw;
    }
}

std::vector<Value> Evaluator::linearNext(const LinearFrame &f, std::span<const Value> state) {
    Activation body = activationOf(f.fn, state);
    const LinearCtx ctx{.frame = &f, .arm = f.recArm, .hole = nullptr};
    body.linear = &ctx;
    return callArguments(body, f.site, f.fn);
}

std::string
Evaluator::linearCont(Activation &act, const LinearFrame &f, std::span<const Value> state) {
    Activation body  = activationOf(f.fn, state);
    const Value cond = eval(body, f.cond);
    if (cond.isConstant() || cond.form != Value::Form::Scalar) {
        unsupported(
            act,
            "the condition that decides a recursion is not a scalar that depends on the model "
            "input");
    }
    const std::string c = emitter_.operand(cond, TypeCode::Bool);
    return f.recArm == 0 ? c : emitter_.node("Not", {c});
}

/// The arguments of call number `steps` (0 = the first call), recomputed from the first call's.
std::vector<Value>
Evaluator::linearReplay(Activation &act, const LinearFrame &f, const Value &steps) {
    const std::vector<Value> finals = emitLoop(
        act,
        emitter_.operand(steps, TypeCode::Int64),
        pick(f.args, f.carried),
        [&](const Value &, const std::vector<Value> &state) {
            const std::vector<Value> next = linearNext(f, place(f.args, f.carried, state));
            return std::pair<std::optional<std::string>, std::vector<Value>>{
                std::nullopt,
                pick(next, f.carried)};
        });
    return place(f.args, f.carried, finals);
}

Value Evaluator::translateLinear(Activation &act, GCGraph *fn, std::vector<Value> args) {
    const size_t frameIndex = linearFrames_.size();
    LinearFrame &f          = linearFrames_.emplace_back();
    f.fn                    = fn;
    f.ports.assign(fn->withPorts().begin(), fn->withPorts().end());
    f.ports.insert(f.ports.end(), fn->normPorts().begin(), fn->normPorts().end());
    f.args             = std::move(args);
    const size_t count = f.ports.size();

    // The one recursive call, and the branch it is an arm of.
    size_t calls = 0;
    for (auto it = fn->nodes().begin(); it != fn->nodes().end(); ++it) {
        if (*it == nullptr || (*it)->kind != GCNodeKind::Func) {
            continue;
        }
        const GCGraph *callee = fn->directCalleeGraphOf(it.ref());
        if (callee == fn) {
            f.site = it.ref();
            ++calls;
        } else if (callReaches(callee, fn)) {
            unsupported(
                act,
                "a recursion that goes through another function and is not in tail position "
                "cannot be exported");
        }
    }
    if (calls != 1) {
        unsupported(
            act,
            std::format(
                "a recursion whose depth depends on the model input calls the function again "
                "in {} places outside tail position; it can be exported as loops only with one "
                "recursive call (a linear recursion)",
                calls),
            ExportError::Kind::CallRemains);
    }
    // The branch that decides it: the one if / else with the call on exactly one side. (A branch
    // whose two sides both use the call's result is part of what is done with that result.)
    size_t deciding = 0;
    for (auto it = fn->nodes().begin(); it != fn->nodes().end(); ++it) {
        if (*it == nullptr || (*it)->kind != GCNodeKind::Join) {
            continue;
        }
        const auto arms = fn->withInputsOf(it.ref());
        size_t reaching = 0, which = 0;
        for (size_t i = 0; i < arms.size(); ++i) {
            if (nodeReachesCall(fn, arms[i], fn)) {
                ++reaching;
                which = i;
            }
        }
        if (reaching == 0 || reaching == arms.size()) {
            continue;
        }
        const gc_node_ref_t brch = fn->normInputsOf(it.ref()).front();
        if (arms.size() != 2 || !fn->withInputsOf(brch).empty()) {
            unsupported(act, "a recursion decided by a match cannot be exported; use if / else");
        }
        ++deciding;
        f.join   = it.ref();
        f.recArm = which;
        f.cond   = fn->normInputsOf(brch).front();
    }
    if (deciding != 1) {
        unsupported(
            act,
            deciding == 0
                ? "a recursive call that is not in tail position is made on every path; a "
                  "linear recursion can be exported only when one if / else decides whether "
                  "the function calls itself"
                : "several branches decide whether a function calls itself outside tail "
                  "position; a linear recursion can be exported only when one if / else "
                  "decides it",
            ExportError::Kind::CallRemains);
    }
    if (nodeReachesCall(fn, f.cond, fn)) {
        unsupported(
            act,
            "the condition that decides a recursion uses the result of the recursive call",
            ExportError::Kind::CallRemains);
    }

    // Which arguments does the recursive call change? (A discarded evaluation.)
    std::vector<bool> carriable(count, false);
    for (size_t k = 0; k < count; ++k) {
        carriable[k] = isCarriable(f.args[k]);
    }
    f.carried.assign(count, false);
    emitter_.pushScope("loop_probe");
    try {
        std::vector<Value> state = f.args;
        for (size_t k = 0; k < count; ++k) {
            if (carriable[k]) {
                state[k] = placeholderLike(f.args[k], emitter_.fresh("probe"));
            }
        }
        const std::vector<Value> next = linearNext(f, state);
        for (size_t k = 0; k < count; ++k) {
            f.carried[k] = !sameValue(next[k], state[k]);
            if (f.carried[k] && !carriable[k]) {
                unsupported(
                    act,
                    "a tuple, struct or array argument changes across a recursion whose depth "
                    "depends on the model input; only tensors and scalars can be carried by a "
                    "loop");
            }
        }
        emitter_.popScope();
    } catch (...) {
        emitter_.popScope();
        throw;
    }

    // Loop 1: the arguments of the last call and the number of recursive calls.
    std::vector<Value> init = pick(f.args, f.carried);
    const size_t m          = init.size();
    init.push_back(Value::constant(rtdata::toSlot<rtdata::Int64>(0), type::Type::Int64()));
    const std::vector<Value> last = emitLoop(
        act,
        std::nullopt,
        init,
        [&](const Value &, const std::vector<Value> &state) {
            const std::vector<Value> full =
                place(f.args, f.carried, std::span<const Value>(state.data(), m));
            const std::string again = linearCont(act, f, full);
            // The step is taken only when the function calls itself again: computed in the
            // then-side of an If, never speculatively (see mergeOutcomes).
            Graph sides[2];
            std::vector<std::string> stepped, kept;
            emitter_.pushScope("then_branch");
            try {
                const std::vector<Value> next = pick(linearNext(f, full), f.carried);
                for (size_t k = 0; k < m; ++k) {
                    const TensorFacts in = factsOf(state[k]), out = factsOf(next[k]);
                    if (!isCarriable(next[k]) || !in.dtype || in.dtype != out.dtype) {
                        unsupported(
                            act,
                            "the dtype of a value changes from one call of a recursion to the "
                            "next; a loop can only carry values of a fixed dtype and shape");
                    }
                    stepped.push_back(emitter_.operand(next[k], *in.dtype));
                }
                const int64_t one[] = {1};
                stepped.push_back(
                    emitter_.node(
                        "Add",
                        {emitter_.operand(state[m], TypeCode::Int64),
                         emitter_.node("Squeeze", {emitter_.int64s(one)})}));
                sides[0] = emitter_.popScope();
            } catch (...) {
                emitter_.popScope();
                throw;
            }
            emitter_.pushScope("else_branch");
            sides[1] = emitter_.popScope();
            for (size_t k = 0; k <= m; ++k) {
                kept.push_back(emitter_.operand(state[k], factsOf(state[k]).dtype));
            }
            for (size_t k = 0; k <= m; ++k) {
                const TensorFacts facts = factsOf(state[k]);
                sides[0].outputs.push_back(
                    valueInfoOf(emitter_.appendIdentity(sides[0], stepped[k], "path_out"), facts));
                sides[1].outputs.push_back(
                    valueInfoOf(emitter_.appendIdentity(sides[1], kept[k], "path_out"), facts));
            }
            const std::vector<std::string> names = emitter_.nodeN(
                "If",
                {again},
                {Attribute::makeGraph("then_branch", std::move(sides[0])),
                 Attribute::makeGraph("else_branch", std::move(sides[1]))},
                m + 1);
            std::vector<Value> out;
            for (size_t k = 0; k <= m; ++k) {
                out.push_back(placeholderLike(state[k], names[k]));
            }
            return std::pair<std::optional<std::string>, std::vector<Value>>{again, out};
        });
    f.finals = place(f.args, f.carried, std::span<const Value>(last.data(), m));
    f.count  = last[m];

    // The value of the last call.
    f.base = linearBody(f, f.finals, 1 - f.recArm, nullptr);
    std::vector<Value> leaves;
    flattenValue(f.base, leaves);
    const auto holeLike = [&](const std::vector<Value> &plain) {
        // The recursive call's result: `plain` for the leaves that are values, a hole for
        // each function.
        std::vector<Value> holes;
        for (size_t j = 0; j < leaves.size(); ++j) {
            if (leaves[j].isClosure()) {
                Value hole      = Value::closure(Value::ClosureKind::Hole, leaves[j].camelType);
                hole.closureRef = j;
                holes.push_back(std::move(hole));
            } else {
                holes.push_back(plain[j]);
            }
        }
        size_t next = 0;
        return rebuildValue(f.base, holes, next);
    };

    // Which leaves does the enclosing expression change? (A discarded evaluation.)
    std::vector<bool> changed(leaves.size(), false);
    bool anyClosure = false, anyChanged = false;
    emitter_.pushScope("loop_probe");
    try {
        std::vector<Value> state = f.args;
        for (size_t k = 0; k < count; ++k) {
            if (f.carried[k]) {
                state[k] = placeholderLike(f.args[k], emitter_.fresh("probe"));
            }
        }
        std::vector<Value> plain(leaves.size());
        for (size_t j = 0; j < leaves.size(); ++j) {
            if (leaves[j].isClosure()) {
                anyClosure = true;
                continue;
            }
            if (!isCarriable(leaves[j])) {
                unsupported(
                    act,
                    "the result of a linear recursion holds something other than tensors, "
                    "scalars and functions");
            }
            plain[j] = placeholderLike(leaves[j], emitter_.fresh("probe"));
        }
        const Value hole = holeLike(plain);
        std::vector<Value> result;
        flattenValue(linearBody(f, state, f.recArm, &hole), result);
        if (result.size() != leaves.size()) {
            unsupported(act, "the two paths of a linear recursion return values of different structure");
        }
        for (size_t j = 0; j < leaves.size(); ++j) {
            if (leaves[j].isClosure() != result[j].isClosure()) {
                unsupported(act, "the two paths of a linear recursion return values of different structure");
            }
            if (!leaves[j].isClosure()) {
                changed[j] = !sameValue(result[j], plain[j]);
                anyChanged = anyChanged || changed[j];
            }
        }
        emitter_.popScope();
    } catch (...) {
        emitter_.popScope();
        throw;
    }
    if (anyClosure && anyChanged) {
        unsupported(
            act,
            "a linear recursion returns a function together with a value that it changes on "
            "the way back; only a value passed through unchanged can accompany a function");
    }

    // Loop 2: the enclosing expression applied in reverse order, n times.
    if (anyChanged) {
        const std::vector<Value> ups = emitLoop(
            act,
            emitter_.operand(f.count, TypeCode::Int64),
            pick(leaves, changed),
            [&](const Value &iter, const std::vector<Value> &current) {
                // Iteration j handles call number n - 1 - j.
                const int64_t one[] = {1};
                const Value index   = Value::symbolicScalar(
                    emitter_.node(
                        "Sub",
                        {emitter_.node(
                             "Sub",
                             {emitter_.operand(f.count, TypeCode::Int64),
                              emitter_.node("Squeeze", {emitter_.int64s(one)})}),
                         iter.name}),
                    type::Type::Int64());
                const std::vector<Value> state = linearReplay(act, f, index);
                const Value hole               = holeLike(place(leaves, changed, current));
                std::vector<Value> result;
                flattenValue(linearBody(f, state, f.recArm, &hole), result);
                return std::pair<std::optional<std::string>, std::vector<Value>>{
                    std::nullopt,
                    pick(result, changed)};
            });
        leaves = place(leaves, changed, ups);
    }
    for (size_t j = 0; j < leaves.size(); ++j) {
        if (leaves[j].isClosure()) {
            Value fnValue      = Value::closure(Value::ClosureKind::Loop, leaves[j].camelType);
            fnValue.closureRef = loopClosures_.size();
            loopClosures_.push_back({frameIndex, j});
            leaves[j] = std::move(fnValue);
        }
    }
    size_t next = 0;
    return rebuildValue(f.base, leaves, next);
}

// ---------------------------------------------------------------- functions as values

std::optional<Value> Evaluator::probeSpecialClosure(Activation &act, gc_node_ref_t ref) {
    const auto special = [](const Value &v, const auto &self) -> bool {
        if (v.isClosure()) {
            return v.closureKind != Value::ClosureKind::Plain ||
                   std::ranges::any_of(v.fields, [&](const Value &c) { return self(c, self); });
        }
        return v.isAggregate() &&
               std::ranges::any_of(v.fields, [&](const Value &c) { return self(c, self); });
    };
    bool found = false;
    emitter_.pushScope("closure_probe");
    act.memo.emplace_back();
    ++closuresWanted_;
    try {
        found = special(eval(act, ref), special);
    } catch (const ExportError &) {
        found = false; // not evaluable: stays deferred, and fails only if it is ever needed
    }
    --closuresWanted_;
    act.memo.pop_back();
    emitter_.popScope();
    if (!found) {
        return std::nullopt;
    }
    ++closuresWanted_;
    try {
        Value v = eval(act, ref);
        --closuresWanted_;
        return v;
    } catch (...) {
        --closuresWanted_;
        throw;
    }
}

Value Evaluator::mergeValues(
    Activation &act, const std::string &cond, const Value &a, const Value &b) {
    std::vector<Value> la, lb;
    flattenValue(a, la);
    flattenValue(b, lb);
    if (la.size() != lb.size()) {
        unsupported(
            act,
            "the arms of a branch on a model-dependent condition yield different types");
    }
    std::vector<Value> merged;
    for (size_t j = 0; j < la.size(); ++j) {
        if (la[j].isClosure() || lb[j].isClosure()) {
            if (sameValue(la[j], lb[j])) {
                merged.push_back(la[j]);
                continue;
            }
            if (!la[j].isClosure() || !lb[j].isClosure()) {
                unsupported(act, "one arm of a branch yields a function and the other does not");
            }
            // (if c then f else g)(x) = if c then f(x) else g(x): decided when it is called.
            Value select  = Value::closure(Value::ClosureKind::Select, la[j].camelType);
            select.name   = cond;
            select.fields = {la[j], lb[j]};
            merged.push_back(std::move(select));
        } else {
            merged.push_back(selectValue(act, cond, la[j], lb[j]));
        }
    }
    size_t next = 0;
    return rebuildValue(a, merged, next);
}

Value Evaluator::evalEagerBranch(
    Activation &act, const Value &cond, std::span<const gc_node_ref_t> arms) {
    ++closuresWanted_;
    try {
        const Value a = eval(act, arms[0]);
        const Value b = eval(act, arms[1]);
        --closuresWanted_;
        return mergeValues(act, emitter_.operand(cond, TypeCode::Bool), a, b);
    } catch (...) {
        --closuresWanted_;
        throw;
    }
}

Value Evaluator::evalIndirectCall(Activation &act, gc_node_ref_t ref) {
    GCGraph *g      = act.graph;
    const auto with = g->withInputsOf(ref);
    Value callee;
    if (!with.empty()) {
        ++closuresWanted_;
        try {
            callee = eval(act, with.front());
            --closuresWanted_;
        } catch (...) {
            --closuresWanted_;
            throw;
        }
    }
    if (with.empty() || !callee.isClosure()) {
        unsupported(
            act,
            "an indirect call remains after simplification; its callee must be known at export "
            "time",
            ExportError::Kind::CallRemains);
    }
    std::vector<Value> args = evalAll(act, with.subspan(1));
    const size_t withCount  = args.size();
    for (Value &v : evalAll(act, g->normInputsOf(ref))) {
        args.push_back(std::move(v));
    }
    return callClosure(act, callee, args, withCount);
}

Value Evaluator::callClosure(
    Activation &act, const Value &fn, const std::vector<Value> &args, size_t withCount) {
    switch (fn.closureKind) {
    case Value::ClosureKind::Hole:
        if (holeCall_ == nullptr) {
            unsupported(
                act,
                "the function returned by a recursive call is called while the recursion's "
                "own value is being computed; this form of recursion cannot be exported");
        }
        return (*holeCall_)(fn.closureRef, args);
    case Value::ClosureKind::Loop:
        return callLoopClosure(act, fn, args, withCount);
    case Value::ClosureKind::Select: {
        const Value a = callClosure(act, fn.fields[0], args, withCount);
        const Value b = callClosure(act, fn.fields[1], args, withCount);
        return mergeValues(act, fn.name, a, b);
    }
    case Value::ClosureKind::Plain:
        break;
    }
    auto *function = rtdata::fromSlot<::Function *>(fn.slot);
    GCGraph *graph = function ? function->graph() : nullptr;
    if (graph == nullptr) {
        unsupported(act, "a call of a function without a body cannot be exported");
    }
    const auto withPorts = graph->withPorts();
    const auto normPorts = graph->normPorts();
    const auto captured  = graph->closureNodes();
    if (withCount != withPorts.size() || args.size() - withCount != normPorts.size() ||
        captured.size() != fn.fields.size()) {
        unsupported(act, "a call does not pass one argument per parameter of its callee");
    }
    Activation inner{.graph = graph, .ports = {}, .memo = {{}}};
    for (size_t i = 0; i < withPorts.size(); ++i) {
        inner.ports.emplace(withPorts[i], args[i]);
    }
    for (size_t i = 0; i < normPorts.size(); ++i) {
        inner.ports.emplace(normPorts[i], args[withCount + i]);
    }
    for (size_t i = 0; i < captured.size(); ++i) {
        inner.ports.emplace(captured[i], fn.fields[i]);
    }
    ++closuresWanted_;
    try {
        Value v = eval(inner, execute::resolveRuntimeTailValueRef(graph));
        --closuresWanted_;
        return v;
    } catch (...) {
        --closuresWanted_;
        throw;
    }
}

void Evaluator::refuseLinear(const LinearFrame &f, std::string_view what) {
    // A condition on the function a recursion returns is reported at the recursive call, not
    // at the place where that function happens to be called (for a pullback: the
    // value_and_grad expression). Without a position there, the caller's node locates it.
    const std::string graph = f.fn->name().substr(0, f.fn->name().find('$'));
    const std::string text =
        isSourceName(graph) ? std::format("in '{}': {}", graph, what) : std::string(what);
    const uint64_t origin = f.fn->nodeOrigin(f.site);
    throw ExportError(text, locationOf(origin), origin, ExportError::Kind::Other);
}

Value Evaluator::callLoopClosure(
    Activation &act, const Value &fn, const std::vector<Value> &args, size_t withCount) {
    const LoopClosureRef where = loopClosures_.at(fn.closureRef);
    const LinearFrame &f       = linearFrames_.at(where.frame);
    // The arguments (the cotangents, for a pullback) may be tuples or structs: they are not
    // carried by a Loop, every call of the chain receives them unchanged. Their leaves must be
    // values a Loop body can read: tensors, scalars and export-time constants.
    for (size_t k = 0; k < args.size(); ++k) {
        std::vector<Value> leaves;
        flattenValue(args[k], leaves);
        for (const Value &leaf : leaves) {
            if (leaf.isClosure() || (leaf.isSymbolic() && !isCarriable(leaf))) {
                refuseLinear(
                    f,
                    std::format(
                        "the function returned by a linear recursion is called with {} "
                        "(argument {}); its arguments can be tensors, scalars, constants and "
                        "tuples or structs of them",
                        leaf.isClosure() ? "a function"
                                         : "an array whose elements depend on the model input",
                        k));
            }
        }
    }
    std::vector<Value> baseLeaves;
    flattenValue(f.base, baseLeaves);
    // The recursive call's result as the function sees it: its values are those of the last
    // call (they are passed through unchanged), its functions are holes.
    std::vector<Value> holes = baseLeaves;
    for (size_t j = 0; j < holes.size(); ++j) {
        if (holes[j].isClosure()) {
            holes[j]            = Value::closure(Value::ClosureKind::Hole, baseLeaves[j].camelType);
            holes[j].closureRef = j;
        }
    }
    size_t at        = 0;
    const Value hole = rebuildValue(f.base, holes, at);
    const auto closureAt = [&](std::span<const Value> state) {
        std::vector<Value> result;
        flattenValue(linearBody(f, state, f.recArm, &hole), result);
        return result.at(where.leaf);
    };
    const HoleCall *outer = holeCall_;
    const auto withHole   = [&](const HoleCall &handler, const auto &run) {
        holeCall_ = &handler;
        try {
            auto v    = run();
            holeCall_ = outer;
            return v;
        } catch (...) {
            holeCall_ = outer;
            throw;
        }
    };

    // A discarded evaluation: what the bottom call returns (its structure and types), how often
    // the function calls the one below it, and with what.
    size_t calls = 0;
    bool sameArguments = true;
    std::vector<Value> shape; // leaves of the result, as placeholders
    Value shapeLike;
    emitter_.pushScope("loop_probe");
    try {
        std::vector<Value> state = f.args;
        for (size_t k = 0; k < state.size(); ++k) {
            if (f.carried[k]) {
                state[k] = placeholderLike(f.args[k], emitter_.fresh("probe"));
            }
        }
        std::vector<Value> probeArgs;
        for (const Value &a : args) {
            std::vector<Value> leaves;
            flattenValue(a, leaves);
            for (Value &leaf : leaves) {
                if (leaf.isSymbolic()) {
                    leaf = placeholderLike(leaf, emitter_.fresh("probe"));
                }
            }
            size_t next = 0;
            probeArgs.push_back(rebuildValue(a, leaves, next));
        }
        const HoleCall none = [&](size_t, const std::vector<Value> &) -> Value {
            refuseLinear(f, "the function of the call that ends a recursion calls a function of the recursion");
        };
        shapeLike = withHole(none, [&] { return callClosure(act, baseLeaves.at(where.leaf), probeArgs, withCount); });
        flattenValue(shapeLike, shape);
        for (Value &leaf : shape) {
            if (!isCarriable(leaf)) {
                refuseLinear(
                    f,
                    "the function returned by a linear recursion returns something other than "
                    "tensors and scalars");
            }
            leaf = placeholderLike(leaf, emitter_.fresh("probe"));
        }
        const HoleCall count = [&](size_t leaf, const std::vector<Value> &inner) -> Value {
            if (leaf != where.leaf) {
                refuseLinear(f, "a function of a linear recursion calls another function of the same recursion");
            }
            ++calls;
            sameArguments = sameArguments && inner.size() == probeArgs.size();
            for (size_t k = 0; sameArguments && k < inner.size(); ++k) {
                sameArguments = sameValue(inner[k], probeArgs[k]);
            }
            size_t next = 0;
            return rebuildValue(shapeLike, shape, next);
        };
        std::vector<Value> result;
        flattenValue(
            withHole(count, [&] { return callClosure(act, closureAt(state), probeArgs, withCount); }),
            result);
        if (calls > 0 && result.size() != shape.size()) {
            refuseLinear(f, "the functions of a linear recursion return values of different structure");
        }
        emitter_.popScope();
    } catch (...) {
        emitter_.popScope();
        throw;
    }
    if (calls == 0) {
        // It never calls the function below it: the first call's function is the whole answer.
        const HoleCall none = [&](size_t, const std::vector<Value> &) -> Value {
            refuseLinear(f, "a function of a linear recursion calls the one below it on some paths only");
        };
        return withHole(none, [&] { return callClosure(act, closureAt(f.args), args, withCount); });
    }
    if (calls != 1 || !sameArguments) {
        refuseLinear(
            f,
            "the function returned by a linear recursion calls the function of the next call "
            "more than once or with changed arguments; only one call with the same arguments "
            "can be exported");
    }

    // Bottom: the function of the last call.
    const HoleCall none = [&](size_t, const std::vector<Value> &) -> Value {
        refuseLinear(f, "the function of the call that ends a recursion calls a function of the recursion");
    };
    const Value bottom =
        withHole(none, [&] { return callClosure(act, baseLeaves.at(where.leaf), args, withCount); });
    std::vector<Value> bottomLeaves;
    flattenValue(bottom, bottomLeaves);

    // Up: the functions of calls n-1 .. 0, each applied to the result of the one below.
    const std::vector<Value> ups = emitLoop(
        act,
        emitter_.operand(f.count, TypeCode::Int64),
        bottomLeaves,
        [&](const Value &iter, const std::vector<Value> &current) {
            const int64_t one[] = {1};
            const Value index   = Value::symbolicScalar(
                emitter_.node(
                    "Sub",
                    {emitter_.node(
                         "Sub",
                         {emitter_.operand(f.count, TypeCode::Int64),
                          emitter_.node("Squeeze", {emitter_.int64s(one)})}),
                     iter.name}),
                type::Type::Int64());
            const std::vector<Value> state = linearReplay(act, f, index);
            const HoleCall below = [&](size_t, const std::vector<Value> &) -> Value {
                size_t next = 0;
                return rebuildValue(bottom, current, next);
            };
            std::vector<Value> result;
            flattenValue(
                withHole(below, [&] { return callClosure(act, closureAt(state), args, withCount); }),
                result);
            return std::pair<std::optional<std::string>, std::vector<Value>>{std::nullopt, result};
        });
    size_t next = 0;
    return rebuildValue(bottom, ups, next);
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
    return variableValueAt(act, cell, ref);
}

/// The content of the local variable `cell` as the node `at` finds it: the value written by the
/// last assignment that must run before `at`. A compound assignment (`+=`) writes the content it
/// finds combined with its operand.
Value Evaluator::variableValueAt(Activation &act, gc_node_ref_t cell, gc_node_ref_t at) {
    GCGraph *g = act.graph;
    std::vector<gc_node_ref_t> before;
    for (const auto users : {g->normOutputsOf(cell), g->withOutputsOf(cell)}) {
        for (gc_node_ref_t user : users) {
            if (user == at) {
                continue;
            }
            const std::string_view uri = operUriOf(g, user);
            if (uri == ":var/get") {
                continue;
            }
            if (!assignmentKindOf(uri).isAssignment || g->normInputsOf(user).size() != 2 ||
                g->normInputsOf(user).front() != cell) {
                unsupported(
                    act,
                    "a variable of the exported function is passed to a call, a branch arm or "
                    "a closure; only a variable that is read and assigned directly in one "
                    "function can be exported");
            }
            if (runsBefore(g, user, at)) {
                before.push_back(user);
            } else if (!runsBefore(g, at, user)) {
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
        if (!isLast) {
            continue;
        }
        const Value operand        = eval(act, g->normInputsOf(last)[1]);
        const AssignmentKind kind  = assignmentKindOf(operUriOf(g, last));
        if (kind.binary.empty()) {
            return operand;
        }
        const Value old = variableValueAt(act, cell, last);
        return applyOper(act, kind.binary, {old, operand}, g->node(last)->dataType);
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
    return applyOper(act, uri, std::move(norm), g->node(ref)->dataType, std::move(with));
}

/// The value of operator `uri` applied to `norm` (and `with`): folded when the arguments are
/// constants, lowered to ONNX otherwise.
Value Evaluator::applyOper(
    Activation &act, const std::string &uri, std::vector<Value> norm, type::Type *resultType,
    std::vector<Value> with) {
    const auto *def = tensor::ops::OpRegistry::instance().find(uri);

    // Simplification has evaluated every pure operator whose arguments are constants; what is
    // left is translated. Side effects have no ONNX counterpart.
    if (!core::OperatorTraitsRegistry::instance().isPure(uri)) {
        unsupported(act, std::format("impure operator '{}' cannot be exported", uri));
    }
    if (allConstant(norm) && allConstant(with) && !std::ranges::any_of(norm, isDeferredClosure) &&
        !std::ranges::any_of(with, isDeferredClosure)) {
        if (auto folded = foldConstantOper(uri, norm, with, resultType)) {
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
            LowerContext(emitter_, uri, norm, std::move(facts), resultType));
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
    if (act.linear != nullptr && g == act.linear->frame->fn && join == act.linear->frame->join) {
        return eval(act, arms[act.linear->arm]);
    }
    const auto caseRefs      = g->withInputsOf(brch);
    const Value cond         = eval(act, g->normInputsOf(brch).front());
    if (!cond.isConstant()) {
        if (!caseRefs.empty() || arms.size() != 2 || cond.form != Value::Form::Scalar) {
            unsupported(
                act,
                "a match on a value that depends on the model input cannot be exported");
        }
        type::Type *joinType = g->node(join)->dataType;
        if (joinType && joinType->code() == TypeCode::Function) {
            refuseFunctionBranch(act, joinType);
        }
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

void Evaluator::refuseFunctionBranch(Activation &act, type::Type *functionType) {
    // An If yields tensors. A function is no value of the model, and which function is called
    // would only be known when the model runs. (The exception, handled before this is
    // reached: the function a recursion with one recursive call returns, see translateLinear.)
    unsupported(
        act,
        std::format(
            "a branch on a condition that depends on the model input chooses between "
            "functions{}: a model has no function values, so which function is called cannot "
            "be decided when the model runs. Call the functions inside the arms and let the "
            "branch choose between their results",
            functionType ? std::format(" (its value is a '{}')", functionType->toString())
                         : std::string()));
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
            if (results[i].isClosure() ||
                (results[i].isConstant() && results[i].ty &&
                 results[i].ty->code() == TypeCode::Function)) {
                refuseFunctionBranch(
                    act,
                    results[i].isClosure() ? results[i].camelType : results[i].ty);
            }
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
    if (holdsClosure(results[0]) || holdsClosure(results[1])) {
        // An arm yields a function that exists only at export time (what a linear recursion
        // returns). Its captured values must stay visible where it is called, so the arms
        // cannot live in If subgraphs: both are evaluated here and the condition selects.
        Value cond        = Value::symbolicScalar(condName, type::Type::Bool());
        return evalEagerBranch(act, cond, arms);
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
            // A function kept as a value where a linear recursion needs it: always inside
            // one, and elsewhere only when it is (or holds) the function such a recursion
            // returned. Otherwise it stays deferred, as before.
            if (closuresWanted_ > 0) {
                values[k] = eval(act, refs[k]);
                allConst  = allConst && values[k].isConstant();
                continue;
            }
            if (auto special = probeSpecialClosure(act, refs[k])) {
                values[k] = std::move(*special);
                allConst  = false;
                continue;
            }
            values[k]   = Value::constant(NullSlot, elemType);
            sawDeferred = true;
            continue;
        }
        values[k] = eval(act, refs[k]);
        allConst  = allConst && values[k].isConstant();
    }
    // (Also when every element is a constant: simplification folds an array literal only where
    // replacing it is execution-safe, and one inside a branch arm, e.g. `[k, k + 1]` in the
    // first, specialised call of a recursion, reaches the export unfolded.)
    if (source.isConstant() && isIntArrayType(targetType)) {
        return symbolicIntArray(act, ref, source, values);
    }
    if (source.isConstant() && isFloatArrayType(targetType)) {
        return symbolicFloatArray(act, ref, source, values);
    }
    if (source.isConstant() && isAggregate) {
        // A tuple or struct of input-dependent values (e.g. gradients) exists only at export
        // time; its elements are read back by accesses or become graph outputs. One whose
        // elements are all constants and that simplification left in a branch arm (the zero
        // gradient of the path on which a loop takes no step) is kept the same way.
        std::vector<Value> fields = elementsOf(source.slot, targetType);
        for (size_t k = 0; k < slots.size(); ++k) {
            fields[static_cast<size_t>(slots[k])] = values[k];
        }
        return Value::aggregate(targetType, std::move(fields));
    }
    if (source.isConstant() && targetType->code() == TypeCode::Function &&
        (closuresWanted_ > 0 || std::ranges::any_of(values, [](const Value &v) { return v.isClosure(); }))) {
        // A closure: the function and its captured values, kept until it is called.
        auto *fn = rtdata::fromSlot<::Function *>(source.slot);
        if (fn == nullptr || fn->graph() == nullptr) {
            unsupported(act, "a closure without a function body cannot be exported");
        }
        const auto closureNodes = fn->graph()->closureNodes();
        Value closure           = Value::closure(Value::ClosureKind::Plain, targetType);
        closure.slot            = source.slot;
        closure.fields.resize(closureNodes.size());
        if (const auto *captured = fn->tuple()) {
            for (size_t i = 0; i < closureNodes.size() && i < captured->size(); ++i) {
                closure.fields[i] = Value::constant(
                    captured->get<slot_t>(i),
                    fn->graph()->node(closureNodes[i])->dataType);
            }
        }
        for (size_t k = 0; k < slots.size(); ++k) {
            const size_t slot = static_cast<size_t>(slots[k]);
            if (slot >= closure.fields.size()) {
                unsupported(act, "a closure captures more values than its function declares");
            }
            closure.fields[slot] = values[k];
        }
        return closure;
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
    /// `sharedBatch`: whether dynamic axis 0 is named "batch" (one tensor argument) or gets a
    /// symbolic size per argument (several: see bindInputs).
    InputBinder(Emitter &emitter, const ExportOptions &options, bool sharedBatch = true)
        : emitter_(emitter), options_(options), sharedBatch_(sharedBatch) {}

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
                    Dim{a == 0 && sharedBatch_ ? std::string("batch")
                                               : std::format("{}_dim{}", name, a)};
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
    bool sharedBatch_ = true;
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

/// Whether a value of type `have` (the exact type of an example) can be the argument of a
/// parameter declared `want`: a tensor where a tensor is declared, of an element type and a
/// shape the declaration does not contradict, and the same through tuples and structs. Only
/// what the two types contradict is refused; a declaration that says less than the example
/// (a plain `Tensor`) accepts it.
bool exampleFits(type::Type *want, type::Type *have) {
    if (!want || !have || want == have) {
        return true;
    }
    if (tensor::asTensorType(want)) {
        return tensor::asTensorType(have) &&
               want->castSafetyFrom(have) != type::CastSafety::Forbidden;
    }
    if (want->code() == TypeCode::Tuple) {
        auto *w = static_cast<type::TupleType *>(want);
        if (have->code() != TypeCode::Tuple) {
            return false;
        }
        auto *h = static_cast<type::TupleType *>(have);
        if (w->size() != h->size()) {
            return false;
        }
        for (size_t i = 0; i < w->size(); ++i) {
            if (!exampleFits(w->typeAt(i), h->typeAt(i))) {
                return false;
            }
        }
        return true;
    }
    if (want->code() == TypeCode::Struct) {
        if (have->code() != TypeCode::Struct) {
            return false;
        }
        auto *w = static_cast<type::StructType *>(want);
        auto *h = static_cast<type::StructType *>(have);
        for (size_t i = 0; i < w->size(); ++i) {
            const auto field = h->findField(w->fieldName(i));
            if (!field || !exampleFits(w->typeAt(i), h->typeAt(*field))) {
                return false;
            }
        }
        return true;
    }
    return true;
}

/// Refuses an example that cannot be the argument of the parameter it is given for. The
/// function is specialized for the examples' types whatever its parameters declare, so without
/// this a tuple given where a tensor is declared surfaces later, as a failure of whatever
/// operator first uses the parameter.
void checkExamples(GCGraph *graph, std::span<type::Type *const> exact) {
    const auto *funcType = graph->funcType();
    if (!funcType) {
        return;
    }
    const std::string fn = graph->name().substr(0, graph->name().find('$'));
    for (size_t i = 0; i < exact.size() && i < funcType->normTypesCount(); ++i) {
        type::Type *declared = funcType->normTypeAt(i);
        if (exampleFits(declared, exact[i])) {
            continue;
        }
        const size_t nameIndex = funcType->withTypesCount() + i;
        const std::string param =
            nameIndex < funcType->argNamesCount()
                ? std::format("parameter '{}'", std::string(funcType->argNameAt(nameIndex)))
                : std::format("parameter {}", i);
        std::string hint;
        if (exact.size() == 1 && exact[0]->code() == TypeCode::Tuple) {
            auto *tuple = static_cast<type::TupleType *>(exact[0]);
            if (tuple->size() == 1 && exampleFits(declared, tuple->typeAt(0))) {
                hint = "; the example of a function of one parameter is its argument itself, "
                       "not a tuple of arguments: pass the value, not a tuple that holds it";
            }
        }
        throw ExportError(
            std::format(
                "the example given for {}{} is a '{}', but the parameter is declared '{}'{}",
                param,
                isSourceName(fn) ? std::format(" of '{}'", fn) : std::string(),
                exact[i]->toString(),
                declared->toString(),
                hint));
    }
}

struct BoundInputs {
    std::vector<Value> values;
    std::vector<type::Type *> types; // the exact type of each input (dtypes and shapes)
};

/// Declares one graph input per tensor of `examples` and returns the parameters' values.
BoundInputs
bindInputs(Emitter &emitter, const ExportOptions &options, std::span<const Example> examples) {
    // Dynamic axis 0 is the symbolic size "batch" when the function has one tensor argument.
    // With several, the option cannot say which of them are data: weights passed next to the
    // data would be declared to have the batch's extent (a model that loads and then fails on
    // ONNX Runtime at other batch sizes), so each argument gets its own symbolic size and
    // nothing is claimed about their equality.
    size_t batched = 0;
    for (const Example &example : examples) {
        if (!tensor::asTensorType(example.type) &&
            !(example.type && example.type->code() == tensor::TensorType::typeCode())) {
            continue;
        }
        const auto *t = rtdata::fromSlot<tensor::TensorObject *>(example.value);
        batched += t->shapeSpan().empty() ? 0 : 1;
    }
    const bool sharedBatch = batched <= 1;
    InputBinder binder(emitter, options, sharedBatch);
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

/// The type of a constant with everything its value fixes: a tensor's element type and shape,
/// through structs and tuples. `declared` itself when the value adds nothing to it. The static
/// type of a captured value says what the program declared (`{ w: Tensor }` for weights read
/// from a file), and types only flow into the functions a constant is passed to (an object is
/// not bound into its callee): with the declared type the body of a loop knows less about the
/// weights than the place that calls it, every level of the recursion the simplification
/// unrolls there is again more precisely typed than the loop, and the unrolling never ends.
type::Type *exactTypeOf(slot_t value, type::Type *declared) {
    if (!declared || value == NullSlot) {
        return declared;
    }
    if (declared->code() == tensor::TensorType::typeCode()) {
        const auto *t = rtdata::fromSlot<tensor::TensorObject *>(value);
        return tensor::TensorType::get(
            t->dtype(),
            tensor::StaticShape(t->shapeSpan().begin(), t->shapeSpan().end()));
    }
    if (declared->code() == TypeCode::Struct) {
        auto *structType = static_cast<type::StructType *>(declared);
        auto *object     = rtdata::fromSlot<::Struct *>(value);
        type::StructTypeFactory factory;
        bool changed = false;
        for (size_t i = 0; i < structType->size(); ++i) {
            type::Type *field = exactTypeOf(object->get<slot_t>(i), structType->typeAt(i));
            changed           = changed || field != structType->typeAt(i);
            factory.add(std::string(structType->fieldName(i)), field);
        }
        return changed ? factory.build() : declared;
    }
    if (declared->code() == TypeCode::Tuple) {
        auto *tupleType = static_cast<type::TupleType *>(declared);
        auto *object    = rtdata::fromSlot<::Tuple *>(value);
        if (tupleType->isCell() || object->size() != tupleType->size()) {
            return declared;
        }
        std::vector<type::Type *> types;
        bool changed = false;
        for (size_t i = 0; i < tupleType->size(); ++i) {
            types.push_back(exactTypeOf(object->get<slot_t>(i), tupleType->typeAt(i)));
            changed = changed || types.back() != tupleType->typeAt(i);
        }
        return changed ? type::TupleType::create(std::move(types)) : declared;
    }
    return declared;
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
            exactTypeOf(captures[i].value, captures[i].type),
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

/// The second half of every export: simplifies `root` (a graph whose parameters already have
/// the types of `inputs`), translates it with its parameters bound to `inputs` and declares
/// the outputs. `names` are the source names of constant objects, for the initializers.
Model translatePrepared(
    core::context::Context &ctx, Emitter &emitter, GCGraph *root, std::span<const Value> inputs,
    std::unordered_map<slot_t, std::string> names, const ExportOptions &options,
    Preparation preparation);

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
    checkExamples(graph, inputTypes);

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
        // Captured values are constants of the model: they get the types their values fix.
        std::vector<type::Type *> captureTypes;
        if (const ::Tuple *captured = fn->tuple(); captured && fn->tupleType()) {
            for (size_t i = 0; i < captured->size() && i < fn->tupleType()->size(); ++i) {
                captureTypes.push_back(
                    exactTypeOf(captured->get<slot_t>(i), fn->tupleType()->typeAt(i)));
            }
        }
        camel::runtime::bindFunctionCall(session.rootDraft(), fn, inputTypes, captureTypes);
        root = session.commit();
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
    return translatePrepared(ctx, emitter, root, inputs, std::move(names), options, preparation);
}

/// What a graph that outgrew its representation may also be: the unrolling of a recursion that
/// did not end. That unrolling stops at whichever limit it meets first, the size of one graph
/// or the space all graphs live in (refuseUnendedUnrolling), and the size of one graph is also
/// what a recursion of constant depth that is merely too deep runs into. The export cannot tell
/// the two apart from the failure, so the refusal for the size of one graph names the
/// recursion and says what to do if its depth depends on the model input. Empty when the
/// function has no recursion.
std::string unendedUnrollingHint(core::context::Context &ctx, const GCGraph *received) {
    const auto call = findRecursiveCall(received);
    if (!call) {
        return {};
    }
    const uint64_t origin = call->first->nodeOrigin(call->second);
    std::string location;
    if (const auto source = ctx.sourceContext();
        source && origin != camel::source::kInvalidOriginId) {
        if (const auto *file = source->fileForOrigin(origin)) {
            const auto range = source->resolveOrigin(origin);
            location         = std::format(
                " (at {}:{}:{})",
                file->path,
                range.start.line + 1,
                range.start.character + 1);
        }
    }
    const std::string name = call->first->name().substr(0, call->first->name().find('$'));
    return std::format(
        ". If the depth of the recursion{}{} depends on the model input, its unrolling did not "
        "end instead: every level that was unrolled gave the next call arguments more specific "
        "than the loop's own (a more precise type, or a constant); give the values the "
        "recursion carries and the constants it uses types that say their element type and "
        "shape (`x as Tensor<f32, const [...]>`)",
        isSourceName(name) ? std::format(" of '{}'", name) : std::string(),
        location);
}

/// The export's simplification filled the space graphs live in. With a recursion in the
/// function that is an unrolling that did not end: said so, with what could not be placed, at
/// the recursive call. Returns (the caller rethrows `error`) when `error` is something else or
/// the function has no recursion.
void refuseUnendedUnrolling(
    core::context::Context &ctx, const GCGraph *received, const std::exception &error) {
    const auto *full = dynamic_cast<const camel::runtime::GraphSpaceExhausted *>(&error);
    if (!full) {
        return;
    }
    const auto call = findRecursiveCall(received);
    if (!call) {
        return;
    }
    const uint64_t origin = call->first->nodeOrigin(call->second);
    std::string location;
    if (const auto source = ctx.sourceContext();
        source && origin != camel::source::kInvalidOriginId) {
        if (const auto *file = source->fileForOrigin(origin)) {
            const auto range = source->resolveOrigin(origin);
            location         = std::format(
                "{}:{}:{}",
                file->path,
                range.start.line + 1,
                range.start.character + 1);
        }
    }
    const std::string name = call->first->name().substr(0, call->first->name().find('$'));
    // What the export received, for scale: the graphs and nodes reachable by direct calls.
    size_t graphs = 0, nodes = 0;
    {
        std::unordered_set<const GCGraph *> seen;
        std::vector<const GCGraph *> work{received};
        while (!work.empty()) {
            const GCGraph *g = work.back();
            work.pop_back();
            if (g == nullptr || !seen.insert(g).second || !g->hasNodePayload()) {
                continue;
            }
            ++graphs;
            for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
                if (*it == nullptr) {
                    continue;
                }
                ++nodes;
                if ((*it)->kind == GCNodeKind::Func) {
                    work.push_back(g->directCalleeGraphOf(it.ref()));
                }
            }
        }
    }
    throw ExportError(
        std::format(
            "the simplification of the exported function did not end: unrolling the "
            "recursion{} filled the memory the compiler keeps graphs in (the graph '{}' it was "
            "building takes {} bytes and no longer fits; the function the export received had "
            "{} nodes in {} graphs). A recursion "
            "whose depth depends on the model input is exported as a loop once its body stops "
            "changing; here every level that was unrolled gave the next call arguments more "
            "specific than the loop's own (a more precise type, or a constant), so one more "
            "level was unrolled each time. Give the values the recursion carries and the "
            "constants it uses types that say their element type and shape (`x as Tensor<f32, "
            "const [...]>`)",
            isSourceName(name) ? std::format(" of '{}'", name) : std::string(),
            full->graph(),
            full->bytes(),
            nodes,
            graphs),
        location,
        origin,
        ExportError::Kind::Other);
}

Model translatePrepared(
    core::context::Context &ctx, Emitter &emitter, GCGraph *root, std::span<const Value> inputs,
    std::unordered_map<slot_t, std::string> names, const ExportOptions &options,
    Preparation preparation) {
    const auto context = ctx.shared_from_this();
    std::ostringstream log;
    // The pass entry (ShapesOnly): the preceding passes simplified the program without two
    // things only the export knows: the shapes of the examples (binding their types above
    // re-inferred the node types, so the shape of a tensor and the arithmetic on it are
    // constants now, and a recursion bounded by a shape can unroll) and the split of branched
    // calls, which is valid only for a consumer that never executes the graph. The same
    // simplification as onnx.export_model finishes the job with them, so that the two entries
    // accept the same functions. Macros are not expanded here: that stays the business of
    // std::macro before this pass.
    const GCGraph *beforeSimplify = root;
    // Measurement aid: what the bound function looks like when the export receives it and
    // after the export's own simplification (one line each on stderr).
    const bool prepStats = std::getenv("CAMEL_ONNX_PREP_STATS") != nullptr;
    if (prepStats) {
        std::cerr << "ONNX_PREP_STATS received " << reachableStats(root) << std::endl;
    }
    try {
        root = preparation == Preparation::ShapesOnly
                   ? simplifyGraph(context, root, exportRewriteConfig(), log, kMaxExportRounds)
                   : simplifyFully(context, root, log);
    } catch (const ExportError &) {
        throw;
    } catch (const std::exception &e) {
        // Simplification unrolls a recursion level by level. A function that calls itself
        // twice on one path doubles with every level, and when its depth depends on the model
        // input nothing ends that before the graph outgrows its representation. Say so, at
        // the call, instead of reporting the allocation failure.
        const auto tree = findTreeRecursion(beforeSimplify);
        if (!tree) {
            // ONNX has no calls, so the export inlines every call and unrolls every recursion
            // of constant depth into the one graph of the exported function, without the size
            // bounds of an optimization pass. A function that does not fit one graph that way
            // is refused as such, at the export_model call (the error names no node).
            if (const auto *capacity =
                    dynamic_cast<const camel::runtime::GraphCapacityError *>(&e)) {
                throw ExportError(
                    std::format(
                        "the exported function does not fit one graph once every call in it "
                        "is inlined and every recursion of constant depth is unrolled (ONNX "
                        "has no calls): graph '{}' needs {} {}, and one runtime graph holds "
                        "at most {} (they are addressed with 16 bits); export a smaller "
                        "function (fewer unrolled steps, or the forward function instead of "
                        "a training step through all of them){}",
                        capacity->graph(),
                        capacity->size(),
                        capacity->quantity(),
                        capacity->limit(),
                        unendedUnrollingHint(ctx, beforeSimplify)));
            }
            // The other way an unrolling ends badly: no single graph is too large, but the
            // space all graphs live in is full (a recursion that is unrolled without end).
            refuseUnendedUnrolling(ctx, beforeSimplify, e);
            throw;
        }
        const uint64_t origin = tree->first->nodeOrigin(tree->second);
        std::string location;
        if (const auto source = ctx.sourceContext();
            source && origin != camel::source::kInvalidOriginId) {
            if (const auto *file = source->fileForOrigin(origin)) {
                const auto range = source->resolveOrigin(origin);
                location         = std::format(
                    "{}:{}:{}",
                    file->path,
                    range.start.line + 1,
                    range.start.character + 1);
            }
        }
        throw ExportError(
            std::format(
                "a function calls itself more than once on one path (tree recursion) and "
                "the depth does not become a constant, so the recursion cannot be unrolled "
                "(simplification stopped with: {}); only a recursion with one recursive call "
                "can be exported as loops",
                e.what()),
            location,
            origin,
            ExportError::Kind::CallRemains);
    }
    if (prepStats) {
        std::cerr << "ONNX_PREP_STATS simplified " << reachableStats(root) << std::endl;
    }
    if (dumpEnabled()) {
        GraphVizDumpPass(context, GraphVizDumpConfig{.readableOnly = true}).apply(root, std::cerr);
    }

    Evaluator evaluator(emitter, options, ctx, std::move(names), ctx.sourceContext());
    evaluator.refuseEffects(root);
    emitOutputs(emitter, evaluator.translate(root, inputs), options.outputName);
    return finishModel(emitter, options);
}

/// Declares the model inputs of a parameter of the entry function from its declared type:
/// one input per tensor or number, named after the parameter (`x`), a struct field or tuple
/// element by its path (`m_fc1_w`, `t_0`).
class DeclaredInputBinder {
  public:
    explicit DeclaredInputBinder(Emitter &emitter) : emitter_(emitter) {}

    Value bind(type::Type *ty, const std::string &name, const std::string &param) {
        if (!ty) {
            refuse(param, name, "has no declared type");
        }
        if (const auto *tensorType = tensor::asTensorType(ty)) {
            if (!tensorType->dtype() || !tensorType->shape()) {
                refuse(
                    param,
                    name,
                    std::format(
                        "is declared '{}', which leaves its {} unknown; a model input needs "
                        "both: write the type with them, e.g. Tensor<f32, const [2, 3]> (a "
                        "negative extent is a dynamic axis), or give the parameter a value "
                        "with --arg, which makes it a constant of the model",
                        ty->toString(),
                        !tensorType->shape()
                            ? (tensorType->dtype() ? "shape" : "element type and shape")
                            : "element type"));
            }
            tensor::StaticShape shape = *tensorType->shape();
            std::vector<size_t> dynamic;
            for (size_t a = 0; a < shape.size(); ++a) {
                if (shape[a] < 0) {
                    shape[a] = tensor::kUnknownDim;
                    dynamic.push_back(a);
                }
            }
            declare(name);
            const Value input   = Value::symbolic(name, tensorType->dtype(), shape);
            ValueInfo inputInfo = valueInfoOf(name, factsOf(input));
            for (size_t a : dynamic) {
                (*inputInfo.shape)[a] = Dim{std::format("{}_dim{}", name, a)};
            }
            emitter_.addInput(std::move(inputInfo));
            return input;
        }
        switch (ty->code()) {
        case TypeCode::Int32:
        case TypeCode::Int64:
        case TypeCode::Float32:
        case TypeCode::Float64:
        case TypeCode::Bool: {
            declare(name);
            Value input = Value::symbolicScalar(name, ty);
            emitter_.addInput(valueInfoOf(name, factsOf(input)));
            return input;
        }
        case TypeCode::Struct: {
            auto *structType = static_cast<type::StructType *>(ty);
            std::vector<Value> fields;
            for (size_t i = 0; i < structType->size(); ++i) {
                fields.push_back(
                    bind(
                        structType->typeAt(i),
                        name + "_" + std::string(structType->fieldName(i)),
                        param));
            }
            return Value::aggregate(ty, std::move(fields));
        }
        case TypeCode::Tuple: {
            auto *tupleType = static_cast<type::TupleType *>(ty);
            std::vector<Value> fields;
            for (size_t i = 0; i < tupleType->size(); ++i) {
                fields.push_back(bind(tupleType->typeAt(i), std::format("{}_{}", name, i), param));
            }
            return Value::aggregate(ty, std::move(fields));
        }
        default:
            break;
        }
        refuse(
            param,
            name,
            std::format(
                "has type '{}'; a model input is a tensor, a number, or a struct or tuple of "
                "those (give the parameter a value with --arg to make it a constant of the "
                "model)",
                ty->toString()));
    }

  private:
    [[noreturn]] static void
    refuse(const std::string &param, const std::string &name, const std::string &why) {
        throw ExportError(
            name == param ? std::format("parameter '{}' of 'main' {}", param, why)
                          : std::format("'{}' of parameter '{}' of 'main' {}", name, param, why));
    }

    void declare(const std::string &name) {
        if (!names_.insert(name).second) {
            throw ExportError(std::format("two inputs are named '{}'", name));
        }
    }

    Emitter &emitter_;
    std::unordered_set<std::string> names_;
};

} // namespace

Model translateEntryGraph(
    core::context::Context &ctx, GCGraph *root, const ExportOptions &options) {
    Emitter emitter(options.opset, capabilitiesFor(options));
    emitter.graph().name = options.graphName;

    // One model input per parameter the entry graph still has, `with` ones first: the order
    // the parameters are bound in.
    DeclaredInputBinder binder(emitter);
    std::vector<Value> inputs;
    const auto *funcType   = root->funcType();
    const size_t withCount = root->withPorts().size();
    const size_t portCount = withCount + root->normPorts().size();
    for (size_t i = 0; i < portCount; ++i) {
        const bool isWith        = i < withCount;
        const size_t k           = isWith ? i : i - withCount;
        const gc_node_ref_t port = isWith ? root->withPorts()[k] : root->normPorts()[k];
        const std::string name   = funcType && i < funcType->argNamesCount()
                                       ? std::string(funcType->argNameAt(i))
                                       : std::format("{}{}", options.inputName, i);
        const bool known =
            funcType && (isWith ? k < funcType->withTypesCount() : k < funcType->normTypesCount());
        if (known && (isWith ? funcType->withIsVarAt(k) : funcType->normIsVarAt(k))) {
            throw ExportError(
                std::format(
                    "parameter '{}' of 'main' is a 'var' parameter, a variable of the caller; "
                    "a model has no such input",
                    name));
        }
        type::Type *declared = known ? (isWith ? funcType->withTypeAt(k) : funcType->normTypeAt(k))
                                     : root->node(port)->dataType;
        inputs.push_back(binder.bind(declared, name, name));
    }

    // The objects given with --arg are constants of the graph; the initializers they become
    // are named after the parameter (a struct's leaves after their fields).
    std::unordered_map<slot_t, std::string> names;
    for (const auto &argument : execute::boundEntryArguments()) {
        collectNames(argument.value, argument.type, argument.name, names);
    }
    DetachedGraphsGuard guard{ctx};
    return translatePrepared(
        ctx,
        emitter,
        root,
        inputs,
        std::move(names),
        options,
        Preparation::ShapesOnly);
}

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
