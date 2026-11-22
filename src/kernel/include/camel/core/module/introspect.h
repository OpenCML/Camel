// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What a module's operators are, as data: name, URI, parameter counts, signature, the
 * declared purity and the declared semantics (camel/core/operator_semantics.h). Used by `camel inspect --ops` (and through it by the tests that hold the
 * signature files and the purity declarations to the operators actually registered) and by
 * the notice a module gets when it leaves the purity of an operator undeclared.
 */

#pragma once

#include "camel/core/module/module.h"
#include "camel/core/operator_semantics.h"

#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace camel::core::module {

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
    /// The effect, from the operator's one statement of it (`sem::Effect`): the same four answers
    /// the access list gives every other reader, so a listing cannot state a purity the operators
    /// themselves do not have. `camel inspect --ops` prints it as its "purity" member
    /// (`sem::effectName`).
    sem::Effect effect = sem::Effect::Undeclared;
    /// The declared semantics (owned by the operator table), nullptr when none is declared.
    const sem::OperatorSemantics *semantics = nullptr;
    /// Whether the module declares the group without exporting it: the kernel of an `#impl`
    /// function (design principles 10.4). Its own bodies call it, an importer cannot, and the
    /// listing says so ("private": true) instead of hiding it.
    bool isPrivate = false;
};

/// Every overload of every operator group `module` exports, and of the groups it declares
/// privately (isPrivate), sorted by name and URI. The module must be loaded: a module declares
/// the purity of its operators when it is loaded at the latest.
std::vector<OperatorDescription> describeOperators(const Module &module);

/// One line `OPERATOR {json}` per overload. An operator with declared semantics has a member
/// "semantics": {"key", "numeric", "compute", "ulp" (bounded only), "sensitive_to" (a
/// list of permission names, finite_math for its four together), "not_ieee" (the special
/// values it does not treat as IEEE does, when there are any), "faults" (when declared),
/// "indexing": true (when it declares index semantics; `camel inspect --instances` shows them
/// per node), "laws" (the names of its reduction laws, when it has any), "algorithm"}.
void dumpOperators(const std::vector<module_ptr_t> &modules, std::ostream &os);

/// Logs a warning naming the operators of `module` whose purity is not declared; returns how
/// many there are.
size_t warnUndeclaredOperatorTraits(const Module &module);

} // namespace camel::core::module
