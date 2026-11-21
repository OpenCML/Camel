// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The `growth_budget` parameter of the passes built on the rewrite engine (camel/passes/opt/
 * config.h). It is read here rather than by each pass, because every one of them reads the same
 * three facts out of it: the nodes it may copy, whether the pipeline wrote it, and which pass owns
 * it -- the pass name a later reader reports (InlineReport::passName).
 */

#pragma once

#include "camel/execute/pass/params.h"
#include "camel/passes/opt/config.h"

#include <string>

namespace camel::execute {

/// The `growth_budget` parameter (design principles 9.3): the nodes specialization and inlining may
/// copy. Its declaration is in stdlib/std.cml.
GrowthBudget growthBudgetOf(const PassArgs &args, std::string passName);

} // namespace camel::execute
