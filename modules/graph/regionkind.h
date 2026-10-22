// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The Camel enum `RegionKind` and its correspondence to GCRegionKind. It has a variant for
 * every kind the graph model names, so a kind added to GCRegionKind stops the build in
 * regionKindTag until it is placed here. `Dead` is a region a rewrite removed: a program that
 * reads the region of a node of a live graph cannot get one, but the enum stays exhaustive.
 */

#pragma once

#include "camel/core/type/enum.h"
#include "camel/runtime/graph.h"

#include <optional>

namespace camel::graphmod {

/// The enum type `RegionKind`.
camel::core::type::EnumType *regionKindType();

/// The position of `kind` in `RegionKind`.
std::optional<uint32_t> regionKindTag(camel::runtime::GCRegionKind kind);

/// The name of the variant at `tag`.
const char *regionKindName(uint32_t tag);

} // namespace camel::graphmod