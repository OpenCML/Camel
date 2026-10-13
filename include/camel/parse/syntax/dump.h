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
 * Text dumps of the token buffer and of the concrete syntax tree: what `camel inspect --tok`
 * and `camel inspect --cst` print.
 */

#pragma once

#include "cst.h"

#include <iosfwd>

namespace camel::parse::syntax {

/// One line per node and per token, drawn as a tree. Comments are not children of any node and
/// are not shown.
void dumpTree(std::ostream &os, const Tree &tree);

/// One line per token, comments included: index, line:column, what precedes it (`n` a line
/// break, `s` a space or a comment, `-` nothing), kind, text.
void dumpTokens(std::ostream &os, const TokenBuffer &buffer);

} // namespace camel::parse::syntax
