// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What a tool reads of a module written in Camel beyond the graph the kernel hands out: the
 * reader its source was read with (the parser, with the trees and the token positions a tool
 * describes a file from) and the graph construction tree.
 *
 * The module itself is a kernel object and names neither: `UserDefinedModule` keeps both behind
 * opaque handles (`core/module/userdef_internal.h`), so the kernel includes neither the parser nor
 * the compiler. A tool that needs them includes this header, which supplies the two types and the
 * casts, and nothing in the kernel gains the dependency back.
 */

#pragma once

#include <memory>

#include "camel/compile/gct.h"
#include "camel/core/module/userdef.h"

namespace camel::parse {
class CamelParser;
}

namespace camel::compile {

/// The reader of `module`'s source: the parser whose abstract syntax tree, concrete syntax tree,
/// source file id and token positions a tool reads a file with. Null for a module that has no
/// source and before its source is read.
std::shared_ptr<camel::parse::CamelParser>
parserOf(const camel::core::module::UserDefinedModule &module);

/// The graph construction tree of `module`: what it declares, before any graph is built. Null
/// before it is built.
const camel::compile::gct::Tree *
treeOf(const camel::core::module::UserDefinedModule &module);

} // namespace camel::compile