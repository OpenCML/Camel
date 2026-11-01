// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Registration of the tensor module's operator catalog.
 */

#include "catalog.h"
#include "binary_ops.h"
#include "registry.h"

#include "../type.h"
#include "camel/core/forward.h"
#include "camel/core/operator_table.h"
#include "camel/runtime/core_facts.h"

#include <mutex>

namespace camel::tensor::ops {

namespace {

/**
 * What the operators that are not pure touch (docs/design/op-semantics 8): the module's
 * generator, the console, the file system, the size of its thread pool. And the operators
 * whose normative definition is written in Camel (docs/design/op-semantics 9): the functions
 * of stdlib/tensor_definitions.cml, compiled when a definition is first asked for. After the
 * operators' traits and semantics, which a declaration is checked against and adds to.
 */
void declareResourcesAndDefinitions() {
    using namespace camel::core::sem;
    auto &table = camel::core::OperatorTable::instance();
    table.declareResources(
        "tensor",
        {
            {"show", externalAmbient("stdout", AccessMode::Write)},
            {"load_npy", externalAmbient("fs", AccessMode::Read)},
            {"save_npy", externalAmbient("fs", AccessMode::ReadModifyWrite)},
            {"set_num_threads", externalAmbient("tensor:threads", AccessMode::Write)},
            {"num_threads", externalAmbient("tensor:threads", AccessMode::Read)},
            {"gemm_threads", externalAmbient("tensor:threads", AccessMode::Read)},
        });
    const auto operand = [](uint8_t index,
                            std::optional<DefinitionDefault> fallback = std::nullopt) {
        return DefinitionParam{
            .kind     = DefinitionParam::Kind::Norm,
            .operand  = index,
            .fallback = fallback};
    };
    const std::string library = "tensor_definitions";
    // softmax(t, axis = -1), log_softmax(t, axis = -1): the axis may be left out.
    for (const char *name : {"softmax", "log_softmax"}) {
        table.declareDefinition(
            std::string("tensor:") + name,
            Definition{
                .module   = library,
                .function = name,
                .params   = {operand(0), operand(1, int64_t{-1})}});
    }
    for (const char *name : {"mean", "relu", "sigmoid"}) {
        table.declareDefinition(
            std::string("tensor:") + name,
            Definition{.module = library, .function = name, .params = {operand(0)}});
    }
}

/**
 * The keys this module's batching rules are filed under. A rule is filed in `rulesets.vmap` under
 * the head of its left side and the number of operands that head takes (`matmul(x, w)` is `matmul`
 * at two, `sum_axis(xs, axis, keepdims)` is `sum_axis` at three), and the key is a property of the
 * operator -- not of the family that happens to hold a rule for it -- so the operator's own module
 * records it. What a batching transform reads from here is the key, never the rule: the rule's name is
 * read from the family's index at the moment it is asked (camel/core/forward.h), so this table cannot
 * drift from the ruleset that is written.
 *
 * `matmul` is keyed by the call, because that is the spelling its rule is written with
 * (stdlib/rulesets/vmap.cml): the same primitive is also spelled `x @ w`, and a rule written that way
 * would be filed under `@` at two -- one head per rule, and the head the ruleset writes is the one
 * this table names. The reductions take their own names, because they are ordinary functions of an
 * axis -- there is no operator to spell them with.
 *
 * `argmax_axis` and `argmin_axis` are here for the reason the transform reads the rule's type off an
 * operand: they answer `int64` positions over any numeric operand, so their rules are generic in the
 * operand's element and the key alone would not say which type the instance is made at.
 *
 * Only the operators whose batched meaning is a rule this ruleset writes have a key: the six
 * reductions along an axis, and the six over every axis. The four that fold (`tensor:sum`,
 * `tensor:mean`, `tensor:max`, `tensor:min`) keep the batch axis in front and fold away the axes the
 * row had after it; `tensor:argmax` and `tensor:argmin` answer a position that no fold produces, so
 * their rule reads the batch as one line (`reshape<[shape(xs)[0], -1]>(xs)`) and reduces its last
 * axis, which is one row's elements.
 */
void declareVmapRuleKeys() {
    camel::core::bindVmapRule({"tensor:matmul", "rulesets.vmap", "matmul", 2});
    camel::core::bindVmapRule({"tensor:sum", "rulesets.vmap", "sum", 1});
    camel::core::bindVmapRule({"tensor:mean", "rulesets.vmap", "mean", 1});
    camel::core::bindVmapRule({"tensor:max", "rulesets.vmap", "max", 1});
    camel::core::bindVmapRule({"tensor:min", "rulesets.vmap", "min", 1});
    camel::core::bindVmapRule({"tensor:argmax", "rulesets.vmap", "argmax", 1});
    camel::core::bindVmapRule({"tensor:argmin", "rulesets.vmap", "argmin", 1});
    camel::core::bindVmapRule({"tensor:sum_axis", "rulesets.vmap", "sum_axis", 3});
    camel::core::bindVmapRule({"tensor:mean_axis", "rulesets.vmap", "mean_axis", 3});
    camel::core::bindVmapRule({"tensor:max_axis", "rulesets.vmap", "max_axis", 3});
    camel::core::bindVmapRule({"tensor:min_axis", "rulesets.vmap", "min_axis", 3});
    camel::core::bindVmapRule({"tensor:argmax_axis", "rulesets.vmap", "argmax_axis", 3});
    camel::core::bindVmapRule({"tensor:argmin_axis", "rulesets.vmap", "argmin_axis", 3});
    camel::core::bindVmapRule({"tensor:reshape", "rulesets.vmap", "reshape", 1});
    camel::core::bindVmapRule({"tensor:unsqueeze", "rulesets.vmap", "unsqueeze", 1});
    camel::core::bindVmapRule({"tensor:transpose", "rulesets.vmap", "transpose", 1});
    camel::core::bindVmapRule({"tensor:permute", "rulesets.vmap", "permute", 1});
    camel::core::bindVmapRule({"tensor:concat", "rulesets.vmap", "concat", 2});
    camel::core::bindVmapRule({"tensor:dim", "rulesets.vmap", "dim", 1});
}

} // namespace

void registerTensorOps() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::vector<OpDef> defs;
        for (auto family :
             {elementwiseOps,
              creationOps,
              layoutOps,
              reductionOps,
              linalgOps,
              utilityOps,
              binaryOps,
              gradientOps}) {
            for (OpDef &def : family()) {
                defs.push_back(std::move(def));
            }
        }
        attachSemantics(defs);
        OpRegistry::instance().add("tensor", std::move(defs));
        declareResourcesAndDefinitions();
        declareVmapRuleKeys();
        registerTensorTangentSpace();
        // The core's `:op/idx_take` is differentiated by the core's own rule, registered with the
        // builtins when the context is built: this module publishes no rule for that URI, so one
        // rule answers it whatever the load order (design principles 5.4).
        // `t[a:b:s]` and `slice(t, axis, a, b, s)` are one node of the core's, and the read it
        // declares is a tensor's: this module publishes that declaration (indexing.cpp).
        registerCoreSliceIndexing();
        // The core's shape facts (core.shape, camel/runtime/core_facts.h) read a tensor's
        // element type and extents from its type, which this module defines.
        camel::runtime::fact::registerShapeReader(
            [](const camel::core::type::Type *type)
                -> std::optional<camel::runtime::fact::TensorShape> {
                if (!isTensorType(type)) {
                    return std::nullopt;
                }
                const TensorFacts facts = tensorFactsOf(type);
                return camel::runtime::fact::TensorShape{
                    .dtype = facts.dtype,
                    .dims  = facts.shape};
            });
    });
}

} // namespace camel::tensor::ops
