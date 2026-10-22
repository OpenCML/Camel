// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Deprecated include shim.
 *
 * Compile-time node semantics have moved to `DraftNodeRef` handles plus direct
 * `GraphDraft` queries. This header intentionally provides no legacy node
 * classes so downstream migration happens explicitly at each remaining use.
 */

#pragma once

#include "camel/compile/gir/types.h"
