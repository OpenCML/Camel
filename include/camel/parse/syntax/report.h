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
 * Author: Zhenjie Wei
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
