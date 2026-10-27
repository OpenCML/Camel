// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "module.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/type.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/type.h"
#include "camel/core/operator_traits.h"
#include "executor.h"
#include "operators.h"

#include <optional>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

// The operators of this module are declared in `stdlib/random.cml`, which also defines in Camel
// everything built on them (design principles 6.4, 10.4). The C++ side keeps the kernels, the
// executor, the operator traits and the resources. The module answers on `rand:`, not on its name,
// so the declaration module reads `rand:` from `operatorProtocols()` (module.h).

RandomModule::RandomModule(context_ptr_t ctx) : BuiltinModule("random", ctx) {}

module_ptr_t RandomModule::create(context_ptr_t ctx) { return std::make_shared<RandomModule>(ctx); }

bool RandomModule::load() {
    if (loaded_)
        return true;
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("rand", [ctx]() { return createRandomExecutor(ctx); });
    // The purity of every operator (design principles 3.3): the keyed primitive and the kernels of
    // the whole-array draws are functions of their arguments; `seed` and `next_key` move the
    // global stream.
    camel::core::OperatorTable::instance().declare(
        "rand",
        {
            {"seed", false},
            {"next_key", false},
            {"philox", true},
            {"uniform_array", true},
            {"normal_array", true},
            {"shuffle_i", true},
            {"shuffle_l", true},
            {"shuffle_f", true},
            {"shuffle_d", true},
        });
    // What each touches (docs/design/op-semantics 8): the global stream, the resource `rng`.
    {
        using camel::core::sem::AccessMode;
        using camel::core::sem::externalAmbient;
        camel::core::OperatorTable::instance().declareResources(
            "rand",
            {
                {"seed", externalAmbient("rng", AccessMode::Write)},
                {"next_key", externalAmbient("rng", AccessMode::ReadModifyWrite)},
            });
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new RandomModule(ctx->shared_from_this()); }
}
