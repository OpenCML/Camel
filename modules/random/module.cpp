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
#include "key.h"
#include "operators.h"

#include <optional>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

// Every operator of this module is declared in `stdlib/random.cml`, `choice`, `sample` and
// `shuffle` included: their result type is the element type of their array argument, which a
// head declaration `::<T>` states exactly (design principles 6.4; handover H19, step 10, H202).
// The C++ side keeps the kernels, the executor, the operator traits and the resources. The
// module answers on `rand:`, not on its name, so the declaration module reads `rand:` from
// `operatorProtocols()` (module.h) now that no group is exported to read it from.

RandomModule::RandomModule(context_ptr_t ctx) : BuiltinModule("random", ctx) {
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
                {"seed", externalAmbient("rng", AccessMode::Write)},
                {"rand", externalAmbient("rng", AccessMode::ReadModifyWrite)},
                {"randn", externalAmbient("rng", AccessMode::ReadModifyWrite)},
                {"randint", externalAmbient("rng", AccessMode::ReadModifyWrite)},
                {"choice", externalAmbient("rng", AccessMode::ReadModifyWrite)},
                {"sample", externalAmbient("rng", AccessMode::ReadModifyWrite)},
                {"shuffle", externalAmbient("rng", AccessMode::ReadModifyWrite)},
            });
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new RandomModule(ctx->shared_from_this()); }
}
