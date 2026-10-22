// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#define LIKELY(x) (__builtin_expect(!!(x), 1))
#define UNLIKELY(x) (__builtin_expect(!!(x), 0))

/// A function small enough that a caller must not pay a call for it: the state bindings of a loop
/// (camel/execute/loop_frame.h) run twice a round, and the compiler would otherwise leave a call
/// to the one copy it keeps for all callers, which is measurable there.
#if defined(__GNUC__) || defined(__clang__)
#define CAMEL_ALWAYS_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define CAMEL_ALWAYS_INLINE __forceinline
#else
#define CAMEL_ALWAYS_INLINE inline
#endif
