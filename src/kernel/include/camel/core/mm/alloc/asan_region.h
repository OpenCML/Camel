// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>

// AddressSanitizer regions for the spaces that own their memory (--spaces, see growable.h).
//
// ASAN tracks what `new` and `malloc` hand out: the redzones around one block, and a shadow that
// says whether each byte of it may be read. A space takes one big `new[]` per segment and divides
// it itself, so to ASAN every byte of every segment stays readable for the life of the process --
// including the bytes of a block the space has returned, and the bytes past the last block it
// handed out. A write into either is therefore invisible: what it breaks shows up later, as a
// corrupted free-list link or as a graph reading another graph's node, and the report names the
// place that noticed rather than the place that wrote.
//
// Putting those bytes back under the shadow with the runtime's own interface moves the report to
// the writing instruction and gives its stack in the space's own terms: `poison` is called when a
// block goes back to the space, `unpoison` when one is handed out.
//
// Zero cost without the sanitizer: when the build is not instrumented the two functions are empty
// inline definitions and nothing else is compiled in -- no symbol to link, no branch to take.

#if defined(__has_feature)
#    if __has_feature(address_sanitizer)
#        define CAMEL_MM_ASAN_REGIONS 1
#    endif
#endif
#if !defined(CAMEL_MM_ASAN_REGIONS) && defined(__SANITIZE_ADDRESS__)
#    define CAMEL_MM_ASAN_REGIONS 1
#endif

namespace camel::core::mm::asan_region {

#if defined(CAMEL_MM_ASAN_REGIONS) && CAMEL_MM_ASAN_REGIONS

// Declared here rather than included from <sanitizer/asan_interface.h>: the spaces must build the
// same whether or not the sanitizer's headers are installed, and the two symbols exist exactly
// when this file says they do.
extern "C" void __asan_poison_memory_region(void const volatile *addr, size_t size);
extern "C" void __asan_unpoison_memory_region(void const volatile *addr, size_t size);

/// Makes `bytes` at `addr` unreadable: touching them is reported at the touch.
inline void poison(void *addr, size_t bytes) noexcept {
    if (addr != nullptr && bytes != 0) {
        __asan_poison_memory_region(addr, bytes);
    }
}

/// Makes `bytes` at `addr` readable again, before the space writes its own bookkeeping there.
inline void unpoison(void *addr, size_t bytes) noexcept {
    if (addr != nullptr && bytes != 0) {
        __asan_unpoison_memory_region(addr, bytes);
    }
}

#else

inline void poison(void *, size_t) noexcept {}
inline void unpoison(void *, size_t) noexcept {}

#endif

} // namespace camel::core::mm::asan_region