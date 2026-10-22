// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/passes/opt/generic.h"
#include "camel/runtime/writings.h"

#include <format>
#include <stdexcept>

const camel::runtime::Program *
ExtractPass::apply(const camel::runtime::Program *program, std::ostream &) {
    const auto model = camel::runtime::findCostModel(cost_);
    if (!model) {
        throw std::runtime_error(std::format("There is no cost model '{}'.", cost_));
    }
    return camel::runtime::extractWritings(context_, program, *model);
}
