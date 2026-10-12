/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * What a module's operators are, as data: name, URI, parameter counts, signature and the
 * declared purity. Used by `camel inspect --ops` (and through it by the tests that hold the
 * signature files and the purity declarations to the operators actually registered) and by
 * the notice a module gets when it leaves the purity of an operator undeclared.
 */

#pragma once

#include "camel/core/module/module.h"

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace camel::core::module {

/// Whether an operator was declared pure, declared to have effects, declared to touch only the
/// program's own variables, or not declared at all (then treated as having effects).
enum class OperatorPurity { Undeclared, Pure, Effectful, StateOnly };

std::string_view to_string(OperatorPurity purity);

struct OperatorDescription {
    std::string module;
    /// The name the module exports the operator group under.
    std::string name;
    /// One overload of the group.
    std::string uri;
    std::string signature;
    /// Parameter counts; -1 for any number, -2 when the resolver does not tell.
    int withCount = -2;
    int normCount = -2;
    OperatorPurity purity = OperatorPurity::Undeclared;
};

/// Every overload of every operator group `module` exports, sorted by name and URI. The module
/// must be loaded: a module declares the purity of its operators when it is loaded at the latest.
std::vector<OperatorDescription> describeOperators(const Module &module);

/// One line `OPERATOR {json}` per overload.
void dumpOperators(const std::vector<module_ptr_t> &modules, std::ostream &os);

/// Logs a warning naming the operators of `module` whose purity is not declared; returns how
/// many there are.
size_t warnUndeclaredOperatorTraits(const Module &module);

} // namespace camel::core::module
