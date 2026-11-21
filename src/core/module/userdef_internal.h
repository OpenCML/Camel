// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Internal helpers for bridging UserDefinedModule to compile/runtime graph
 * carriers. These APIs are intentionally kept out of the public module
 * surface so runtime-facing code does not learn about compile-graph details.
 */

#pragma once

#include <memory>

#include "camel/core/module/userdef.h"

namespace camel::runtime {
class Program;
}

namespace camel::core::module::detail {

/// The kernel's one door to the front end of a module written in Camel. Every member is a handle
/// the kernel does not interpret: the compiler (`src/compile/module_source.cpp`) makes them and
/// reads them, and asks the kernel for the few facts it keeps (the stage, the diagnostics).
class UserDefinedModuleAccess {
  public:
    static const void *compileGraphOpaque(const UserDefinedModule &module);
    /// Encodes the module's graphs into `program` (see camel/compile/gir/encode.h).
    static void
    encodeRuntimeProgram(const UserDefinedModule &module, camel::runtime::Program &program);
    /// The reader the module's source was read with, as it was handed over (null when it has
    /// none and before it is read): the compiler's object, kept opaque here.
    static std::shared_ptr<void> sourceReader(const UserDefinedModule &module);
    /// The module's graph construction tree, opaque (null before it is built).
    static const void *constructionTree(const UserDefinedModule &module);
};

} // namespace camel::core::module::detail