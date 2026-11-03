// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `remat(f)` as an operator of the `transforms` module (see remat_operator.h).
 *
 * The wiring is `split`'s, copied rather than shared: a type resolver, a kernel and the two
 * registrations. What the kernel does differently from every other transform here is that it does
 * not build a new *body*: it copies the body it was handed and sets one bit on the copy, because
 * the whole of what `remat` says is a property of the graph (`kGraphFlagRecomputeBackward`).
 *
 * The reading it prints is the pair the step has to be able to show: the property before the copy
 * (0 for a function nobody marked) and after it (1), so a run says both numbers the author asked
 * for. `split` reads the same bit and prints what it did with it (split.cpp).
 */

#include "remat_operator.h"

#include "readings.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/runtime/draft_clone.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"
#include "camel/utils/type.h"

#include <format>
#include <iostream>
#include <optional>
#include <string>

namespace camel::transforms {

namespace {

namespace mm = camel::core::mm;
namespace rt = camel::runtime;
using camel::core::context::Context;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;
using camel::core::rtdata::toSlot;
using camel::core::type::FunctionType;
using camel::core::type::FunctionTypeFactory;
using camel::core::type::FuncTypeResolver;
using camel::core::type::Type;
using camel::core::type::type_vec_t;
using camel::core::type::TypeCode;

/// The type of `remat(f)`: `(f: F) => F` -- the function in, the same function out, because a call
/// through the mark is a call of the same code. The shape is `jvpOperatorSignature`'s: the argument
/// is the operator's ordinary parameter and `F` itself is the *exit* type, because what a call of
/// an operator answers with is the exit type of the type the resolver gives (`split(f)` answers the
/// pair for the same reason). Answering with `F` as the whole type would type the call as `F`'s
/// result instead of as `F`.
FunctionType *rematOperatorSignature(FunctionType *function) {
    if (function == nullptr) {
        return nullptr;
    }
    FunctionTypeFactory factory;
    factory.addNormArg("f", function, false);
    factory.setExitType(function);
    return factory.build();
}

/// remat: a function in, the same function out. Only a function value is accepted -- there is
/// nothing to mark on a number -- and no `with` parameter is read.
class RematResolver final : public FuncTypeResolver {
  public:
    std::optional<FunctionType *>
    resolve(const type_vec_t &with, const type_vec_t &norm) const override {
        if (!with.empty() || norm.size() != 1 || norm[0] == nullptr ||
            norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        return rematOperatorSignature(tt::as_ptr<FunctionType>(norm[0]));
    }

    std::optional<std::string> explainRejection(
        const type_vec_t &with, const type_vec_t &norm,
        type::static_args_t normStatics) const override {
        (void)normStatics;
        if (!with.empty()) {
            return "`remat` takes no `with` parameter: the function it marks is its argument";
        }
        if (norm.size() != 1 || norm[0] == nullptr) {
            return "`remat` takes one argument, the function it marks";
        }
        if (norm[0]->code() != TypeCode::Function) {
            return "`remat` marks a function: " + norm[0]->toString() +
                   " is not one, and a value has nothing to mark";
        }
        return std::nullopt;
    }

    std::string signature() const override {
        return "((float, float) => float) => (float, float) => float";
    }
    bool
    parameterNames(std::vector<std::string> &with, std::vector<std::string> &norm) const override {
        with.clear();
        norm = {"f"};
        return true;
    }
};

/// remat(f) at run time: a function value over a copy of `f`'s graph that carries
/// `kGraphFlagRecomputeBackward`. `f`'s graph is read, never changed; the copy is the transform's
/// own graph, reserved in a group so that it is published the way every transform's graph is.
slot_t rematKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    auto *function = norm.get<::Function *>(0);
    if (function == nullptr || function->graph() == nullptr) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "transforms: remat needs a function value");
    }
    const rt::GCGraph *source = function->graph();
    if (source->recomputesOnBackward()) {
        // Marking a graph twice is marking it once: the value it already is answers the call.
        return toSlot<::Function *>(function);
    }
    rt::GraphDraftGroup group(ctx.shared_from_this(), function->program());
    const std::string name = std::format("{}__remat", source->name());
    const rt::GraphId made = group.reserve(name, name);
    {
        auto &transaction = group.transaction(made);
        auto &draft       = transaction.edit();
        draft.setFuncType(source->funcType());
        const rt::DraftGraphCloneResult cloned =
            rt::cloneRuntimeGraphIntoDraft(transaction, source);
        for (rt::gc_node_ref_t port : cloned.normPorts) {
            draft.appendNormPort(port);
        }
        for (rt::gc_node_ref_t port : cloned.withPorts) {
            draft.appendWithPort(port);
        }
        // The captures of the graph being copied are ports too, and the copy has to declare them or a
        // call through the marked value binds nothing: the closure tuple is there, the graph says it
        // takes none, and every read of a capture answered the zero of its slot -- `x + c` computed
        // `x`, i.e. 1.0 where 4.0 is the answer (author's review of 2026-11-02, high-2). The clone
        // carries them in the source graph's order, which is the closure tuple's order, and the
        // layout comes from the value, the same one the copy's tuple is made of below.
        for (rt::gc_node_ref_t port : cloned.closureNodes) {
            draft.appendClosureNode(port);
        }
        if (!cloned.closureNodes.empty()) {
            draft.setClosureType(const_cast<camel::core::type::TupleType *>(function->tupleType()));
        }
        if (cloned.entryNode != rt::kInvalidNodeRef) {
            draft.setEntryNode(cloned.entryNode);
        }
        if (cloned.exitNode != rt::kInvalidNodeRef) {
            draft.setExitNode(cloned.exitNode);
        }
        if (cloned.outputNode != rt::kInvalidNodeRef) {
            draft.setOutputNode(cloned.outputNode);
        }
        if (cloned.returnNode != rt::kInvalidNodeRef) {
            draft.setReturnNode(cloned.returnNode, cloned.returnKind);
        }
        draft.setFlags(static_cast<uint8_t>(source->flags() | rt::kGraphFlagRecomputeBackward));
    }
    group.encode();
    const rt::GCGraph *marked = group.encoded(made);
    if (marked == nullptr) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "transforms: remat could not publish the graph it built");
    }
    auto *copy = ::Function::create(group.program(), made, function->tupleType(), mm::autoSpace());
    if (::Tuple *closure = function->tuple()) {
        for (size_t i = 0; i < closure->size(); ++i) {
            copy->tuple()->set<slot_t>(i, closure->get<slot_t>(i), function->tupleType());
        }
    }
    // The reading: the property before and after the copy, so one run prints both numbers.
    readings() << "REINTERP remat function=" << source->name()
              << " in_recompute=" << (source->recomputesOnBackward() ? 1 : 0)
              << " out_recompute=" << (marked->recomputesOnBackward() ? 1 : 0)
              << " graph=" << rt::toValue(made) << std::endl;
    return toSlot<::Function *>(copy);
}

} // namespace

const std::vector<oper_group_ptr_t> &rematOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create("remat", {{"transforms:remat", std::make_shared<RematResolver>()}}),
    };
    return groups;
}

std::unordered_map<std::string, operator_t> rematOperatorKernels() {
    return {
        {"remat", &rematKernel},
    };
}

void registerRematOperatorMetadata() {
    camel::core::OperatorTable::instance().declare("transforms", {{"remat", false}});
    // The kernel reads the graph of the function value it is handed -- the mark is a property of
    // that graph -- so it refuses a loop there (OperatorTraits::readsGraphs).
    camel::core::OperatorTable::instance().set(
        "transforms:remat",
        {.pure = false, .readsGraphs = true});
    // The mark is a property of the graph it is handed, and the copy carries the graph's captures as
    // well: before 2026-11-02 the copy declared none, so a call through the marked value bound
    // nothing and answered the zero of every capture's slot -- measured 1.0 where `x + c` says 4.0
    // (author's review, high-2, the same capture-loss class as `vmap`'s).
}

} // namespace camel::transforms