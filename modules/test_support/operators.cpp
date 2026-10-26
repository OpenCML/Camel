// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include <format>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

#if defined(_WIN32)
// `windows.h` first: `tlhelp32.h` uses HANDLE, BOOL, DWORD and SIZE_T without declaring them, and
// no header this file includes before it reaches windows.h.
#include <windows.h>
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;

namespace {

// One meeting point for the whole process.
struct Rendezvous {
    std::mutex mutex;
    std::condition_variable met;
    int64_t waiting   = 0; // calls blocked right now
    uint64_t round    = 0; // bumped every time a group forms
    int64_t lastGroup = 0; // size of the group formed last
};

Rendezvous &rendezvous() {
    static Rendezvous instance;
    return instance;
}

} // namespace

std::unordered_map<std::string, operator_t> getTestSupportOpsMap() {
    return {
        {"rendezvous", __test_support_rendezvous__},
        {"thread_count", __test_support_thread_count__},
        {"with_probe", __test_support_with_probe__},
        {"with_probe_array", __test_support_with_probe_array__},
    };
}

// The calls that wait here leave together once as many of them have arrived as the caller asked
// for, and each of them is told how many met. The wait is bounded by the timeout, in seconds.
slot_t __test_support_rendezvous__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const Int64 parties         = norm.get<Int64>(0);
    const double timeoutSeconds = norm.get<double>(1);
    if (parties < 1 || timeoutSeconds < 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "test_support.rendezvous",
            std::format(
                "the number of parties is {} and the timeout is {} seconds; parties must be at "
                "least 1 and the timeout must not be negative",
                parties,
                timeoutSeconds));
    }

    Rendezvous &point = rendezvous();
    std::unique_lock lock(point.mutex);
    ++point.waiting;
    if (point.waiting >= parties) {
        // This call completes a group: everyone waiting leaves with it.
        const int64_t group = point.waiting;
        point.lastGroup     = group;
        point.waiting       = 0;
        ++point.round;
        point.met.notify_all();
        return toSlot(static_cast<Int64>(group));
    }

    const uint64_t round = point.round;
    const bool met       = point.met.wait_for(lock, std::chrono::duration<double>(timeoutSeconds), [&] {
        return point.round != round;
    });
    if (met) {
        return toSlot(static_cast<Int64>(point.lastGroup));
    }
    // Timed out: report who was waiting with this call, and stop waiting.
    const int64_t together = point.waiting;
    --point.waiting;
    return toSlot(static_cast<Int64>(together));
}

// The threads of this process, the caller included: the count `/proc/self/status` reports as
// `Threads:`, not the process's CPU count. A test that reads it pins how many threads a run
// creates, which is how a second worker pool is detected without timing anything.
slot_t __test_support_thread_count__(ArgsView &with, ArgsView &norm, Context &ctx) {
#if defined(_WIN32)
    const DWORD pid = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<thread_count> failed");
    }
    THREADENTRY32 entry;
    entry.dwSize = sizeof(entry);
    Int64 count  = 0;
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == pid) {
                ++count;
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return toSlot(count);
#elif defined(__APPLE__)
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<thread_count> failed");
    }
    for (mach_msg_type_number_t i = 0; i < count; ++i) {
        mach_port_deallocate(mach_task_self(), threads[i]);
    }
    vm_deallocate(
        mach_task_self(), reinterpret_cast<vm_address_t>(threads), count * sizeof(thread_act_t));
    return toSlot(static_cast<Int64>(count));
#else
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.compare(0, 8, "Threads:") == 0) {
            return toSlot(static_cast<Int64>(std::stoll(line.substr(8))));
        }
    }
    throwRuntimeFault(RuntimeDiag::RuntimeError, "<thread_count> failed");
#endif
}

// `base * 10000 + step * 100 + mode`: the three `with` values the call ended with.
slot_t __test_support_with_probe__(ArgsView &with, ArgsView &, Context &) {
    return toSlot(
        with.get<Int64>(0) * 10000 + with.get<Int64>(1) * 100 + with.get<Int64>(2));
}

// The same number, plus the length of the array: which of the two declarations answered.
slot_t __test_support_with_probe_array__(ArgsView &with, ArgsView &norm, Context &) {
    return toSlot(
        with.get<Int64>(0) * 10000 + with.get<Int64>(1) * 100 + with.get<Int64>(2) +
        static_cast<Int64>(norm.get<Array *>(0)->size()));
}
