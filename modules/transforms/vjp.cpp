// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `vjp` as an operator: the name, the graph it builds, and the refusal for a shape it does not
 * build yet.
 *
 * The library used to write the transform once per arity, because a body has to name its
 * parameters. The declaration is one for every function type (`stdlib/vjp.cml`), and what stands
 * behind it is this file: the same three steps the bodies ran, made once and closed over.
 *
 *   forward, linear = split(jvp(f))
 *   vjp(f)          = (xs) => { let (y, res) = forward(xs)
 *                               (y, (ct) => transpose(linear)(res, ct)) }
 *
 * Two graphs carry it. The pullback's `(ct) => transpose(linear)(res, ct)` takes the residuals as a
 * trailing port, because they belong to one call of the outer graph and a closure passes what it
 * captures after its parameters. The outer `(xs) => (y, pullback)` takes the two halves the same
 * way: its captures are `forward` and `linear`, so a call of the value this operator answers is the
 * call the library body made.
 *
 * The arithmetic is not here and must not be: `jvp`, `split` and `transpose` build the graphs and
 * the rulesets give the numbers, so `vjp` reads on every backend as the bodies did. What is here is
 * the shape -- the currying, the residual the pullback captures, and the one-element cotangent
 * table a function of one parameter answers with its number (that is `grad`:
 * `grad(f)(x) = vjp(f)(x).1(1.0)`).
 *
 * The shapes built today are functions of `float` parameters, one or more. A parameter whose
 * tangent space is not `float` is refused by name, with the ruler's answer for it
 * (`camel::core::tangentSpaceOf`), so the sentence a pilot stops at is still the one that says what
 * is missing.
 */

#include "vjp.h"

#include "jvp_operator.h"
#include "split_operator.h"
#include "transpose_operator.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/operator.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/tangent_space.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"`r`n#include "camel/utils/type.h"

#include <format>
#include <iostream>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace camel::transforms {

namespace {

namespace mm = camel::core::mm;
namespace rt = camel::runtime;
namespace tt = camel::core::type;

using camel::core::context::Context;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;
using camel::core::rtdata::toSlot;
using camel::core::type::FunctionType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::core::type::param_vec_t;

/// The slots one operator call is given, for the three kernels this one composes: nothing on the
/// `with` side (`jvp(f)`, `split(g)` and `transpose(h)` take their function as the ordinary
/// parameter) and one function value on the norm side.
class FixedArgs final : public ArgsView {
  public:
    FixedArgs(std::vector<slot_t> slots, std::vector<Type *> types)
        : slots_(std::move(slots)), types_(std::move(types)) {}

    size_t size() const override { return slots_.size(); }
    slot_t slot(size_t index) const override { return slots_[index]; }
    void setSlot(size_t index, slot_t value) override { slots_[index] = value; }
    tt::TypeCode code(size_t index) const override { return types_[index]->code(); }
    Type *type(size_t index) const override { return types_[index]; }

  private:
    std::vector<slot_t> slots_;
    std::vector<Type *> types_;
};

/// The answer of a call that this step does not build: the parameter it stopped at and what the
/// ruler says about that parameter's type. A refusal that names the shape is the difference between
/// "another shape" and "a bug".
[[noreturn]] void refuseParameter(size_t index, Type *type) {
    const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(type);
    throwRuntimeFault(
        RuntimeDiag::RuntimeError,
        std::format(
            "transforms: `vjp` builds the graph of a function of `float` parameters, and parameter "
            "{} is a `{}`, whose tangent space is {} -- this step does not lift it yet",
            index, type->toString(), space.describe()));
}

/// Runs one of this module's own transform kernels on `function`: the three steps of `vjp` are
/// operators of this module, so composing them is a call and not a second implementation.
slot_t runKernel(
    const std::string &name, const std::unordered_map<std::string, operator_t> &kernels,
    ::Function *function, Context &ctx) {
    const auto it = kernels.find(name);
    if (it == kernels.end()) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::format(
                "transforms: `vjp` needs the operator `transforms:{}`, which this module does not "
                "register",
                name));
    }
    FixedArgs with({}, {});
    FixedArgs norm({toSlot<::Function *>(function)}, {function->graph()->funcType()});
    return it->second(with, norm, ctx);
}

/// A tuple type of `types`, in order.
TupleType *tupleOf(std::vector<Type *> types) { return TupleType::create(std::move(types)); }

/// A port of `type` holding one of the graph's parameters, in the order the function takes them.
rt::gc_node_ref_t addPort(rt::GraphTransaction &transaction, Type *type) {
    auto &draft                  = transaction.edit();
    const rt::gc_node_ref_t node = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
    draft.appendNormPort(node);
    return node;
}

/// A port of `type` holding a value the closure captured, in the order the closure's tuple lists
/// them. A capture is not a parameter: the caller passes the parameters and the function value
/// carries the captures, so the port is registered as one of the graph's closure nodes
/// (`src/compile/gir/captures.cpp` makes the same pair of calls for a captured value).
rt::gc_node_ref_t addCapture(rt::GraphTransaction &transaction, Type *type) {
    auto &draft                  = transaction.edit();
    const rt::gc_node_ref_t node = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
    draft.appendClosureNode(node);
    return node;
}

/// A call of the function value `callee` with `args`, the call answering `exitType`.
rt::gc_node_ref_t addCall(
    rt::GraphTransaction &transaction, rt::gc_node_ref_t callee,
    std::span<const rt::gc_node_ref_t> args, Type *exitType) {
    auto &draft                  = transaction.edit();
    const rt::gc_node_ref_t node = draft.addCallNode(exitType);
    draft.setWithInputs(node, std::span<const rt::gc_node_ref_t>(&callee, 1));
    draft.setNormInputs(node, args);
    return node;
}

/// The element `index` of the aggregate `source`.
rt::gc_node_ref_t addElement(
    rt::GraphTransaction &transaction, Type *elementType, rt::gc_node_ref_t source, size_t index) {
    auto &draft                  = transaction.edit();
    const rt::gc_node_ref_t node = draft.addAccsNode(elementType, static_cast<uint32_t>(index));
    draft.setNormInputs(node, std::span<const rt::gc_node_ref_t>(&source, 1));
    return node;
}

/// A tuple of `values`, in order.
rt::gc_node_ref_t addTuple(
    rt::GraphTransaction &transaction, TupleType *type, std::span<const rt::gc_node_ref_t> values) {
    auto &draft = transaction.edit();
    std::vector<size_t> slots(values.size());
    for (size_t i = 0; i < slots.size(); ++i) {
        slots[i] = i;
    }
    auto *empty                 = ::Tuple::create(values.size(), mm::autoSpace());
    const rt::gc_node_ref_t src = draft.materializeStaticValue(toSlot<::Tuple *>(empty), type);
    const rt::gc_node_ref_t node =
        draft.addFillNode(type, rt::makeFillPayload(rt::GCFillKind::Tuple, slots));
    draft.setNormInputs(node, std::span<const rt::gc_node_ref_t>(&src, 1));
    draft.setWithInputs(node, values);
    return node;
}

/// A closure of `graph` capturing `captures`, its value of type `type`, the captured values of the
/// types `closureType` lists, in the same order.
rt::gc_node_ref_t addClosure(
    rt::GraphDraftGroup &group, rt::GraphTransaction &transaction, rt::GraphId graph,
    FunctionType *type, TupleType *closureType, std::span<const rt::gc_node_ref_t> captures) {
    auto &draft = transaction.edit();
    auto *function =
        ::Function::create(group.program(), graph, closureType, mm::autoSpace());
    const rt::gc_node_ref_t src = draft.materializeStaticValue(toSlot<::Function *>(function), type);
    std::vector<size_t> slots(captures.size());
    for (size_t i = 0; i < slots.size(); ++i) {
        slots[i] = i;
    }
    const rt::gc_node_ref_t node =
        draft.addFillNode(type, rt::makeFillPayload(rt::GCFillKind::FunctionClosure, slots));
    draft.setNormInputs(node, std::span<const rt::gc_node_ref_t>(&src, 1));
    draft.setWithInputs(node, captures);
    return node;
}

/// Makes `result` what the graph answers with.
void setResult(rt::GraphTransaction &transaction, rt::gc_node_ref_t result) {
    auto &draft = transaction.edit();
    draft.setOutputNode(result);
    draft.setExitNode(result);
    draft.setReturnNode(result, rt::GCReturnKind::Self);
}

/// The three steps on `f`: the two halves of the lifted graph and the pullback of its linear half.
struct Halves {
    ::Function *forward       = nullptr;
    ::Function *linear        = nullptr;
    ::Function *transposed    = nullptr;
    FunctionType *forwardType = nullptr;
    /// The residuals' type: what `forward` answers beside the value, and the pullback's capture.
    Type *residualType = nullptr;
};

Halves halvesOf(::Function *f, Context &ctx) {
    auto *const lifted =
        camel::core::rtdata::fromSlot<::Function *>(runKernel(
            "jvp", camel::transforms::operatorKernels(), f, ctx));
    auto *const pair = camel::core::rtdata::fromSlot<::Tuple *>(runKernel(
        "split", camel::transforms::splitOperatorKernels(), lifted, ctx));

    Halves halves;
    halves.forward = camel::core::rtdata::fromSlot<::Function *>(pair->get<slot_t>(0));
    halves.linear  = camel::core::rtdata::fromSlot<::Function *>(pair->get<slot_t>(1));
    halves.transposed =
        camel::core::rtdata::fromSlot<::Function *>(runKernel(
            "transpose", camel::transforms::transposeOperatorKernels(), halves.linear, ctx));
    halves.forwardType = halves.forward->graph()->funcType();
    auto *exit         = TypeTraits::as_ptr<TupleType>(const_cast<Type *>(halves.forwardType->exitType()));
    halves.residualType = exit->typeAt(1);
    return halves;
}

/// The function `vjp(f)` is: `(xs) => (forward(xs).0, (ct) => transpose(linear)(res, ct))`.
::Function *buildVjp(
    const camel::core::context::context_ptr_t &ctx, ::Function *f, FunctionType *fType) {
    Halves halves = halvesOf(f, *ctx);

    FunctionType *const transposedType = halves.transposed->graph()->funcType();
    Type *const cotangentsType         = const_cast<Type *>(transposedType->exitType());
    const size_t arity                 = fType->normTypesCount();

    // A function of one parameter answers the number, not a one-long table: `grad` is
    // `vjp(f)(x).1(1.0)`, and the gradient of one parameter is a number.
    Type *pullbackExit = cotangentsType;
    if (arity == 1) {
        pullbackExit = TypeTraits::as_ptr<TupleType>(cotangentsType)->typeAt(0);
    }

    param_vec_t pullbackParams;
    pullbackParams.emplace_back(Type::Float64(), false);
    FunctionType *const pullbackType =
        FunctionType::create(param_vec_t{}, pullbackParams, pullbackExit);
    TupleType *const pullbackClosure =
        tupleOf({transposedType, halves.residualType});

    param_vec_t params;
    for (size_t i = 0; i < arity; ++i) {
        params.emplace_back(const_cast<Type *>(fType->normTypeAt(i)), false);
    }
    TupleType *const answer     = tupleOf({Type::Float64(), pullbackType});
    FunctionType *const outType = FunctionType::create(param_vec_t{}, params, answer);
    TupleType *const outClosure =
        tupleOf({halves.forwardType, transposedType});

    const std::string base =
        std::format("{}__vjp", f->graph() != nullptr ? f->graph()->name() : "f");
    rt::GraphDraftGroup group(ctx->shared_from_this(), f->program());
    const rt::GraphId pullbackId = group.reserve(base + "_pb", base + "_pb");
    const rt::GraphId outerId    = group.reserve(base, base);

    // `(ct) => transpose(linear)(res, ct)`: the parameter first, the captures after it, which is
    // the order a closure passes what it captured.
    {
        auto &transaction = group.transaction(pullbackId);
        auto &draft       = transaction.edit();
        draft.setFuncType(pullbackType);
        const rt::gc_node_ref_t ct     = addPort(transaction, Type::Float64());
        const rt::gc_node_ref_t linear = addCapture(transaction, transposedType);
        const rt::gc_node_ref_t res    = addCapture(transaction, halves.residualType);
        const rt::gc_node_ref_t args[] = {res, ct};
        rt::gc_node_ref_t call         = addCall(transaction, linear, args, cotangentsType);
        if (arity == 1) {
            call = addElement(transaction, pullbackExit, call, 0);
        }
        setResult(transaction, call);
    }

    // `(xs) => (y, pullback)`.
    {
        auto &transaction = group.transaction(outerId);
        auto &draft       = transaction.edit();
        draft.setFuncType(outType);
        std::vector<rt::gc_node_ref_t> xs;
        for (size_t i = 0; i < arity; ++i) {
            xs.push_back(addPort(transaction, const_cast<Type *>(fType->normTypeAt(i))));
        }
        const rt::gc_node_ref_t forward =
            addCapture(transaction, halves.forwardType);
        const rt::gc_node_ref_t linear = addCapture(transaction, transposedType);
        const rt::gc_node_ref_t called =
            addCall(transaction, forward, xs, const_cast<Type *>(halves.forwardType->exitType()));
        const rt::gc_node_ref_t value = addElement(transaction, Type::Float64(), called, 0);
        const rt::gc_node_ref_t res   = addElement(transaction, halves.residualType, called, 1);
        const rt::gc_node_ref_t captures[] = {linear, res};
        const rt::gc_node_ref_t pullback =
            addClosure(group, transaction, pullbackId, pullbackType, pullbackClosure, captures);
        const rt::gc_node_ref_t values[] = {value, pullback};
        setResult(transaction, addTuple(transaction, answer, values));
    }

    group.encode();
    if (group.encoded(outerId) == nullptr) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError, "transforms: `vjp` could not publish the graph it built");
    }
    auto *built = ::Function::create(group.program(), outerId, outClosure, mm::autoSpace());
    if (::Tuple *closure = built->tuple()) {
        closure->set<slot_t>(0, toSlot<::Function *>(halves.forward), outClosure);
        closure->set<slot_t>(1, toSlot<::Function *>(halves.transposed), outClosure);
    }
    return built;
}

slot_t vjpKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    auto *function = norm.get<::Function *>(0);
    if (function == nullptr || function->graph() == nullptr) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "transforms: `vjp` needs a function value");
    }
    FunctionType *const type = function->graph()->funcType();
    if (type == nullptr || type->normTypesCount() == 0) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "transforms: `vjp` needs a function of at least one parameter");
    }
    for (size_t i = 0; i < type->normTypesCount(); ++i) {
        if (!type->normTypeAt(i)->equals(Type::Float64())) {
            refuseParameter(i, const_cast<Type *>(type->normTypeAt(i)));
        }
    }
    ::Function *const built = buildVjp(ctx.shared_from_this(), function, type);
    std::cout << "REINTERP vjp function=" << function->graph()->name()
              << " arity=" << type->normTypesCount()
              << " graph=" << rt::toValue(built->graphId())
              << " type=" << built->graph()->funcType()->toString() << std::endl;
    return toSlot<::Function *>(built);
}

} // namespace

std::unordered_map<std::string, operator_t> vjpOperatorKernels() {
    return {
        {"vjp", &vjpKernel},
    };
}

void registerVjpOperatorMetadata() {
    // The module half of the URI is the file the declaration is written in (`stdlib/vjp.cml`), not
    // this module's name: a body-less declaration names `<its module>:<its name>`.
    camel::core::OperatorTable::instance().declare("vjp", {{"vjp", false}});
    // It reads the graph of the function value it is handed -- the transform is a rewrite of that
    // graph -- so it refuses a loop there, as `jvp` and `remat` do.
    camel::core::OperatorTable::instance().set("vjp:vjp", {.pure = false, .readsGraphs = true});
}

} // namespace camel::transforms