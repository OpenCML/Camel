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
 * Created: Nov. 22, 2025
 * Updated: Dec. 31, 2025
 * Supported by: National Key Research and Development Program of China
 */

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
