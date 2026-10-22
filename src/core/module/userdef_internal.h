// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Internal helpers for bridging UserDefinedModule to compile/runtime graph
 * carriers. These APIs are intentionally kept out of the public module
 * surface so runtime-facing code does not learn about compile-graph details.
 */

#pragma once

#include "camel/core/module/userdef.h"

namespace camel::runtime {
class Program;
}

namespace camel::core::module::detail {

class UserDefinedModuleAccess {
  public:
    static const void *compileGraphOpaque(const UserDefinedModule &module);
    /// Encodes the module's graphs into `program` (see camel/compile/gir/encode.h).
    static void
    encodeRuntimeProgram(const UserDefinedModule &module, camel::runtime::Program &program);
};

} // namespace camel::core::module::detail
