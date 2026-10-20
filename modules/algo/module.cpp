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
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/type.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/type.h"
#include "camel/core/operator_traits.h"
#include "executor.h"
#include "operators.h"
#include "sequence.h"

#include <optional>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

namespace {
const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "sort",
            {
                {
                    "algo:sort",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (array: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return std::nullopt;
                            return tt::as_ptr<ArrayType>(norm[0]);
                        }),
                },
            }),

        OperatorGroup::create(
            "insert_sort",
            {
                {
                    "algo:insert_sort",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (array: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return std::nullopt;
                            return tt::as_ptr<ArrayType>(norm[0]);
                        }),
                },
            }),

        OperatorGroup::create(
            "quick_sort",
            {
                {
                    "algo:quick_sort",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (array: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return std::nullopt;
                            return tt::as_ptr<ArrayType>(norm[0]);
                        }),
                },
            }),

        OperatorGroup::create(
            "merge_sort",
            {
                {
                    "algo:merge_sort",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (array: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return std::nullopt;
                            return tt::as_ptr<ArrayType>(norm[0]);
                        }),
                },
            }),

        OperatorGroup::create(
            "merge_sorted_arrays",
            {
                {
                    "algo:merge_sorted_arrays",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "<> (lft: T[], rgt: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array ||
                                norm[1]->code() != TypeCode::Array)
                                return std::nullopt;
                            const auto &lftElemType = tt::as_ptr<ArrayType>(norm[0])->elemType();
                            const auto &rgtElemType = tt::as_ptr<ArrayType>(norm[1])->elemType();
                            if (!lftElemType->equals(rgtElemType))
                                return std::nullopt;
                            return tt::as_ptr<ArrayType>(norm[0]);
                        }),
                },
            }),
    };
    return groups;
}
} // namespace

AlgoModule::AlgoModule(context_ptr_t ctx) : BuiltinModule("algo", ctx) {
    for (const auto &group : getOperatorGroups())
        exportEntity(group->name(), group);
    for (const auto &group : getAlgoSequenceGroups())
        exportEntity(group->name(), group);
}

module_ptr_t AlgoModule::create(context_ptr_t ctx) { return std::make_shared<AlgoModule>(ctx); }

bool AlgoModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("algo", [ctx = context_]() {
        return createAlgoExecutor(ctx);
    });
    // The purity of every operator (design principles 3.3).
    camel::core::OperatorTable::instance().declare(
        "algo",
        {
            // Return a new array: no operator changes its argument (design principles
            // 11.3). Only one name per operation is kept (11.3: `sort`, not `sorted`).
            {"sort", true},
            {"insert_sort", true},
            {"quick_sort", true},
            {"merge_sort", true},
            {"merge_sorted_arrays", true},
            {"flatten", true},
            {"unique", true},
            {"argsort", true},
        });
    // The in-place forms of the sorting operators (design principles 11.3): `xs = xs->sort`
    // sorts the variable's own array when nothing else refers to it (Builder::
    // inPlaceRebindingOf). Each changes the array it is given, so it is not pure; what it
    // changes is the program's own variable, so it has no effect outside the program.
    // An array of numbers is sorted without a fault, so the array is never left half done.
    auto &table = camel::core::OperatorTable::instance();
    for (const char *name : {"sort", "insert_sort", "quick_sort", "merge_sort"}) {
        const std::string uri = std::string("algo:") + name;
        table.set(uri + "_in_place", {.stateOnly = true, .mayFault = true});
        table.setInPlaceForm(uri, uri + "_in_place");
    }
    // An in-place form changes the array it is given and returns it (docs/design/op-semantics
    // 8): a read-modify-write of its first operand, an object, whole.
    for (const char *name : {"sort", "insert_sort", "quick_sort", "merge_sort"}) {
        using namespace camel::core::sem;
        table.declareResources(
            std::string("algo:") + name + "_in_place",
            ResourceSpec{
                .accesses = {operandAccess(ResourceClass::Object, 0, AccessMode::ReadModifyWrite)},
                .result   = ResultAlias{.kind = ResultAlias::Kind::Same, .operand = 0},
            });
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new AlgoModule(ctx->shared_from_this()); }
}
