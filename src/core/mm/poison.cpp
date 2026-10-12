/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/mm/poison.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef _WIN32
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

// The poison word and the fault handler of the safepoint-poisoning mode (see poison.h).

namespace camel::core::mm::poison {

namespace {

// The reservation the poison word points into. Virtual tables and object fields are read at
// small offsets from the word, in both directions (an object header precedes its payload), so
// the word sits in the middle of 2 MiB of inaccessible address space.
constexpr size_t kGuardBytes = size_t{2} << 20;

std::atomic<uintptr_t> guardBase{0};
std::atomic<slot_t> poisonWord{0};

#ifndef _WIN32
struct sigaction previousSegv;
struct sigaction previousBus;

void writeAll(const char *text) {
    const size_t length = std::strlen(text);
    size_t done         = 0;
    while (done < length) {
        const ssize_t n = ::write(STDERR_FILENO, text + done, length - done);
        if (n <= 0) {
            break;
        }
        done += static_cast<size_t>(n);
    }
}

void onFault(int signo, siginfo_t *info, void *context) {
    const uintptr_t base = guardBase.load(std::memory_order_relaxed);
    const auto addr      = reinterpret_cast<uintptr_t>(info ? info->si_addr : nullptr);
    if (base != 0 && addr >= base && addr < base + kGuardBytes) {
        // Only async-signal-safe calls from here on.
        writeAll(
            "[camel] GC poison: a pointer to a Camel object was used after a safepoint moved "
            "the object (a raw pointer was kept across a safepoint; see "
            "docs/technical/35_gc_handle_rules.md)\n");
        _exit(134);
    }
    // Not ours: hand the fault to whoever was installed before, or die by the default action.
    const struct sigaction &previous = signo == SIGSEGV ? previousSegv : previousBus;
    if ((previous.sa_flags & SA_SIGINFO) != 0 && previous.sa_sigaction != nullptr) {
        previous.sa_sigaction(signo, info, context);
        return;
    }
    if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN &&
        previous.sa_handler != nullptr) {
        previous.sa_handler(signo);
        return;
    }
    ::signal(signo, SIG_DFL);
    ::raise(signo);
}
#endif

} // namespace

slot_t install() {
    if (const slot_t word = poisonWord.load(std::memory_order_acquire); word != 0) {
        return word;
    }
#ifndef _WIN32
    void *region =
        ::mmap(nullptr, kGuardBytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (region != MAP_FAILED) {
        const auto base = reinterpret_cast<uintptr_t>(region);
        guardBase.store(base, std::memory_order_release);
        poisonWord.store(static_cast<slot_t>(base + kGuardBytes / 2), std::memory_order_release);

        // The handler runs on its own stack: a stale pointer can also surface as a runaway
        // recursion, and the report must still come out.
        static char altStack[64 * 1024];
        stack_t stack{};
        stack.ss_sp   = altStack;
        stack.ss_size = sizeof(altStack);
        ::sigaltstack(&stack, nullptr);

        struct sigaction action{};
        action.sa_sigaction = onFault;
        action.sa_flags     = SA_SIGINFO | SA_ONSTACK;
        ::sigemptyset(&action.sa_mask);
        ::sigaction(SIGSEGV, &action, &previousSegv);
        ::sigaction(SIGBUS, &action, &previousBus);
        return poisonWord.load(std::memory_order_acquire);
    }
#endif
    // No reservation (Windows, where this mode is not verified, or a host that refused the
    // mapping): a non-canonical address still faults when it is dereferenced, without the
    // report.
    poisonWord.store(static_cast<slot_t>(0xDEADF00DDEAD0000ULL), std::memory_order_release);
    return poisonWord.load(std::memory_order_acquire);
}

slot_t word() noexcept { return poisonWord.load(std::memory_order_acquire); }

bool isPoison(slot_t value) noexcept {
    const slot_t word = poisonWord.load(std::memory_order_acquire);
    if (word == 0) {
        return false;
    }
    if (value == word) {
        return true;
    }
    const uintptr_t base = guardBase.load(std::memory_order_relaxed);
    return base != 0 && value >= base && value < base + kGuardBytes;
}

} // namespace camel::core::mm::poison
