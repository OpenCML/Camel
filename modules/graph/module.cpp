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

namespace {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;
using camel::core::rtdata::Variant;

bool isHandleType(const Type *type) {
    for (HandleKind kind :
         {HandleKind::Program,
          HandleKind::Graph,
          HandleKind::Node,
          HandleKind::Region,
          HandleKind::TypeRef,
          HandleKind::Op,
          HandleKind::Tag,
          HandleKind::Txn}) {
        if (HandleType::is(type, kind))
            return true;
    }
    return false;
}

slot_t currentKernel(ArgsView &, ArgsView &, Context &ctx) {
    const camel::runtime::Program *program = ctx.currentRuntimeProgram();
    if (program == nullptr) {
        program = ctx.runtimeProgram();
    }
    return toSlot(const_cast<camel::runtime::Program *>(program));
}

/// Whether two handles of one kind name the same thing of the same Program. A Program is its
/// own identity; every other handle compares the numbers it holds.
slot_t sameKernel(ArgsView &, ArgsView &norm, Context &) {
    using camel::core::rtdata::Object;
    auto *a = norm.get<Object *>(0);
    auto *b = norm.get<Object *>(1);
    return rtdata::toSlot<rtdata::Bool>(a == b || (a != nullptr && b != nullptr && a->equals(b, norm.type(0))));
}

class GraphExecutor final : public Executor {
  public:
    explicit GraphExecutor(context_ptr_t ctx)
        : Executor(std::move(ctx), kernels()) {}

  private:
    static std::unordered_map<std::string, operator_t> kernels() {
        auto out = readKernels();
        out.emplace("current", currentKernel);
        out.emplace("same", sameKernel);
        return out;
    }
};

class GraphModule final : public BuiltinModule {
  public:
    explicit GraphModule(context_ptr_t ctx) : BuiltinModule("graph", std::move(ctx)) {
        for (HandleKind kind :
             {HandleKind::Program,
              HandleKind::Graph,
              HandleKind::Node,
              HandleKind::Region,
              HandleKind::TypeRef,
              HandleKind::Op,
              HandleKind::Tag,
              HandleKind::Txn}) {
            exportType(Reference(camel::graphmod::handleKindName(kind)), HandleType::get(kind));
        }
        exportType(Reference("NodeKind"), nodeKindType());
        for (const auto &group : readGroups()) {
            exportEntity(group->name(), group);
        }
        exportEntity(
            "current",
            OperatorGroup::create(
                "current",
                {{"graph:current",
                  StaticFuncTypeResolver::create({}, {}, HandleType::get(HandleKind::Program))}}));
        exportEntity(
            "same",
            OperatorGroup::create(
                "same",
                {{"graph:same",
                  DynamicFuncTypeResolver::create(
                      {{0, {}}, {2, {false, false}}},
                      "(a: H, b: H) => bool, H a handle type of the graph module",
                      [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                          -> std::optional<Type *> {
                          if (!isHandleType(norm[0]) || !norm[0]->equals(norm[1]))
                              return std::nullopt;
                          return Type::Bool();
                      })}}));
    }

    bool load() override {
        if (loaded_)
            return true;
        context_->registerExecutorFactory("graph", [ctx = context_]() {
            return std::make_shared<GraphExecutor>(ctx);
        });
        // The purity of every operator (design principles 3.3): a Program is immutable.
        auto &table = camel::core::OperatorTable::instance();
        table.declare("graph", {{"current", true}, {"same", true}});
        for (const auto &group : readGroups()) {
            table.declare("graph", {{group->name(), true}});
        }
        loaded_ = true;
        return true;
    }
};

} // namespace

extern "C" Module *camel_module_create(Context *ctx) {
    return new GraphModule(ctx->shared_from_this());
}
