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
 * Created: Feb. 22, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <string>

namespace camel::core::mm {
namespace profiler {

// Collect snapshots of all memory regions and return a JSON string.
std::string snapshotToJson();

// ---- Region view API (for the Debugger Web UI) ----

// Binary view: read the raw memory range [offset, offset+limit) of a region and return the raw
// byte array (unformatted).
// Only contiguous-memory regions are supported (bump: birthSpace/havenSpace/cacheSpace).
// Returns JSON: { "region", "offset", "limit", "capacity", "used", "data": [0..255], "hasMore" }.
// limit should not exceed 4096; formatting and pagination are handled by the Web UI.
std::string regionMemoryRawToJson(const char *regionName, size_t offset, size_t limit);

// Object view: return the object list for a region, paginated by [offset, offset+limit).
// Returns JSON: { "region", "objects": [ { "addr", "size", "age", "region" }, ... ], "total",
// "hasMore" }. Currently implemented only for bump-type regions; other types temporarily return
// an empty list (reserved for architecture work).
std::string regionObjectsToJson(const char *regionName, size_t offset, size_t limit);

} // namespace profiler
} // namespace camel::core::mm
