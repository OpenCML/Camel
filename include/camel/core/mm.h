// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

GenerationalAllocatorWithGC &autoSpace();
GrowableFreeListAllocator &metaSpace();
GrowableFreeListAllocator &graphSpace();
GrowableBumpAllocator &permSpace();

/// The data block space (core/mm/block.h). A non-moving free-list space, like the meta space:
/// a data pointer handed to a kernel, DLPack or Python stays valid until the last reference
/// drops. Its limit is `--block-limit <megabytes>` (default 2048 MB), chosen before the space
/// exists; like the other space limits it only catches a runaway allocation, and a program that
/// builds several large models in one process legitimately needs it higher.
constexpr size_t kDefaultBlockSpaceLimitBytes = 2048 * MB;
GrowableFreeListAllocator &blockSpace();

/// Selects the limit of the block space for this process (`--block-limit`). It has to be called
/// before the block space is first used; it returns false, and changes nothing, when that has
/// already happened (the space is laid out once).
bool selectBlockSpaceLimit(size_t bytes);

/// One line with the occupancy of the graph, permanent and meta spaces (`[camel] spaces: ...`):
/// bytes in use now and at the high-water mark, bytes reserved, segments, allocations, limit.
/// Printed at exit after the collector's line when the statistics were turned on with
/// `std::profile gc_stats=...`.
void reportSpaceUsage(std::ostream &os);

/// Which collector the auto space runs.
enum class CollectorMode {
    MarkSweep,    // Non-moving mark-sweep: every object keeps its address (--gc mark-sweep).
    Generational, // Young objects are copied (they move); old ones are marked and swept. Default.
};

/// Asks for the allocator's statistics at exit, the way `std::profile gc_stats=<level>` does:
/// 1 prints one line of pacing statistics and the occupancy of the spaces, 2 also prints one
/// line per major collection. May be called before the auto space exists (the request is kept)
/// or after it (it is applied at once).
void enableStatsReport(int level);

/// True once enableStatsReport asked for the statistics; other diagnostics (the FastVM's code
/// counters) print their line under the same switch.
bool statsReportEnabled();

/// Selects the collector for this process. It has to be called before the auto space is first
/// used (the spaces are laid out once); it returns false, and changes nothing, when that has
/// already happened.
bool selectCollectorMode(CollectorMode mode);

/// The collector the auto space runs, or will run when it is first used.
CollectorMode collectorMode();

/// "mark-sweep" / "generational", and the reverse (nullopt for anything else).
const char *collectorModeName(CollectorMode mode);
std::optional<CollectorMode> parseCollectorMode(std::string_view text);

/// How the allocator is sized: the collector's spaces and the block space's first segment. A zero
/// is "the default", the meaning these numbers have always had (`--gc-nursery-mb`,
/// `--gc-promotion-age`, `--gc-large-threshold-kb`, `--gc-large-budget-mb`,
/// `--gc-growth-percent`, `--gc-heap-limit-mb`, `--space-segment-kb`).
struct AllocatorSizing {
    size_t nurseryMb        = 0; // the birth space, default 4 MB
    size_t promotionAge     = 0; // minor collections before promotion, default 4 (1 to 15)
    size_t largeThresholdKb = 0; // what counts as a large object, default 64 KB (4 KB mark-sweep)
    size_t largeBudgetMb    = 0; // large-object bytes between collections, default 4 MB
    size_t growthPercent    = 0; // growth of the large-object space, default 100 %
    size_t heapLimitMb      = 0; // the heap limit, default none
    size_t spaceSegmentKb   = 0; // the first segment of every growable space, default as it was
    size_t metaSpaceLimitMb = 0; // the meta space's limit (--meta-space-limit-mb)
    size_t graphSpaceLimitMb = 0; // the graph space's limit (--graph-space-limit-mb)
    size_t permSpaceLimitMb = 0; // the permanent space's limit (--perm-space-limit-mb)
};

/// Selects the sizing for this process. Like `selectCollectorMode`, it has to be called before
/// the auto space is first used and returns false when that has already happened.
bool selectAllocatorSizing(const AllocatorSizing &sizing);

/// What a run asks the collector to do beyond collecting: the stress collection and the readings
/// (`--gc-stress`, `--gc-check`). They are what was set from the environment, made settable
/// before the heap exists.
struct AllocatorChecks {
    /// Which collection a stress point performs.
    enum class Stress { None, Minor, Major, Both };

    Stress stress                 = Stress::None;
    size_t stressEveryAllocations = 0; // collect every N allocations (0: never)
    size_t stressEverySafepoints  = 0; // collect every N safepoints (0: never)
    bool logMoves                 = false; // one line per moved object
    bool poison                   = false; // write over the from-space at every safepoint
    bool verifyBefore             = false; // check the invariants before a collection
    bool verifyAfter              = false; // ... and after
    bool printConfig              = false; // print the resolved configuration at start
};

/// Selects the checks for this process, under the same rule as the sizing.
bool selectAllocatorChecks(const AllocatorChecks &checks);

} // namespace camel::core::mm
