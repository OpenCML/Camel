// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/native.h"
#include "module.h"

#include <algorithm>
#include <string_view>

#include "../tensor/ops/registry.h"
#include "camel/core/context/context.h"
#include "camel/core/module/module.h"
#include "camel/core/operator_traits.h"
#include "executor.h"
#include "layers.h"
#include "optim.h"
#include "state.h"

using namespace camel::core::context;
using namespace camel::core::module;

namespace {

/// The operators whose signatures `stdlib/nn.cml` declares. The
/// C++ side keeps their kernels, executors, traits, resources and semantics; the declaration is
/// the module's signature, so their groups are not exported here. `save_params` comes from the
/// optimizer groups rather than the tensor registry, but it stays on this list: the registry
/// loop below must never export a declared name, whether or not that name moves into the
/// registry.
constexpr std::string_view kDeclaredOperators[] = {
    "save_params",
};

bool isDeclaredOperator(std::string_view name) {
    return std::find(std::begin(kDeclaredOperators), std::end(kDeclaredOperators), name) !=
           std::end(kDeclaredOperators);
}

} // namespace

NnModule::NnModule(context_ptr_t ctx) : BuiltinModule("nn", ctx) {
    // nn's operators rely on the tensor catalog (and its tangent space) being registered.
    camel::tensor::ops::registerTensorOps();
    camel::nn::registerNnTensorOps();
    camel::core::type::registerNativeType("nn", "OptimizerState", camel::nn::OptimizerStateType::Default());
    for (const auto &group : camel::tensor::ops::OpRegistry::instance().operatorGroups("nn")) {
        if (!isDeclaredOperator(group->name())) {
            exportEntity(group->name(), group);
        }
    }
    for (const auto &group : camel::nn::optimizerOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

module_ptr_t NnModule::create(context_ptr_t ctx) { return std::make_shared<NnModule>(ctx); }

bool NnModule::load() {
    if (loaded_) {
        return true;
    }
    context_->registerExecutorFactory("nn", [ctx = context_]() { return createNnExecutor(ctx); });
    // The optimizers are functions of their arguments: they return new trees and states.
    for (const char *uri : {"nn:sgd", "nn:adam_state", "nn:adam"}) {
        camel::core::OperatorTable::instance().set(uri, {.pure = true, .elementwise = false});
    }
    // And they reuse the storage of the tree they are handed. `sgd(params, g, lr)` answers a
    // parameter tree shaped like `params`, and `adam(params, g, state, lr, ...)` a parameter tree
    // and the next state: at every leaf the new value is a function of the old one, written over
    // the old leaf when nothing else holds it, so a training loop's step asks the runtime for no
    // new block per parameter (design principles 10.5; docs/design/arrays-memory/README.cn.md
    // 4.3.1 names this case -- "a training loop's optimizer, written as a pure function, updates
    // its parameters in place, with no donate_argnums hint"). The declaration is the one fact this
    // module writes about it, and it is what the memory plan reads to count the call as an update:
    // neither optimizer has an in-place form, because the value it updates is a *tree* walked leaf
    // by leaf (modules/nn/optim.cpp#walk), not one array `OperatorTable::setInPlaceForm` could
    // name. Both stay `pure`: what they change is the program's own value, and only where no one
    // else can see the old one (Array::writableInPlace at the leaf).
    //
    // `adam`'s state is a tree of two moments that is walked the same way, so the same declaration
    // covers it: the value handed in as `state` is the one the next state is written into.
    // `adam_state` declares nothing: it makes a state out of the *shape* of the tree it is given
    // and hands back nothing of it -- every moment is a new zero tensor (`zerosLeaf`) -- so there
    // is no operand of it to reuse.
    for (const char *uri : {"nn:sgd", "nn:adam"}) {
        camel::core::OperatorTable::instance().declareReusesOperand(uri);
    }
    // Writes a file.
    camel::core::OperatorTable::instance().declare("nn", {{"save_params", false}});
    // It writes files (docs/design/op-semantics 8).
    camel::core::OperatorTable::instance().declareResources(
        "nn",
        {{"save_params",
          camel::core::sem::externalAmbient("fs", camel::core::sem::AccessMode::ReadModifyWrite)}});
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new NnModule(ctx->shared_from_this()); }
}
