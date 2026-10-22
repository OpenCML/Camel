// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Internal static-region artifact and fresh-input binding boundary.

#pragma once
#include "tiles/program.h"

#include <vector>
namespace camel::tiles {
Program compileRegionTemplate(Function *function, runtime::fact::FactStore &facts);
void bindRegionArguments(
    Program &program, const std::vector<Argument> &arguments, runtime::fact::FactStore &facts);
Program prepareProgram(
    Function *function, const std::vector<Argument> &arguments, runtime::fact::FactStore &facts,
    bool staticRegion = false);
// Per-thread diagnostic for contract tests; never emits runtime output.
size_t compilationExtractions();
bool hasCompiledRegion(Function *function, runtime::fact::FactStore &facts);
} // namespace camel::tiles
