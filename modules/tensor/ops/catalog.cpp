// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Registration of the tensor module's operator catalog.
 */

#include "catalog.h"
#include "binary_ops.h"
#include "registry.h"

#include "../type.h"
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
            {"seed", externalAmbient("tensor:generator", AccessMode::Write)},
            {"random", externalAmbient("tensor:generator", AccessMode::ReadModifyWrite)},
            {"randn", externalAmbient("tensor:generator", AccessMode::ReadModifyWrite)},
            {"show", externalAmbient("stdout", AccessMode::Write)},
            {"load_npy", externalAmbient("fs", AccessMode::Read)},
            {"save_npy", externalAmbient("fs", AccessMode::ReadModifyWrite)},
            {"set_num_threads", externalAmbient("tensor:threads", AccessMode::Write)},
            {"num_threads", externalAmbient("tensor:threads", AccessMode::Read)},
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
        registerTensorTangentSpace();
        // The core's shape facts (core.shape, camel/runtime/core_facts.h) read a tensor's
        // element type and extents from its type, which this module defines.
        camel::runtime::fact::registerShapeReader(
            [](const camel::core::type::Type *type)
                -> std::optional<camel::runtime::fact::TensorShape> {
                const TensorType *tensor = asTensorType(type);
                if (tensor == nullptr) {
                    return std::nullopt;
                }
                return camel::runtime::fact::TensorShape{
                    .dtype = tensor->dtype(),
                    .dims  = tensor->shape()};
            });
    });
}

} // namespace camel::tensor::ops
