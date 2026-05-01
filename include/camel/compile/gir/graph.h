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
 * Created: Apr. 12, 2026
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Deprecated include shim.
 *
 * Compile-time graph semantics have moved to `DraftGraphBuilder` plus
 * `GraphDraft`. This header intentionally stops exposing the old `Graph`
 * surface so remaining users fail at concrete migration points instead of
 * depending on stale APIs.
 */

#pragma once

#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/compile/gir/types.h"
