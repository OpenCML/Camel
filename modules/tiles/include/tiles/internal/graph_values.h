// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Typed aggregate construction shared by GPU wrappers and extracted regions.

#pragma once
#include "camel/runtime/draft.h"
#include <span>
namespace camel::tiles {
// Preserve aggregate structure, wrapping only tensor/f64 leaves. Null means unsupported.
core::type::Type *residentType(core::type::Type *type);
// Construct a fully bound tuple or struct with the supplied aggregate type.
runtime::gc_node_ref_t packGraphAggregate(
    runtime::GraphDraft &draft, core::type::Type *type,
    std::span<const runtime::gc_node_ref_t> values);
// Caller keeps all referenced values rooted and holds NoCollectionScope.
runtime::gc_node_ref_t
packGraphTuple(runtime::GraphDraft &draft, std::span<const runtime::gc_node_ref_t> values);
} // namespace camel::tiles
