// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The lifetime of a `Context`, read from a second one built beside the running one.
 *
 * A process may hold several contexts (a second `Context::create` beside the running one is what a
 * tool or a language server does), and each of them owns the modules, the executor manager and the
 * builders it made. Those objects refer back to the context they belong to, so the reference they
 * hold must not be an owning one: a context its own members hold is kept alive by itself, never
 * reaches its destructor, and then everything its destructor does -- giving back the object root
 * set and the graph sweeper it registered, draining the deferred releases its teardown queued --
 * happens only for a context that nothing else keeps, which is never a context a program built.
 *
 * This pass reads that lifetime. It builds a second context in a scope of its own, reads how many
 * owners the second context has while the scope holds one of them, ends the scope, and reads what
 * the teardown left behind. One line for the test plan:
 *
 *   CONTEXTS-PROBE held=<n> live_before=<n> live_second=<n> live_after=<n> rootset_before=<n>
 *                  rootset_second=<n> rootset_after=<n> root_given_back=<0|1>
 *                  deferred_before=<0|1> deferred_after=<0|1> ran=<0|1> alive=<0|1>
 *
 * `held` is the second context's owner count while the scope holds one of them: 1 means the scope
 * is its only owner and it is destroyed at the end of the scope, anything more means something
 * else keeps it alive and its destructor never runs. `live_before` / `live_second` / `live_after`
 * are the contexts alive at those three points (`liveContextCount()`), so `live_after` going back
 * to `live_before` is the second context having been destroyed.
 *
 * `rootset_*` are how many context root sets the allocator holds -- read before the second context
 * is built, while both are alive and after the scope. It keeps one entry per owner
 * (`registerObjectRootSet` and `unregisterObjectRootSet`, mm/gen.h), so the running context's entry
 * is there throughout, the second context adds its own while it lives, and its destructor gives
 * back its own and only its own: `root_given_back` is the second context's entry no longer being
 * registered.
 *
 * The deferred-release queue is one per process and drains where a collection ends and, for a
 * context being destroyed, in its destructor. The pass queues a release of its own inside the
 * scope, where no collection starts before the scope ends: `deferred_before` is it being queued,
 * `deferred_after` is it still being queued after the scope and `ran` is it having been run, so
 * `ran` is the second context's destructor having drained the queue.
 *
 * `alive` is the program this pass was given, still allocated after a major collection ran with
 * the running context's own registration in place: a registration belongs to the owner that made
 * it, so building and destroying the second context neither displaced the running context's entry
 * nor removed it.
 *
 * The defect this pass was written for: a module keeps a strong pointer to its context and the
 * context owns its modules, so the second context had five owners (the scope's own, the executor
 * manager's, the built-in module's and the two executor factories that module registers) and read
 * `held=5`; it was never destroyed (`live_after` stayed up, the second context's entry stayed
 * registered so `root_given_back` read 0) and its teardown never drained the queue
 * (`deferred_after=1 ran=0`).
 */

#include "camel/passes/test/context_lifetime.h"

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/core/mm/deferred.h"

#include <atomic>
#include <ostream>
#include <string>
#include <string_view>

namespace {

/// The release the pass queues: whether it ran is whether anything drained the queue between the
/// point it was queued and the point the reading is taken.
std::atomic<bool> gProbeReleaseRan{false};

void probeRelease(void *) noexcept {
    gProbeReleaseRan.store(true, std::memory_order_release);
}

/// The prefix a `Context`'s graph roots are registered under: one entry per context.
constexpr std::string_view kContextRootSetPrefix = "runtimeGraphRoots";

/// How many context root sets the allocator holds right now.
size_t countContextRootSets() {
    size_t count = 0;
    for (const std::string &source : camel::core::mm::autoSpace().rootSourceDescriptions()) {
        if (source.starts_with(kContextRootSetPrefix)) {
            ++count;
        }
    }
    return count;
}

} // namespace

const camel::runtime::Program *
ContextLifetimeTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::core::context;

    const size_t liveBefore    = liveContextCount();
    const size_t rootsetBefore = countContextRootSets();

    size_t held          = 0;
    size_t liveSecond    = 0;
    size_t rootsetSecond = 0;
    bool pendingBefore   = false;
    {
        context_ptr_t other = Context::create(EntryConfig{}, camel::core::error::DiagsConfig{});
        // How many owners the second context has while this scope holds one of them.
        held          = other.use_count();
        liveSecond    = liveContextCount();
        rootsetSecond = countContextRootSets();
        // A release of the pass's own, queued where no collection starts before the scope ends.
        gProbeReleaseRan.store(false, std::memory_order_release);
        camel::core::mm::deferRelease(&probeRelease, nullptr);
        pendingBefore = camel::core::mm::deferredReleasePending();
    }

    const size_t liveAfter    = liveContextCount();
    const size_t rootsetAfter = countContextRootSets();
    const bool pendingAfter   = camel::core::mm::deferredReleasePending();
    const bool ran            = gProbeReleaseRan.load(std::memory_order_acquire);
    // Whatever the second context's teardown could not drain is drained here, so a queued release
    // nobody runs does not outlive the reading and change what a later collection sees.
    camel::core::mm::runDeferredReleases();

    // A major collection with the running context's own registration in place: what the running
    // program reaches through its own graphs has to survive it.
    camel::core::mm::autoSpace().majorGC();
    const bool alive = camel::core::mm::graphSpace().isAllocated(
        const_cast<camel::runtime::Program *>(program));

    os << "CONTEXTS-PROBE held=" << held << " live_before=" << liveBefore
       << " live_second=" << liveSecond << " live_after=" << liveAfter
       << " rootset_before=" << rootsetBefore << " rootset_second=" << rootsetSecond
       << " rootset_after=" << rootsetAfter
       << " root_given_back=" << (rootsetAfter != rootsetSecond ? 1 : 0)
       << " deferred_before=" << (pendingBefore ? 1 : 0)
       << " deferred_after=" << (pendingAfter ? 1 : 0) << " ran=" << (ran ? 1 : 0)
       << " alive=" << (alive ? 1 : 0) << "\n";
    return program;
}