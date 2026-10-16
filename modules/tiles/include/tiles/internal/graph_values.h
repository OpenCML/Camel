/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Typed aggregate construction shared by GPU wrappers and extracted regions.
#pragma once
#include "camel/runtime/draft.h"
#include <span>
namespace camel::tiles {
// Caller keeps all referenced values rooted and holds NoCollectionScope.
runtime::gc_node_ref_t
packGraphTuple(runtime::GraphDraft &draft, std::span<const runtime::gc_node_ref_t> values);
} // namespace camel::tiles
