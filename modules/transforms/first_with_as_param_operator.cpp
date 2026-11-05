// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `first_with_as_param(f)` as an operator of the `transforms` module (see
 * first_with_as_param_operator.h).
 *
 * The wiring is `remat`'s and `jvp`'s: a type resolver, a kernel and the three registrations. What
 * the kernel does is `remat`'s too -- clone `f`'s graph into a member of a `GraphDraftGroup` and
 * answer a function value over the copy -- with one difference that is the whole of the transform:
 * the clone's first declared `with` port is registered as an ordinary port, in front of the ones
 * the source graph had, and the copied signature says the same thing.
 *
 * Nothing of the body is read, rewritten or re-derived: the nodes come across exactly as they were,
 * and a read of the weight is a port read before the reinterpretation and after it. That is why the
 * transform is a port reclassification and not a body rewrite, and why the two halves -- the
 * signature the call is typed as and the ports the graph binds -- have to be written from one law
 * (`firstWithAsParamSignature`).
 */

#include "first_with_as_param_operator.h"

#include "readings.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/func.h"
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
using camel::core::type::FuncTypeResolver;
using camel::core::type::FunctionType;
using camel::core::type::FunctionTypeFactory;
using camel::core::type::Type;
using camel::core::type::TypeCode;
using camel::core::type::type_vec_t;

/// The sent name of a parameter, empty when the signature records none: the names travel with the
/// parameters, so a diagnostic, a graph dump or a translation keeps naming the weight `x` after the
/// move instead of numbering it.
std::string withNameOf(const FunctionType *function, size_t index) {
    return function->hasArgNames() ? std::string(function->argNameAt(index)) : std::string();
}

std::string normNameOf(const FunctionType *function, size_t index) {
    return function->hasArgNames()
               ? std::string(function->argNameAt(function->withTypesCount() + index))
               : std::string();
}

/// Why the first `with` parameter of this function cannot be moved, or an empty string when it can.
/// The three shapes below are not this transform's limits but facts about what a `with` slot can
/// hold: a `var` cell is a variable and not a value a call hands over, a `constas` parameter is
/// part of the signature's identity rather than a value, and a `with` pack stands for however many
/// arguments a call gives past the fixed ones, so it has no first member to take out.
std::string unmovableWith(const FunctionType *function) {
    if (function->hasWithRest()) {
        return "`first_with_as_param` moves one `with` parameter, and this function's `with` list "
               "ends in a pack, which stands for the arguments a call gives past the fixed ones";
    }
    if (function->withIsVarAt(0)) {
        return "`first_with_as_param` moves a value parameter, and this function's first `with` "
               "parameter is a `var` cell: a variable is read and written by name, not handed in "
               "by a call";
    }
    if (function->withIsConstasAt(0)) {
        return "`first_with_as_param` moves a parameter a call passes a value for, and this "
               "function's first `with` parameter is a compile-time one (`constas`), which is part "
               "of the signature's identity";
    }
    return {};
}

} // namespace

FunctionType *firstWithAsParamSignature(FunctionType *function) {
    if (function == nullptr || function->withTypesCount() == 0) {
        return nullptr;
    }
    if (!unmovableWith(function).empty()) {
        return nullptr;
    }
    Type *moved = function->withTypeAt(0);
    if (moved == nullptr || function->exitType() == nullptr) {
        return nullptr;
    }
    FunctionTypeFactory factory;
    // The parameters keep their names: they are how diagnostics, graph dumps and translations refer
    // to them, and the weight keeps the one it was declared with.
    factory.setHasCompileInfo(function->hasArgNames());
    factory.setExitType(function->exitType());
    factory.setResources(function->resources());
    // The `with` list is the source's own, one place earlier.
    for (size_t i = 1; i < function->withTypesCount(); ++i) {
        if (function->withTypeAt(i) == nullptr) {
            return nullptr;
        }
        factory.addWithArg(
            withNameOf(function, i),
            function->withTypeAt(i),
            function->withIsVarAt(i),
            function->withNeedsValueAt(i));
    }
    // ... and the first ordinary parameter is the value the first `with` port carried.
    factory.addNormArg(withNameOf(function, 0), moved, false);
    for (size_t i = 0; i < function->normTypesCount(); ++i) {
        if (function->normTypeAt(i) == nullptr) {
            return nullptr;
        }
        // A rest parameter is the last ordinary one and stays the last one: it stands for the
        // arguments a call gives past the named parameters, and the moved weight is a named one.
        if (function->hasRest() && i + 1 == function->normTypesCount()) {
            factory.addRestArg(normNameOf(function, i), function->restPack());
            continue;
        }
        factory.addNormArg(
            normNameOf(function, i), function->normTypeAt(i), function->normIsVarAt(i));
    }
    return factory.build();
}

namespace {

/// The type of the call: `(f: F) => firstWithAsParamSignature(F)`. The reinterpreted function is the
/// *exit* type, not the whole type, so a call of the operator is typed as the function it answers
/// (the shape `rematOperatorSignature` and `jvpOperatorSignature` have).
FunctionType *firstWithAsParamOperatorSignature(FunctionType *function) {
    FunctionType *reinterpreted = firstWithAsParamSignature(function);
    if (reinterpreted == nullptr) {
        return nullptr;
    }
    FunctionTypeFactory factory;
    factory.addNormArg("f", function, false);
    factory.setExitType(reinterpreted);
    return factory.build();
}

/// `first_with_as_param`: one function in, the same function with its first `with` parameter moved
/// onto the front of the ordinary list, out. No `with` parameter is read by the operator itself.
class FirstWithAsParamResolver final : public FuncTypeResolver {
  public:
    std::optional<FunctionType *>
    resolve(const type_vec_t &with, const type_vec_t &norm) const override {
        if (!with.empty() || norm.size() != 1 || norm[0] == nullptr ||
            norm[0]->code() != TypeCode::Function) {
            return std::nullopt;
        }
        FunctionType *answer = firstWithAsParamOperatorSignature(tt::as_ptr<FunctionType>(norm[0]));
        if (answer == nullptr) {
            return std::nullopt;
        }
        return answer;
    }

    std::optional<std::string> explainRejection(
        const type_vec_t &with, const type_vec_t &norm,
        type::static_args_t normStatics) const override {
        (void)normStatics;
        if (!with.empty()) {
            return "`first_with_as_param` takes no `with` parameter: the function it reinterprets "
                   "is its argument";
        }
        if (norm.size() != 1 || norm[0] == nullptr) {
            return "`first_with_as_param` takes one argument, the function whose signature it "
                   "reinterprets";
        }
        if (norm[0]->code() != TypeCode::Function) {
            return "`first_with_as_param` reinterprets a function's signature: " +
                   norm[0]->toString() + " is not a function";
        }
        const auto *function = tt::as_ptr<FunctionType>(norm[0]);
        if (function->withTypesCount() == 0) {
            return "`first_with_as_param` moves the first `with` parameter onto the ordinary list, "
                   "and this function has no `with` parameter: there is no first one to move";
        }
        const std::string why = unmovableWith(function);
        if (!why.empty()) {
            return why;
        }
        return std::nullopt;
    }

    /// The law, not one instance of it: what the reinterpreted function is for a given `f` is
    /// `firstWithAsParamSignature`'s answer, and the declaration the module publishes says the same
    /// thing in prose (`stdlib/signatures/transforms.cml`). The answer is a function type, so it is
    /// written as one, parenthesised: a reader of this line can see that the result is callable.
    std::string signature() const override {
        return "(f: (...) => any) => ((the first `with` parameter, then the parameters) => any)";
    }

    /// `f` is the only parameter, in every form: `first_with_as_param(f: g)` has to bind to it.
    bool parameterNames(std::vector<std::string> &with, std::vector<std::string> &norm)
        const override {
        with.clear();
        norm = {"f"};
        return true;
    }
};

/// `first_with_as_param(f)` at run time: a function value over a copy of `f`'s graph whose first
/// declared `with` port is an ordinary one and whose signature says so.
slot_t firstWithAsParamKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    auto *function = norm.get<::Function *>(0);
    if (function == nullptr || function->graph() == nullptr) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError, "transforms: first_with_as_param needs a function value");
    }
    const rt::GCGraph *source = function->graph();
    FunctionType *reinterpreted = firstWithAsParamSignature(source->funcType());
    if (reinterpreted == nullptr) {
        if (source->funcType() != nullptr && source->funcType()->withTypesCount() > 0) {
            const std::string sentence = unmovableWith(source->funcType());
            if (!sentence.empty()) {
                throwRuntimeFault(RuntimeDiag::RuntimeError, "transforms: " + sentence);
            }
        }
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "transforms: first_with_as_param needs a function with a first `with` parameter that "
            "is a value");
    }
    rt::GraphDraftGroup group(ctx.shared_from_this(), function->program());
    const std::string name = std::format("{}__first_with_as_param", source->name());
    const rt::GraphId made = group.reserve(name, name);
    {
        auto &transaction = group.transaction(made);
        auto &draft       = transaction.edit();
        draft.setFuncType(reinterpreted);
        const rt::DraftGraphCloneResult cloned = rt::cloneRuntimeGraphIntoDraft(transaction, source);
        // The reinterpretation: the first declared `with` port is registered as the first ordinary
        // port, and the `with` ports after it keep the order they had. A draft node is reclassified
        // by registering it in one list only -- `appendNormPort` refuses a node that is already a
        // `with` port -- and the clone has registered neither yet, so the two lists are written
        // here once, in the order the new signature names them.
        if (cloned.withPorts.empty()) {
            throwRuntimeFault(
                RuntimeDiag::RuntimeError,
                "transforms: first_with_as_param found a signature with a `with` parameter but a "
                "graph with no `with` port to move");
        }
        draft.appendNormPort(cloned.withPorts.front());
        for (size_t i = 1; i < cloned.withPorts.size(); ++i) {
            draft.appendWithPort(cloned.withPorts[i]);
        }
        for (rt::gc_node_ref_t port : cloned.normPorts) {
            draft.appendNormPort(port);
        }
        // The captures of the source graph are ports too, and the copy has to declare them or a
        // call through the reinterpreted value binds nothing (the defect `remat_operator.cpp`
        // records for the same copy).
        for (rt::gc_node_ref_t port : cloned.closureNodes) {
            draft.appendClosureNode(port);
        }
        if (!cloned.closureNodes.empty()) {
            draft.setClosureType(
                const_cast<camel::core::type::TupleType *>(function->tupleType()));
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
        draft.setFlags(source->flags());
    }
    group.encode();
    const rt::GCGraph *reinterpretedGraph = group.encoded(made);
    if (reinterpretedGraph == nullptr) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "transforms: first_with_as_param could not publish the graph it built");
    }
    auto *copy =
        ::Function::create(group.program(), made, function->tupleType(), mm::autoSpace());
    if (::Tuple *closure = function->tuple()) {
        for (size_t i = 0; i < closure->size(); ++i) {
            copy->tuple()->set<slot_t>(i, closure->get<slot_t>(i), function->tupleType());
        }
    }
    // The reading: the two port lists before and after, so a run says what moved rather than that
    // something did. `with_in` / `params_in` are the source graph's declared `with` ports and its
    // ordinary ones; `with_out` / `params_out` are the copy's. The copy having no `with` port is the
    // fact a caller of this transform can check: a `with` value is what the forward and reverse
    // transforms have no place to take a direction in from, so a function whose answer has to move
    // with the weight is built without one.
    readings() << "REINTERP first_with_as_param function=" << source->name()
               << " with_in="
               << (source->funcType() != nullptr ? source->funcType()->withTypesCount() : 0)
               << " params_in="
               << (source->funcType() != nullptr ? source->funcType()->normTypesCount() : 0)
               << " with_out="
               << (reinterpretedGraph->funcType() != nullptr
                       ? reinterpretedGraph->funcType()->withTypesCount()
                       : 0)
               << " params_out="
               << (reinterpretedGraph->funcType() != nullptr
                       ? reinterpretedGraph->funcType()->normTypesCount()
                       : 0)
               << " graph=" << rt::toValue(made) << std::endl;
    return toSlot<::Function *>(copy);
}

} // namespace

const std::vector<oper_group_ptr_t> &firstWithAsParamOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "first_with_as_param",
            {{"transforms:first_with_as_param", std::make_shared<FirstWithAsParamResolver>()}}),
    };
    return groups;
}

std::unordered_map<std::string, operator_t> firstWithAsParamOperatorKernels() {
    return {
        {"first_with_as_param", &firstWithAsParamKernel},
    };
}

void registerFirstWithAsParamOperatorMetadata() {
    // It clones a graph: the call does something the compiler cannot see from the arguments alone,
    // so it is declared as not pure, like the module's other transforms.
    camel::core::OperatorTable::instance().declare(
        "transforms", {{"first_with_as_param", false}});
    // The kernel reads the graph of the function value it receives -- the ports it moves are the
    // graph's -- so it refuses a loop there as `jvp` does (OperatorTraits::readsGraphs).
    camel::core::OperatorTable::instance().set(
        "transforms:first_with_as_param", {.pure = false, .readsGraphs = true});
    // How a call is written: one function and no `with` parameters of its own.
    camel::core::OperatorTable::instance().declareCallForm(
        "transforms:first_with_as_param",
        {.name = "first_with_as_param", .minArgs = 1, .withCount = 0, .declaredByModule = true});
}

} // namespace camel::transforms