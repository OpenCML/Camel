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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The Camel enum `NodeKind` and its correspondence to GCNodeKind. The enum lists the kinds of
 * node a program can show, in the order of the table in nodekind.cpp. Bind and Dref exist only
 * while a graph is built, and Dead marks the place of a removed node: none of them is a node of
 * a graph a program reads, so none has a variant.
 */

#pragma once

#include "camel/core/type/enum.h"
#include "camel/runtime/graph.h"

#include <optional>

namespace camel::graphmod {

/// The enum type `NodeKind`.
camel::core::type::EnumType *nodeKindType();

/// The position of `kind` in `NodeKind`; nothing for a kind a program does not show. The
/// function names every GCNodeKind in a switch, so a kind added to the graph model stops the
/// build here until it is placed in the enum or left out on purpose.
std::optional<uint32_t> nodeKindTag(camel::runtime::GCNodeKind kind);

/// The name of the variant at `tag`.
const char *nodeKindName(uint32_t tag);

} // namespace camel::graphmod
