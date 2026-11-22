// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Text dumps of the token buffer and of the concrete syntax tree: what `camel inspect --tok`
 * and `camel inspect --cst` print.
 */

#pragma once

#include "cst.h"

#include <iosfwd>
#include <ostream>

namespace camel::parse::syntax {

/// One line per node and per token, drawn as a tree. Comments are not children of any node and
/// are not shown.
void dumpTree(std::ostream &os, const Tree &tree);

/// One line per token, comments included: index, line:column, what precedes it (`n` a line
/// break, `s` a space or a comment, `-` nothing), kind, text.
void dumpTokens(std::ostream &os, const TokenBuffer &buffer);

} // namespace camel::parse::syntax
