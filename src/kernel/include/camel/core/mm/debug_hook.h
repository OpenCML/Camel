// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <string_view>

namespace camel::core::mm {

/// Pre-allocation event (not allocated yet, so no ptr).
struct PreAllocEvent {
    size_t size;       // Size about to be allocated (including header).
    const char *space; // Space name.
};

/// Post-allocation event information for debugger clients.
struct AllocEvent {
    void *ptr;
    size_t size;       // Total size including header.
    const char *space; // Space name: auto.birth, auto.haven, auto.elder, auto.large, meta, perm
};

#ifndef NDEBUG
/// A debugger inspection boundary outside the outer allocator's locks. Declare before
/// lock/operation guards. Nested allocators contribute their completed allocation event;
/// only the outer scope notifies, after every nested allocator has released its guards.
class AllocationDebugScope {
  public:
    AllocationDebugScope();
    ~AllocationDebugScope() noexcept(false);
    AllocationDebugScope(const AllocationDebugScope &)            = delete;
    AllocationDebugScope &operator=(const AllocationDebugScope &) = delete;
};
#endif

/// Invoke breakpoint before allocation (Debug build only); may block until continue.
void invokePreAllocHook(const PreAllocEvent &evt);

/// Set callback invoked after each allocation (Debug build only)
void setPostAllocDebugHook(void (*fn)(const AllocEvent *evt));

/// Clear the post-alloc hook
void clearPostAllocDebugHook();

/// Invoke the hook if set (called from allocator, Debug build only)
void invokePostAllocHook(const AllocEvent &evt);

#ifndef NDEBUG
/// Debug-only observer of safepoints, installed by the runtime (a draft session installs the
/// one that checks the draft-root invariant). It runs at the start of every safepoint, before
/// any decision to collect, so an invariant it reports is reported at the first safepoint the
/// unsafe path reaches rather than at the first collection. It must not allocate or collect;
/// a violation is reported by the observer itself.
using SafepointObserver = void (*)(std::string_view reason);

/// Install the observer. There is one; a null function pointer removes it. The observer is
/// not in a Release build at all, so nothing calls this there.
void setSafepointObserver(SafepointObserver observer);

/// Call the observer if one is installed (Debug build only; the fast path of `safepoint` in
/// gen.h calls it when it does not enter the allocator).
void observeSafepoint(std::string_view reason);
#endif

} // namespace camel::core::mm
