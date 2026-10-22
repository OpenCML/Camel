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

// Every operator of this module is declared in `stdlib/algo.cml`: `argsort` and the seven names
// that take the element type of their array argument are generic declarations, and the C++ side
// keeps the kernels, the executor, the operator traits and the in-place forms (design principles
// 6.4; handover H19, step 10, H202).

AlgoModule::AlgoModule(context_ptr_t ctx) : BuiltinModule("algo", ctx) {}

module_ptr_t AlgoModule::create(context_ptr_t ctx) { return std::make_shared<AlgoModule>(ctx); }

bool AlgoModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("algo", [ctx = context_]() {
        return createAlgoExecutor(ctx);
    });
    // The array primitives answer on the `array` protocol as well (H19 step 5): `array.cml`
    // declares them for itself, and the kernels stay here until the C++ module moves.
    context_->registerExecutorFactory("array", [ctx = context_]() {
        return createAlgoSequenceExecutor(ctx);
    });
    camel::core::OperatorTable::instance().declare(
        "array",
        {
            {"flatten", true},
            {"unique", true},
            {"argsort", true},
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
        // The form returns its operand, changed: the one fact the temporary-value analysis needs.
        table.declareReusesOperand(uri + "_in_place");
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
