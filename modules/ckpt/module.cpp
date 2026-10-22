/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

// The native module `ckpt`: the checkpoint operators (save_tree, load_tree) and, until the
// type-directed tree intrinsics are rewritten in Camel (H51 step 13), the four they expand to.
// The module is not called `tree`: `stdlib/tree.cml` is the Camel tree library, and a `.cml`
// beside a same-named `.cmo` would be that native module's declaration file instead of the
// library (H19 plan section 1.6).
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
class TreeResolver final : public FuncTypeResolver {
  public:
    explicit TreeResolver(std::string name) : name_(std::move(name)) {}
    std::optional<FunctionType *>
    resolve(const type_vec_t &, const type_vec_t &, const ModifierSet &) const override {
        return std::nullopt; // The compiler resolves callbacks separately for each leaf.
    }
    std::string signature() const override { return name_ + " (type-directed tree intrinsic)"; }

  private:
    std::string name_;
};
// The checkpoint operators (save_tree, load_tree) are the only ones that run; the others are
// expanded by the compiler.
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
        for (const std::string name :
             {"tree_map", "tree_reduce", "tree_unzip", "tree_leaves"}) {
            exportEntity(
                name,
                OperatorGroup::create(
                    name, {{"ckpt:" + name, std::make_shared<TreeResolver>(name)}}));
        }
    }
    bool load() override {
        context_->registerExecutorFactory("ckpt", [ctx = context_]() {
            return std::make_shared<CkptExecutor>(ctx, camel::ckptlib::checkpointKernels());
        });
        camel::ckptlib::declareCheckpointResources();
        camel::core::OperatorTable::instance().declare(
            "ckpt",
            {{"tree_map", false},
             {"tree_reduce", false},
             {"tree_unzip", true},
             {"tree_leaves", true}});
        loaded_ = true;
        return true;
    }
};
} // namespace
extern "C" Module *camel_module_create(Context *ctx) {
    return new CkptModule(ctx->shared_from_this());
}