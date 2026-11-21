// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Internal helpers for bridging UserDefinedModule to the compile-side front end. These APIs are
 * intentionally kept out of the public module surface so runtime-facing code does not learn about
 * compile-graph details.
 *
 * The front end of a module written in Camel is the compiler's object
 * (`src/compile/module_source.cpp`). The kernel keeps it as an opaque handle and touches it only
 * through this class: what it opens here are the three things the loader needs of the module's
 * own state, and nothing in the kernel interprets the handle.
 */

#pragma once

#include <memory>

#include "camel/core/module/userdef.h"

namespace camel::core::module::detail {

/// The kernel's one door to the front end of a module written in Camel. Every member is a handle
/// the kernel does not interpret: the compiler makes it and reads it, and asks the kernel for the
/// few facts it keeps (the stage, the diagnostics).
class UserDefinedModuleAccess {
  public:
    /// The module's front end, as the loader left it: the compiler's object, kept opaque here.
    /// Null in a build with no loader and before one is attached.
    static std::shared_ptr<void> frontEnd(const UserDefinedModule &module);
    /// The root graph of the program the module is the entry of, as the loader handed it over
    /// (null before it is built): what `camel inspect` and the debugger draw.
    static std::shared_ptr<void> programGraph(const UserDefinedModule &module);
    /// Set while the module compiles a program of its own (see
    /// `UserDefinedModule::setProgramGraph`). Written only by the loader.
    static void setBuildingAProgramOfItsOwn(UserDefinedModule &module, bool on);
    /// The module's graph construction tree, opaque (null before it is built). Defined beside the
    /// loader, which is the only code that knows what the front end holds.
    static const void *constructionTree(const UserDefinedModule &module);
};

} // namespace camel::core::module::detail