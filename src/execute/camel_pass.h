// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Passes written in Camel (the graph module).
 *
 * A pass is a function `(f: F) => F!` exported by a module written in Camel. It is
 * named on the pipeline as `<module>::<function>`, like the passes of the C++ modules. The
 * module is compiled as a program of its own whose entry is that function, the tool program,
 * which the context keeps: its root graph holds the module's top-level values — computed before
 * the function runs, as a program computes them before `main` (design principles 10.1) — and
 * calls the function, whose captures are the closure of the function value that call makes. The
 * program being processed is the argument, a value of the tool program's run. The tool program
 * runs on NodeVM.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics/range.h"

#include <optional>
#include <string>
#include <vector>

namespace camel::execute {

/// Why a `<module>::<pass>` name is not a pass, when the module is there: the passes the
/// module does provide, and whether it declares the name without exporting it. A module that is
/// not there, or that cannot be loaded or compiled, has its own answer (`problem`).
struct MissingCamelPass {
    /// The module is there and compiles, and the name is not one of its passes.
    bool moduleHasNoSuchPass = false;
    /// It declares the name as a function but does not export it.
    bool declaresUnexported = false;
    /// The passes it does provide, by name (empty when it provides none).
    std::vector<std::string> provided;
};

/// Registers the pass `<module>::<function>` when the module `module` exports a function of that
/// name with the signature of a pass. Nothing is registered when the module is not there or
/// exports no such function (`problem` is empty: the pass is unknown), or when it is there and
/// refused (it does not compile, it is not a pass), and then `problem` says why; `errorLocation`,
/// when given, is the library's first error as a secondary location when it has one.
/// `missing`, when given, receives what the module provides and whether it declares the name
/// without exporting it, when the module is there and the name is not one of its passes: the
/// caller then reports the name against that list.
void registerCamelPass(
    const camel::core::context::context_ptr_t &ctx, const std::string &module,
    const std::string &function, std::string &problem,
    std::optional<camel::core::error::DiagLabel> *errorLocation = nullptr,
    MissingCamelPass *missing                                  = nullptr);

} // namespace camel::execute
