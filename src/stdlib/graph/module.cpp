// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The graph module: a program as a value. It reads the graphs of a Program (the Program the
 * running program was compiled to, or one a pass is given) and, in later commits, builds a new
 * one through a transaction. Every read is pure: a Program is an immutable snapshot.
 */

#include "camel/core/type/native.h"
#include "facts.h"
#include "handle.h"
#include "nodekind.h"
#include "probe.h"
#include "read.h"
#include "regionkind.h"
#include "txn.h"

#include "camel/core/context/context.h"
#include "camel/core/module/builtin.h"
#include "camel/core/operator_traits.h"
#include "camel/core/resource_semantics.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/variant.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"
#include "camel/execute/pass/base.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <optional>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;
using camel::graphmod::effectsType;
using camel::graphmod::factKernels;
using camel::graphmod::HandleKind;
using camel::graphmod::HandleType;
using camel::graphmod::nodeKindType;
using camel::graphmod::readKernels;
using camel::graphmod::regionKindType;
using camel::graphmod::txnKernels;

namespace {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;
using camel::core::rtdata::Variant;

constexpr HandleKind kHandleKinds[] = {
    HandleKind::Program,
    HandleKind::Graph,
    HandleKind::Node,
    HandleKind::Region,
    HandleKind::TypeRef,
    HandleKind::Op,
    HandleKind::Tag,
    HandleKind::Txn,
};

slot_t equalHandles(ArgsView &norm) {
    using camel::core::rtdata::Object;
    auto *a = norm.get<Object *>(0);
    auto *b = norm.get<Object *>(1);
    return rtdata::toSlot<rtdata::Bool>(a == b || a->equals(b, norm.type(0)));
}
slot_t eqKernel(ArgsView &, ArgsView &norm, Context &) { return equalHandles(norm); }
slot_t neKernel(ArgsView &, ArgsView &norm, Context &) {
    return rtdata::toSlot<rtdata::Bool>(!rtdata::fromSlot<rtdata::Bool>(equalHandles(norm)));
}

slot_t idKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle = norm.get<camel::graphmod::Handle *>(0);
    // A node's id is its *canonical* number (design principles 9.5): the position it has in the
    // order `nodes` gives, which the graph's structure decides. A node of a draft has none, and
    // `nodeCanonicalNumber` refuses the reading rather than answer the place the node holds in the
    // draft -- that place is the order the nodes were made in and the commit replaces it.
    if (handle->kind() == HandleKind::Node) {
        return rtdata::toSlot<rtdata::Int64>(
            static_cast<int64_t>(camel::graphmod::nodeCanonicalNumber(handle)));
    }
    return rtdata::toSlot<rtdata::Int64>(
        handle->kind() == HandleKind::Graph ? static_cast<int64_t>(toValue(handle->graph()))
                                            : static_cast<int64_t>(handle->index()));
}

/// The number a handle was *created* with: the GraphId, the NodeId or the OperId. That is spelling
/// -- it says the order the program was written in -- so the operator's spec declares `spelling`
/// and only a body whose type writes the capability reaches it (design principles 9.5).
slot_t creationIdKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle = norm.get<camel::graphmod::Handle *>(0);
    return rtdata::toSlot<rtdata::Int64>(
        handle->kind() == HandleKind::Graph ? static_cast<int64_t>(toValue(handle->graph()))
                                            : static_cast<int64_t>(handle->index()));
}

/// Explicitly selected pass preservation comparison. Generic invocation does not infer it
/// from a function-to-function type. Structural and effect-credential admission remain active.
slot_t checkPassBoundaryKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const auto *before = norm.get<::Function *>(0)->program();
    const auto *after  = norm.get<::Function *>(1)->program();
    if (checkPassBoundary(
            ctx.shared_from_this(),
            "graph::check_pass_boundary (selected)",
            *before,
            *after)) {
        return rtdata::makeError(
            "PassBoundary",
            "selected pass boundary check failed",
            "<graph::check_pass_boundary>");
    }
    return rtdata::makeOk(
        rtdata::toSlot(norm.get<::Function *>(1)),
        TupleType::fallible(norm.type(1)));
}

class GraphExecutor final : public Executor {
  public:
    explicit GraphExecutor(context_ptr_t ctx) : Executor(std::move(ctx), kernels()) {}

  private:
    static std::unordered_map<std::string, operator_t> kernels() {
        auto out = readKernels();
        out.insert(txnKernels().begin(), txnKernels().end());
        out.insert(factKernels().begin(), factKernels().end());
        out.emplace("eq", eqKernel);
        out.emplace("ne", neKernel);
        out.emplace("id", idKernel);
        out.emplace("creation_id", creationIdKernel);
        out.emplace("check_pass_boundary", checkPassBoundaryKernel);
        return out;
    }
};

class GraphModule final : public BuiltinModule {
  public:
    explicit GraphModule(context_ptr_t ctx) : BuiltinModule("graph", std::move(ctx)) {
        for (HandleKind kind : kHandleKinds) {
            camel::core::type::registerNativeType(
                "graph",
                camel::graphmod::handleKindName(kind),
                HandleType::get(kind));
        }
        camel::core::type::registerNativeType("graph", "NodeKind", nodeKindType());
        camel::core::type::registerNativeType("graph", "RegionKind", regionKindType());
        // The type of the value `effects` gives: a structural type has no name of its own, and
        // the declaration of `effects` names its result.
        camel::core::type::registerNativeType("graph", "Effects", effectsType());
        // The signatures C++ computes (standard library conventions 2): the four operators of a
        // function value take the type of an argument, so a declaration cannot express them.
        // Everything else is declared in stdlib/graph.cml, including the per-sample-type
        // `constant` and `add_constant`.
        // `==` of the handle types is declared in stdlib/graph.cml (design principles 4.3): two
        // handles are equal when they name the same thing of the same Program, and `!=` is
        // `!(==)` on that declaration -- the graph holds the equality and the negation, as it
        // does for an array or a tuple. `operator !=` cannot be declared
        // (OperatorNotOverloadable), and `__neq__` needs no entry here: an entry in that group
        // is what answers `!=` when the `==` is a Camel function the compiler cannot negate,
        // which a declared handle operator is not. `==` is default imported so that an importer
        // of `graph` finds it among a type's operators without naming it.
        exportDefaultImportRef("__eq__");
    }

    bool load() override {
        if (loaded_)
            return true;
        context_->registerExecutorFactory("graph", [ctx = context_]() {
            return std::make_shared<GraphExecutor>(ctx);
        });
        // The purity of every operator (design principles 3.3): a Program is immutable. The
        // names are the ones stdlib/graph.cml declares; `eq`/`ne` belong to the operator groups
        // (`==` is declared, `!=` stays in C++) and `current`/`id` to the read side.
        auto &table = camel::core::OperatorTable::instance();
        table.declare(
            "graph",
            {
                // the reads: a Program is not changed by them
                {"graphs_of", true},
                {"check_pass_boundary", true},
                {"name", true},
                {"nodes", true},
                {"exit_of", true},
                {"inputs", true},
                {"with_inputs", true},
                {"sync_inputs", true},
                {"order_inputs", true},
                {"users", true},
                {"value_users", true},
                {"is_constant", true},
                {"constant_bytes", true},
                {"kind", true},
                {"op", true},
                {"op_name", true},
                {"is_pure", true},
                {"relative_cost", true},
                {"may_be_recomputed", true},
                {"may_fault", true},
                {"may_be_rearranged", true},
                {"type_of", true},
                {"type_name", true},
                {"callee", true},
                {"tags", true},
                {"declaration_tags", true},
                {"tag_name", true},
                {"tag_graphs", true},
                {"rule_of", true},
                {"is_anchor", true},
                {"is_written_slot", true},
                {"origin", true},
                {"show", true},
                {"region_of", true},
                {"region_kind", true},
                {"region_parent", true},
                // the value's type: the program itself, the number a handle is known by, the
                // constant a node holds, and the three facts
                {"id", true},
                {"creation_id", true},
                {"constant", true},
                {"effects", true},
                {"shape_text", true},
                {"permissions", true},
                // equality of two handles
                {"eq", true},
                {"ne", true},
                // a transaction is a state that edits change: none of its operators is pure,
                // except the two that read a function value
                {"open", false},
                {"replace", false},
                {"erase", false},
                {"commit", false},
                {"abort", false},
                {"eval", false},
                {"order", false},
                {"inline", false},
                {"outline", false},
                {"graph_of", true},
                {"add_constant", false},
            });
        // What a transaction operator touches: the transaction, whole (the declarations say
        // `uses t`). Edits to one transaction keep the order they are written in.
        {
            using camel::core::sem::AccessMode;
            using camel::core::sem::operandAccess;
            using camel::core::sem::ResourceClass;
            camel::core::sem::ResourceSpec onTxn;
            onTxn.accesses = {
                operandAccess(ResourceClass::Handle, 0, AccessMode::ReadModifyWrite, "graph::Txn")};
            table.declareResources(
                "graph",
                {
                    {"replace", onTxn},
                    {"erase", onTxn},
                    {"eval", onTxn},
                    {"order", onTxn},
                    {"inline", onTxn},
                    {"outline", onTxn},
                    {"add_constant", onTxn},
                    {"commit", onTxn},
                    {"abort", onTxn},
                });
        }
        // What `open` touches: the transaction it makes, which is the handle the nine above then
        // edit. It reads the function value it is handed (a value, not a resource) and writes
        // nothing outside the program, so its whole statement is the resource it allocates.
        {
            using camel::core::sem::Resource;
            using camel::core::sem::ResourceClass;
            camel::core::sem::ResourceSpec opensTxn;
            opensTxn.allocates = Resource{.cls = ResourceClass::Handle, .name = "graph::Txn"};
            table.declareResources("graph", {{"open", opensTxn}});
        }
        // What the operators that read the *spelling* of the program say about themselves (design
        // principles 9.5): a graph's or a function's name, a node's source position, the text it
        // shows, and the number it was created with. Declared here, once, the way an operator's
        // accesses are, so a body that calls one is measured as reading the spelling and a
        // declaration that does not write `spelling` is refused. `nodes`, `id`, `op_name`,
        // `type_name` and `tag_name` are not here: they answer the canonical order and the
        // operator's or type's declared name, which the program does not spell.
        {
            camel::core::sem::ResourceSpec readsTheSpelling;
            readsTheSpelling.spelling = true;
            table.declareResources(
                "graph",
                {
                    {"name", readsTheSpelling},
                    {"origin", readsTheSpelling},
                    {"show", readsTheSpelling},
                    {"creation_id", readsTheSpelling},
                });
        }
        camel::graphmod::registerHandleProbePass();
        loaded_ = true;
        return true;
    }
};

} // namespace

extern "C" Module *camel_module_create(Context *ctx) {
    return new GraphModule(ctx->shared_from_this());
}
