/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
