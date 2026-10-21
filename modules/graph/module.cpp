/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The graph module: a program as a value. It reads the graphs of a Program (the Program the
 * running program was compiled to, or one a pass is given) and, in later commits, builds a new
 * one through a transaction. Every read is pure: a Program is an immutable snapshot.
 */

#include "handle.h"
#include "nodekind.h"
#include "read.h"
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
using camel::graphmod::nodeKindType;
using camel::graphmod::readGroups;
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
        for (const auto &group : readGroups()) {
            exportEntity(group->name(), group);
        }
        for (const auto &group : txnGroups()) {
            exportEntity(group->name(), group);
        }
        exportEntity(
            "current",
            OperatorGroup::create(
                "current",
                {{"graph:current",
                  StaticFuncTypeResolver::create({}, {}, HandleType::get(HandleKind::Program))}}));
        // `==` and `!=` of the handle types (design principles 4.3): a library defines the
        // operators of its own types. Two handles are equal when they name the same thing of
        // the same Program.
        std::vector<std::pair<std::string, resolver_ptr_t>> eq, ne;
        for (HandleKind kind : kHandleKinds) {
            Type *type = HandleType::get(kind);
            eq.emplace_back("graph:eq", StaticFuncTypeResolver::create({}, {{type, false}, {type, false}}, Type::Bool()));
            ne.emplace_back("graph:ne", StaticFuncTypeResolver::create({}, {{type, false}, {type, false}}, Type::Bool()));
        }
        exportEntity("__eq__", OperatorGroup::create("__eq__", eq));
        exportEntity("__neq__", OperatorGroup::create("__neq__", ne));
        exportDefaultImportRef("__eq__");
        exportDefaultImportRef("__neq__");
        // The number a handle is known by inside its Program: the GraphId of a graph, the NodeId
        // of a node, the OperId of an operator. Stable for the Program, so a pass can key a Map
        // by it.
        exportEntity(
            "id",
            OperatorGroup::create(
                "id",
                {{"graph:id",
                  DynamicFuncTypeResolver::create(
                      {{0, {}}, {1, {false}}},
                      "(h: Graph | Node | Op) => int",
                      [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                          -> std::optional<Type *> {
                          if (HandleType::is(norm[0], HandleKind::Graph) ||
                              HandleType::is(norm[0], HandleKind::Node) ||
                              HandleType::is(norm[0], HandleKind::Op))
                              return Type::Int64();
                          return std::nullopt;
                      })}}));
        exportType(Reference("Value"), HandleType::get(HandleKind::Node));
    }

    bool load() override {
        if (loaded_)
            return true;
        context_->registerExecutorFactory("graph", [ctx = context_]() {
            return std::make_shared<GraphExecutor>(ctx);
        });
        // The purity of every operator (design principles 3.3): a Program is immutable.
        auto &table = camel::core::OperatorTable::instance();
        table.declare("graph", {{"current", true}, {"eq", true}, {"ne", true}, {"id", true}});
        for (const auto &group : readGroups()) {
            table.declare("graph", {{group->name(), true}});
        }
        // A transaction is a state that edits change: none of its operators is pure, except the
        // two that read a function value.
        for (const auto &group : txnGroups()) {
            const bool reads = group->name() == "program_of" || group->name() == "graph_of";
            table.declare("graph", {{group->name(), reads}});
        }
        loaded_ = true;
        return true;
    }
};

} // namespace

extern "C" Module *camel_module_create(Context *ctx) {
    return new GraphModule(ctx->shared_from_this());
}
