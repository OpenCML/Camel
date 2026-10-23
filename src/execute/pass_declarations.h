// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The declarations a module makes of its passes (design principles 9.3, H233). A pass is a
// body-less function of the module's `.cml` file; the pipeline resolves a pass by the module it
// belongs to and the name of that function. Nothing in C++ declares a pass's name, parameters
// or documentation any more.

#pragma once

#include "camel/core/context/context.h"
#include "camel/execute/pass/params.h"

#include <string>
#include <vector>

namespace camel::execute::detail {

/// One pass as the standard library declares it.
struct PassDeclaration {
    /// The name inside its module: `opt::inline`, `export_model`.
    std::string name;
    /// The `///` text above the declaration.
    std::string doc;
    std::vector<PassParam> params;
    /// The condition after `when`, as written; empty when the declaration has none.
    std::string when;
};

/// The pass declarations of `module` (`std` for the core), read from `<module>.cml` under the
/// context's module search paths. Empty when the module has no declaration file.
const std::vector<PassDeclaration> &passDeclarations(
    const std::string &module, const camel::core::context::context_ptr_t &ctx);

/// The declaration of `name` in `module`, or null.
const PassDeclaration *findPassDeclaration(
    const std::string &module, const std::string &name,
    const camel::core::context::context_ptr_t &ctx);

/// Whether the declaration's applicability condition holds for the value the pipeline applies a
/// pass to: the program's root function. The condition is evaluated, not ignored: one the
/// pipeline cannot show to hold is refused, and `why` says which.
bool passDeclarationApplies(const PassDeclaration &declaration, std::string &why);

/// The file the declarations of `module` are read from, or empty when there is none.
std::string moduleDeclarationPath(
    const std::string &module, const camel::core::context::context_ptr_t &ctx);

} // namespace camel::execute::detail