// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The allocator is one per process, while the object root set belongs to one owner each. This pass
 * reads what the allocator holds while a second context is created beside the running one, adds a
 * registration of its own and takes it back, and prints one line for the test plan:
 *
 *   CONTEXTS before=<n> second=<n> held=<n> released=<n> own=<0|1> own_released=<0|1> alive=<0|1>
 *
 * `before` is the running context's own registration (one `runtimeGraphRoots` entry, the name the
 * profiler and `std::gcsnap` already report), `second` the same plus the second context's, and
 * `released` the same after the pass's own registration was taken back: giving one registration
 * back removes that one and nobody else's. `own` / `own_released` read the pass's own entry by its
 * name. `alive` is the program this pass was given, still allocated in the graph space after a
 * major collection ran while both contexts were alive. `held` is how many owners the second context
 * has while this pass holds one of them: more than one means something else keeps it alive, so its
 * destructor -- the one that gives its registration back -- never runs. The context owns its
 * modules and a module keeps a strong pointer to its context, so `held` reads 2 and a second
 * context of a process is never destroyed. Reported, not fixed here: it is a lifetime defect of its
 * own (the same one makes `--timeit N` keep N contexts), and it is what a fix of the registration
 * path has to be tested through.
 *
 * Before the fix the allocator held one slot: the second context took the first's registration over
 * (`second` stayed 1) and the pass's own registration took the second's over (`released` read 0),
 * and a major collection marked almost nothing -- the string constants this run reaches through its
 * own graphs were swept, and the back end then printed `contexts-1c2contexts-1c3` for
 * `contexts-1-2-3`.
 */

#include "camel/passes/test/contexts.h"

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"

#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kContextRootSetPrefix = "runtimeGraphRoots";
constexpr std::string_view kOwnRootSetName       = "test::contexts.roots";

/// How many object root sets a given owner name prefix accounts for right now.
size_t countRootSets(std::string_view prefix) {
    size_t count = 0;
    for (const std::string &source : camel::core::mm::autoSpace().rootSourceDescriptions()) {
        if (source.starts_with(prefix)) {
            ++count;
        }
    }
    return count;
}

} // namespace

const camel::runtime::Program *
ContextsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::core::context;
    const size_t before = countRootSets(kContextRootSetPrefix);
    size_t second       = 0;
    size_t held         = 0;
    size_t own          = 0;
    size_t ownReleased  = 0;
    {
        context_ptr_t other = Context::create(EntryConfig{}, camel::core::error::DiagsConfig{});
        second              = countRootSets(kContextRootSetPrefix);
        // How many owners the second context has while this scope holds one of them. One means
        // this scope is its only owner, so it is destroyed when the scope ends and gives its own
        // registration back; more than one means something else keeps it alive, and then the
        // destructor -- the one that gives the registration back -- never runs.
        held = other.use_count();
        // A full collection while the second context is alive: it marks through every registered
        // root set, so this run's graphs and the static data they reach stay alive and current.
        camel::core::mm::autoSpace().majorGC();
        // A registration of the pass's own, then taken back: the two contexts' entries are what
        // must survive it.
        std::vector<camel::core::rtdata::Object *> ownRoots;
        camel::core::mm::autoSpace().registerObjectRootSet(
            &ownRoots, std::string(kOwnRootSetName), &ownRoots);
        own = countRootSets(kOwnRootSetName);
        camel::core::mm::autoSpace().unregisterObjectRootSet(&ownRoots);
        ownReleased = countRootSets(kOwnRootSetName);
    }
    const size_t released = countRootSets(kContextRootSetPrefix);
    const bool alive      = camel::core::mm::graphSpace().isAllocated(
        const_cast<camel::runtime::Program *>(program));
    os << "CONTEXTS before=" << before << " second=" << second << " held=" << held
       << " released=" << released << " own=" << own << " own_released=" << ownReleased
       << " alive=" << (alive ? 1 : 0) << "\n";
    return program;
}