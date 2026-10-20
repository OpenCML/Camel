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
 * Created: Nov. 07, 2025
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <optional>
#include <string_view>
#include "mm/alloc/bump_ptr.h"
#include "mm/alloc/free_list.h"
#include "mm/alloc/growable.h"
#include "mm/foreign_handle.h"
#include "mm/gen.h"
#include "mm/root_handle.h"

#include <iosfwd>

namespace camel::core::mm {

// Limits of the growable spaces: the default total and the environment variable that
// overrides it, in megabytes. A function takes about 1 to 2 KB of the graph space, 0.6 to 2 KB
// of the permanent space and, when its graph owns a constant, about 1.2 KB of the meta space,
// so the defaults hold programs of more than a hundred thousand functions. They are kept this
// low because a limit is also what stops a pass that never stops growing the program (an
// unrolling that does not end): the time to reach it grows faster than the limit.
constexpr size_t kDefaultGraphSpaceLimitBytes = 256 * MB;
constexpr size_t kDefaultPermSpaceLimitBytes  = 256 * MB;
constexpr size_t kDefaultMetaSpaceLimitBytes  = 256 * MB;
constexpr const char *kGraphSpaceLimitEnv     = "CAMEL_GRAPH_SPACE_LIMIT_MB";
constexpr const char *kPermSpaceLimitEnv      = "CAMEL_PERM_SPACE_LIMIT_MB";
constexpr const char *kMetaSpaceLimitEnv      = "CAMEL_META_SPACE_LIMIT_MB";

GenerationalAllocatorWithGC &autoSpace();
GrowableFreeListAllocator &metaSpace();
GrowableFreeListAllocator &graphSpace();
GrowableBumpAllocator &permSpace();

/// Numeric array buffers (core/array/buffer.h). A non-moving free-list space, like the meta
/// space: a data pointer handed to a kernel or to DLPack stays valid until the last reference
/// drops. Its limit is CAMEL_BUFFER_LIMIT_MB, default 256 MB, and can be raised like the
/// other spaces.
constexpr const char *kBufferSpaceLimitEnv = "CAMEL_BUFFER_LIMIT_MB";
GrowableFreeListAllocator &bufferSpace();

/// One line with the occupancy of the graph, permanent and meta spaces (`[camel] spaces: ...`):
/// bytes in use now and at the high-water mark, bytes reserved, segments, allocations, limit.
/// Printed at exit after the collector's line when CAMEL_GC_STATS is set.
void reportSpaceUsage(std::ostream &os);

/// Which collector the auto space runs.
enum class CollectorMode {
    MarkSweep,    // Non-moving mark-sweep: every object keeps its address. The default.
    Generational, // Young objects are copied (they move); old ones are marked and swept.
};

/// The environment variable that selects the collector: `generational` (or 1) or `mark-sweep`
/// (or 0). `--gc <mode>` on the command line wins over it.
constexpr const char *kCollectorModeEnv = "CAMEL_GC";

/// Selects the collector for this process. It has to be called before the auto space is first
/// used (the spaces are laid out once); it returns false, and changes nothing, when that has
/// already happened.
bool selectCollectorMode(CollectorMode mode);

/// The collector the auto space runs, or will run when it is first used.
CollectorMode collectorMode();

/// "mark-sweep" / "generational", and the reverse (nullopt for anything else).
const char *collectorModeName(CollectorMode mode);
std::optional<CollectorMode> parseCollectorMode(std::string_view text);

} // namespace camel::core::mm
