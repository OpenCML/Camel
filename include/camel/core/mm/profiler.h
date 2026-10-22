// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

namespace camel::core::mm {
namespace profiler {

// Collect snapshots of all memory regions and return a JSON string.
// Set detailed=false for aggregate-only inspection without building object arrays.
std::string snapshotToJson(bool detailed = true);

// Return a compact JSON object describing GC mode, stress, and verification settings.
std::string configToJson();

// Return a short human-readable GC summary for terminal diagnostics.
std::string summaryToText();

// ---- Region view API (for debugger clients) ----

// Binary view: read the raw memory range [offset, offset+limit) of a region and return the raw
// byte array (unformatted).
// Only contiguous-memory regions are supported (bump: birthSpace/havenSpace/cacheSpace).
// Returns JSON: { "region", "offset", "limit", "capacity", "used", "data": [0..255], "hasMore" }.
// limit should not exceed 4096; formatting and pagination are handled by the client.
std::string regionMemoryRawToJson(const char *regionName, size_t offset, size_t limit);

// Object view: return the object list for a region, paginated by [offset, offset+limit).
// Returns JSON: { "region", "objects": [ { "addr", "size", "age", "region" }, ... ], "total",
// "hasMore" }. Currently implemented only for bump-type regions; other types temporarily return
// an empty list (reserved for architecture work).
std::string regionObjectsToJson(const char *regionName, size_t offset, size_t limit);

} // namespace profiler
} // namespace camel::core::mm
