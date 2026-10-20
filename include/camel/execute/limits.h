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
 * Created: Oct. 12, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace camel::core::context {
class Context;
}

namespace camel::execute {

/// The limit on non-tail recursion (design principles 3.9: tail recursion is optimized, non-tail
/// recursion gets a configurable limit). It counts the calls that are open at once and are not
/// in tail position; a tail call replaces its caller and is not counted.
///
/// It is given by `--max-recursion-depth <n>` (EntryConfig::maxRecursionDepth) or, without the
/// option, by the environment variable CAMEL_MAX_RECURSION_DEPTH. Without either, every
/// scheduler stops at kDefaultMaxRecursionDepth: the default is 1000 on all of them, and the
/// parallel scheduler counts like the others (design principles 3.9, "默认值").
///
/// What is counted is the same on every back end: each call of a function (written directly,
/// made through a function value, or made by a higher-order operator for an element) that is
/// open and was not a tail call. The one call the program does not make itself, the entry
/// graph's call of `main` (or of the initializer of a module-level value), is not counted
/// (kEntryCalls). So with a limit of L, `depth(L - 1)` called from `main` is the deepest that
/// runs, for `func depth(n) = if n == 0 then 0 else 1 + depth(n - 1)`: L calls of `depth` are
/// open at the bottom.
inline constexpr size_t kEntryCalls                   = 1;
inline constexpr size_t kDefaultMaxRecursionDepth     = 1000;
inline constexpr const char *kMaxRecursionDepthEnvVar = "CAMEL_MAX_RECURSION_DEPTH";
inline constexpr const char *kMaxRecursionDepthOption = "--max-recursion-depth";

/// The limit on nested calls while std::macro evaluates a call ahead of time (its evaluator has
/// no tail calls: every call counts). `--max-macro-depth <n>`, else CAMEL_MAX_MACRO_DEPTH.
inline constexpr size_t kDefaultMaxMacroDepth     = 64;
inline constexpr const char *kMaxMacroDepthEnvVar = "CAMEL_MAX_MACRO_DEPTH";
inline constexpr const char *kMaxMacroDepthOption = "--max-macro-depth";

/// The limit on the rounds one loop may run while std::macro evaluates it ahead of time (design
/// principles 8; a loop that would not end makes the compilation fail instead of hang).
/// `--max-macro-rounds <n>`, else CAMEL_MAX_MACRO_ROUNDS. Rounds do not nest, so the count has no
/// relation to the stack and its range is wider than the depth limits'.
inline constexpr size_t kDefaultMaxMacroRounds     = 1'000'000;
inline constexpr size_t kMaxRoundsLimit            = size_t{1} << 40;
inline constexpr const char *kMaxMacroRoundsEnvVar = "CAMEL_MAX_MACRO_ROUNDS";
inline constexpr const char *kMaxMacroRoundsOption = "--max-macro-rounds";

/// The largest value either limit accepts. The interpreters nest on the native stack (NodeVM
/// for every call, FastVM for calls through function values and higher-order operators, the
/// macro evaluator and std::tf for every call), so a count alone cannot promise that the stack
/// holds. Where the bounds of the native stack can be read (kNativeStackGuard), every nested
/// call also checks the stack, and running out of it is the same diagnostic as reaching the
/// count; the count may then be large. Elsewhere the count is all there is, and it cannot be
/// raised above the default.
#if (defined(__linux__) && defined(__GLIBC__)) || defined(_WIN32) || defined(__APPLE__)
inline constexpr bool kNativeStackGuard = true;
#else
inline constexpr bool kNativeStackGuard = false;
#endif
inline constexpr size_t kMaxDepthLimit = kNativeStackGuard ? 1000000 : kDefaultMaxRecursionDepth;

/// Bytes of the frame pool of an interpreter that runs with `limit`: 4 KiB per call that may be
/// open (about 4 MiB for the default limit of 1000), up to 256 MiB. The pool is reserved once
/// and only the part in use is touched.
size_t framePoolBytes(size_t limit);

/// A depth limit written as text (an option value, an environment variable): a decimal integer
/// from 1 to kMaxDepthLimit. Anything else is no value.
std::optional<size_t> parseDepthLimit(std::string_view text);

struct RecursionLimit {
    size_t frames = kDefaultMaxRecursionDepth;
    /// Given by the option or the environment, not the default.
    bool given = false;
};

/// The limit in effect for a run in `context`: the option, else the environment, else the
/// default. A value of the environment that parseDepthLimit rejects is ignored here (the
/// command line reports it before anything runs).
RecursionLimit recursionLimit(const core::context::Context &context);
size_t macroDepthLimit(const core::context::Context &context);
/// A round limit written as text: a decimal integer from 1 to kMaxRoundsLimit.
std::optional<size_t> parseRoundLimit(std::string_view text);
/// The rounds one loop may run under std::macro in `context`: the option, else the environment,
/// else kDefaultMaxMacroRounds.
size_t macroRoundLimit(const core::context::Context &context);

/// The fault of a call that is not in tail position and would be open call number `limit + 1`.
/// `graphName` is the graph the call is written in. Every back end raises this one, so the
/// diagnostic reads the same on all of them; they report it at the call expression.
[[noreturn]] void throwRecursionLimitReached(std::string_view graphName, size_t limit);
/// The fault of a call refused because the native stack of the thread is nearly full, with
/// `openCalls` calls open, before the count reached the limit.
[[noreturn]] void throwNativeStackExhausted(std::string_view graphName, size_t openCalls);

/// What stands in place of the limit in that diagnostic (throwNativeStackExhausted), for a back
/// end that raises it itself.
std::string nativeStackExhaustedDetail(size_t openCalls);

/// False when the environment variable CAMEL_NATIVE_STACK_GUARD is 0: the checks of the native
/// stack are then skipped and only the counts stop a recursion. A switch for measuring where
/// the stack really ends (a raised limit then ends in a crash); not for normal use.
bool nativeStackGuardEnabled();
/// The bounds of the calling thread's native stack (it grows down from `high`); both 0 where
/// they cannot be read. Read once per thread.
void nativeStackBounds(uintptr_t &low, uintptr_t &high);
/// True when the calling thread has less native stack left than one more nested call of an
/// interpreter may need (the reserve covers the call and the unwinding of the diagnostic). Always
/// false where the stack bounds cannot be read (!kNativeStackGuard).
bool nativeStackNearlyExhausted();
/// The lowest stack address at which the calling thread still passes that check: the low bound
/// plus the reserve. Code that cannot call nativeStackNearlyExhausted (native code compiled by
/// std::jit) compares its stack pointer with this. 0 where the bounds cannot be read or the
/// guard is switched off: no stack pointer is below it.
uintptr_t nativeStackFloor();
/// Native stack the calling thread has used so far, in bytes (0 where it cannot be read).
size_t nativeStackUsed();
/// Size of the calling thread's native stack in bytes (0 where it cannot be read).
size_t nativeStackSize();

} // namespace camel::execute
