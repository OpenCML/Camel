// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The native module `ckpt`: the checkpoint operators (`save_tree`, `load_tree`) and the one tree
// operator that is not written in Camel yet (`tree_leaves`, which needs value-tuple expansion).
// `tree_map`, `tree_reduce` and `tree_unzip` are in `stdlib/tree.cml`: the compiler expands them
// from their Camel bodies like any other function, with no intrinsic left here.
// The module is not called `tree`: `stdlib/tree.cml` is the Camel tree library, and a `.cml`
// beside a same-named `.cmo` would be that native module's declaration file instead of the
// library.

#include "../tensor/ops/registry.h"
#include "camel/core/context/context.h"
#include "camel/core/module/builtin.h"
#include "camel/core/operator_traits.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"
#include "include/ckpt/checkpoint.h"

namespace {
using namespace camel::core::type;
using namespace camel::core::module;
using namespace camel::core::context;
// The checkpoint operators run; `tree_leaves` is still an operator whose result type comes from
// its argument, so its resolver leaves the signature to the compiler that expands it.
class TreeLeavesResolver final : public FuncTypeResolver {
  public:
    std::optional<FunctionType *>
    resolve(const type_vec_t &, const type_vec_t &, const ModifierSet &) const override {
        return std::nullopt; // The compiler resolves the leaf tuple from the tree's type.
    }
    std::string signature() const override { return "tree_leaves (type-directed)"; }
};
class CkptExecutor final : public Executor {
  public:
    CkptExecutor(context_ptr_t ctx, std::unordered_map<std::string, operator_t> ops)
        : Executor(std::move(ctx), std::move(ops)) {}
};
class CkptModule final : public BuiltinModule {
  public:
    explicit CkptModule(context_ptr_t ctx) : BuiltinModule("ckpt", std::move(ctx)) {
        // Tensors are leaves of a checkpoint: their types must be registered.
        camel::tensor::ops::registerTensorOps();
        for (const auto &group : camel::ckptlib::checkpointOperatorGroups())
            exportEntity(group->name(), group);
        exportEntity(
            "tree_leaves",
            OperatorGroup::create(
                "tree_leaves", {{"ckpt:tree_leaves", std::make_shared<TreeLeavesResolver>()}}));
    }
    bool load() override {
        context_->registerExecutorFactory("ckpt", [ctx = context_]() {
            return std::make_shared<CkptExecutor>(ctx, camel::ckptlib::checkpointKernels());
        });
        camel::ckptlib::declareCheckpointResources();
        camel::core::OperatorTable::instance().declare("ckpt", {{"tree_leaves", true}});
        loaded_ = true;
        return true;
    }
};
} // namespace
extern "C" Module *camel_module_create(Context *ctx) {
    return new CkptModule(ctx->shared_from_this());
}