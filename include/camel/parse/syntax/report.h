// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Turns the errors a lexer and a parser recorded into diagnostics (see report.cpp).
 */

#pragma once

#include "camel/core/error/diagnostics.h"
#include "cst.h"

namespace camel::parse::syntax {

/// One diagnostic per lexical error: an illegal character (a run of them is one error), an
/// unclosed string, an unterminated comment.
void reportLexicalErrors(const TokenBuffer &buffer, camel::core::error::Diagnostics &diagnostics);

/// One diagnostic per syntax error of the tree, under the most specific code that applies.
void reportSyntaxErrors(const Tree &tree, camel::core::error::Diagnostics &diagnostics);

} // namespace camel::parse::syntax
