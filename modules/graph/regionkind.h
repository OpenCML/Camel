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