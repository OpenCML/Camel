/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Jul. 29, 2025
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/type.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/type.h"
#include "camel/core/operator_traits.h"
#include "executor.h"
#include "key.h"
#include "operators.h"

#include <optional>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

namespace {
// The computed signatures of this module stay in C++ (design principles 6.4; handover H19,
// step 10): `choice`, `sample` and `shuffle` take their result type from the element type of
// their array argument, and `stdlib/signatures/random.cml` keeps their reference entries.
// Every other operator is declared in `stdlib/random.cml`, the one source of its signature.
const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "choice",
            {{"rand:choice",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "<> (array: T[]) => T",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (norm[0]->code() != TypeCode::Array)
                          return std::nullopt;
                      return tt::as_ptr<ArrayType>(norm[0])->elemType();
                  })}}),
        OperatorGroup::create(
            "sample",
            {{"rand:sample",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {2, {false, false}}},
                  "<> (array: T[], n: int) => T[]",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (norm[0]->code() != TypeCode::Array)
                          return std::nullopt;
                      return norm[0];
                  })}}),
        OperatorGroup::create(
            "shuffle",
            {{"rand:shuffle",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "<> (array: T[]) => T[]",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (norm[0]->code() != TypeCode::Array)
                          return std::nullopt;
                      return norm[0];
                  })}}),
    };
    return groups;
}
} // namespace

RandomModule::RandomModule(context_ptr_t ctx) : BuiltinModule("random", ctx) {
    for (const auto &group : getOperatorGroups())
        exportEntity(group->name(), group);
    exportType(Reference("Key"), camel::random::keyType());
}

module_ptr_t RandomModule::create(context_ptr_t ctx) { return std::make_shared<RandomModule>(ctx); }

bool RandomModule::load() {
    if (loaded_)
        return true;
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("rand", [ctx]() { return createRandomExecutor(ctx); });
    // The purity of every operator (design principles 3.3).
    camel::core::OperatorTable::instance().declare(
        "rand",
        {
            // The generator functions read or advance it; keyed random is pure.
            {"seed", false},
            {"rand", false},
            {"randn", false},
            {"randint", false},
            {"choice", false},
            {"sample", false},
            {"shuffle", false},
            {"key", true},
            {"split", true},
            {"permutation", true},
        });
    // What each touches (docs/design/op-semantics 8): the module's generator. Its draws are
    // effects the program orders like any other (the operators are not pure).
    {
        using camel::core::sem::AccessMode;
        using camel::core::sem::externalAmbient;
        camel::core::OperatorTable::instance().declareResources(
            "rand",
            {
                {"seed", externalAmbient("rand:generator", AccessMode::Write)},
                {"rand", externalAmbient("rand:generator", AccessMode::ReadModifyWrite)},
                {"randn", externalAmbient("rand:generator", AccessMode::ReadModifyWrite)},
                {"randint", externalAmbient("rand:generator", AccessMode::ReadModifyWrite)},
                {"choice", externalAmbient("rand:generator", AccessMode::ReadModifyWrite)},
                {"sample", externalAmbient("rand:generator", AccessMode::ReadModifyWrite)},
                {"shuffle", externalAmbient("rand:generator", AccessMode::ReadModifyWrite)},
            });
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new RandomModule(ctx->shared_from_this()); }
}
