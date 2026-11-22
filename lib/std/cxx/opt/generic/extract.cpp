// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/execute/pass/registration.h"
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

namespace {

using camel::execute::PassKernel;

const PassKernel extractKernel(
    "std::cxx::extract",
    [](const camel::core::context::context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<ExtractPass>(ctx, args.getString("cost"));
    });

} // namespace
