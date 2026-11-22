// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "budget.h"

namespace camel::execute {

GrowthBudget growthBudgetOf(const PassArgs &args, std::string passName) {
    GrowthBudget budget;
    const int64_t nodes = args.getInt("growth_budget");
    budget.nodes        = nodes < 0 ? 0 : static_cast<size_t>(nodes);
    budget.written      = args.given("growth_budget");
    budget.passName     = std::move(passName);
    return budget;
}

} // namespace camel::execute
