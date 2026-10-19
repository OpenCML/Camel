/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Public names for type-directed intrinsics; the compiler emits ordinary graph nodes.
#include "camel/core/context/context.h"
#include "camel/core/module/builtin.h"
#include "camel/core/operator_traits.h"
#include "camel/core/type/resolver.h"

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
class TreeModule final : public BuiltinModule {
  public:
    explicit TreeModule(context_ptr_t ctx) : BuiltinModule("tree", std::move(ctx)) {
        for (const std::string name :
             {"tree_map", "tree_reduce", "tree_unzip", "tree_leaves", "tree_paths"}) {
            exportEntity(
                name,
                OperatorGroup::create(
                    name,
                    {{"tree:" + name, std::make_shared<TreeResolver>(name)}}));
        }
    }
    bool load() override {
        camel::core::OperatorTable::instance().declare(
            "tree",
            {{"tree_map", false},
             {"tree_reduce", false},
             {"tree_unzip", true},
             {"tree_leaves", true},
             {"tree_paths", true}});
        loaded_ = true;
        return true;
    }
};
} // namespace
extern "C" Module *camel_module_create(Context *ctx) {
    return new TreeModule(ctx->shared_from_this());
}
