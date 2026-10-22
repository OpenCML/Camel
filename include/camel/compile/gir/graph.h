// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
