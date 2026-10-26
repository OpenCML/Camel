// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operators.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm/blocking.h"
#include "camel/core/operator.h"
#include "camel/core/process_exit.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/string.h"
#include <format>

#include <memory>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;
#include <chrono>
#include <thread>

#ifdef _WIN32
// `windows.h` first: `Lmcons.h` and `tlhelp32.h` use HANDLE, BOOL, DWORD, SIZE_T and ULONG_PTR
// without declaring them, and no header this file includes before them reaches windows.h.
#include <windows.h>
#include <Lmcons.h>
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

#include <fstream>
#include <string>

std::unordered_map<std::string, operator_t> getProcessOpsMap() {
    return {
        {"exit", __process_exit__},
        {"sleep", __process_sleep__},
        {"whoami", __process_whoami__},
        {"cpu_count", __process_cpu_count__},
        {"thread_count", __process_thread_count__},
    };
}

slot_t __process_sleep__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 ms = norm.get<Int64>(0);
    if (ms < 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "process.sleep",
            std::format("the duration is {} ms; it must not be negative", ms));
    }
    try {
        // A sleep is a wait, and a wait is announced: the scheduler takes this thread out of its
        // GC region for it and puts it back after, so that a sleeping worker is not what makes a
        // cooperative stop-the-world collection give up (C24, MG/R5/BE/6). Without a scheduler
        // that registered itself (every other back end, and every serial run) this costs one
        // relaxed load.
        camel::core::mm::BlockingWaitScope wait;
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    } catch (const std::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("<sleep> ") + e.what());
    }
    return NullSlot;
}

slot_t __process_whoami__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::string username;
#ifdef _WIN32
    char buffer[UNLEN + 1];
    DWORD len = UNLEN + 1;
    if (GetUserNameA(buffer, &len)) {
        username = buffer;
    } else {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<whoami> failed");
    }
#else
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        username = pw->pw_name;
    } else {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<whoami> failed");
    }
#endif
    return toSlot(checkedString(username, mm::autoSpace(), "process.whoami"));
}

slot_t __process_cpu_count__(ArgsView &with, ArgsView &norm, Context &ctx) {
    unsigned int count = std::thread::hardware_concurrency();
    if (count == 0) {
        count = 1;
    }
    return toSlot(static_cast<Int64>(count));
}

// The threads of this process, the caller included: the count `/proc/self/status` reports as
// `Threads:`, not the process's CPU count. A test that reads it pins how many threads a run
// creates, which is how a second worker pool is detected without timing anything.
slot_t __process_thread_count__(ArgsView &with, ArgsView &norm, Context &ctx) {
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

// Ends the program with the given exit code: see camel/core/process_exit.h for how the run is
// unwound. The process leaves through main, not through std::exit, so that buffered output is
// written and what the run holds is released as after a normal return.
slot_t __process_exit__(ArgsView &with, ArgsView &norm, Context &ctx) {
    throw camel::core::ProcessExit{static_cast<int>(norm.get<Int64>(0))};
}
