// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The graph module: a program as a value. It reads the graphs of a Program (the Program the
 * running program was compiled to, or one a pass is given) and, in later commits, builds a new
 * one through a transaction. Every read is pure: a Program is an immutable snapshot.
 */

#include "facts.h"
#include "handle.h"
#include "nodekind.h"
#include "read.h"
#include "regionkind.h"
#include "txn.h"

#include "camel/core/context/context.h"
#include "camel/core/module/builtin.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/variant.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <optional>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;
using camel::graphmod::HandleKind;
using camel::graphmod::HandleType;
using camel::graphmod::effectsType;
using camel::graphmod::nodeKindType;
using camel::graphmod::regionKindType;
using camel::graphmod::factKernels;
using camel::graphmod::readKernels;
using camel::graphmod::txnGroups;
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

slot_t currentKernel(ArgsView &, ArgsView &, Context &ctx) {
    const camel::runtime::Program *program = ctx.currentRuntimeProgram();
    if (program == nullptr) {
        program = ctx.runtimeProgram();
    }
    return toSlot(const_cast<camel::runtime::Program *>(program));
}

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
    return rtdata::toSlot<rtdata::Int64>(
        handle->kind() == HandleKind::Graph ? static_cast<int64_t>(toValue(handle->graph()))
                                            : static_cast<int64_t>(handle->index()));
}

class GraphExecutor final : public Executor {
  public:
    explicit GraphExecutor(context_ptr_t ctx)
        : Executor(std::move(ctx), kernels()) {}

  private:
    static std::unordered_map<std::string, operator_t> kernels() {
        auto out = readKernels();
        out.insert(txnKernels().begin(), txnKernels().end());
        out.insert(factKernels().begin(), factKernels().end());
        out.emplace("current", currentKernel);
        out.emplace("eq", eqKernel);
        out.emplace("ne", neKernel);
        out.emplace("id", idKernel);
        return out;
    }
};

class GraphModule final : public BuiltinModule {
  public:
    explicit GraphModule(context_ptr_t ctx) : BuiltinModule("graph", std::move(ctx)) {
        for (HandleKind kind : kHandleKinds) {
            exportType(Reference(camel::graphmod::handleKindName(kind)), HandleType::get(kind));
        }
        exportType(Reference("NodeKind"), nodeKindType());
        exportType(Reference("RegionKind"), regionKindType());
        // The type of the value `effects` gives: a structural type has no name of its own, and
        // the declaration of `effects` names its result (handover H141).
        exportType(Reference("Effects"), effectsType());
        // The signatures C++ computes (standard library conventions 2): the four operators of a
        // function value take the type of an argument, so a declaration cannot express them.
        // Everything else is declared in stdlib/graph.cml, `constant` and `add_constant` per
        // sample type included (handover H202).
        for (const auto &group : txnGroups()) {
            exportEntity(group->name(), group);
        }
        // `==` of the handle types is declared in stdlib/graph.cml (design principles 4.3): two
        // handles are equal when they name the same thing of the same Program. `!=` cannot be
        // declared — an overload of `==` gives it, and `operator !=` is OperatorNotOverloadable
        // — so the module keeps the `__neq__` group the compiler asks for when it does not
        // negate a Camel overload. Both names are default imported: an importer of `graph`
        // finds them among a type's operators without naming them.
        std::vector<std::pair<std::string, resolver_ptr_t>> ne;
        for (HandleKind kind : kHandleKinds) {
            Type *type = HandleType::get(kind);
            ne.emplace_back("graph:ne", StaticFuncTypeResolver::create({}, {{type, false}, {type, false}}, Type::Bool()));
        }
        exportEntity("__neq__", OperatorGroup::create("__neq__", ne));
        exportDefaultImportRef("__eq__");
        exportDefaultImportRef("__neq__");
        exportType(Reference("Value"), HandleType::get(HandleKind::Node));
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
                {"graphs", true},
                {"root", true},
                {"name", true},
                {"nodes", true},
                {"inputs", true},
                {"with_inputs", true},
                {"ctrl_inputs", true},
                {"users", true},
                {"value_users", true},
                {"is_constant", true},
                {"kind", true},
                {"op", true},
                {"op_name", true},
                {"is_pure", true},
                {"type_of", true},
                {"type_name", true},
                {"callee", true},
                {"tags", true},
                {"is_anchor", true},
                {"is_written_slot", true},
                {"origin", true},
                {"show", true},
                {"region_of", true},
                {"region_kind", true},
                {"region_parent", true},
                // the value's type: the program itself, the number a handle is known by, the
                // constant a node holds, and the three facts
                {"current", true},
                {"id", true},
                {"constant", true},
                {"effects", true},
                {"shape", true},
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
                {"open_function", false},
                {"to_function", false},
                {"program_of", true},
                {"graph_of", true},
                {"add_constant", false},
            });
        loaded_ = true;
        return true;
    }
};

} // namespace

extern "C" Module *camel_module_create(Context *ctx) {
    return new GraphModule(ctx->shared_from_this());
}
