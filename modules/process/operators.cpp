// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operators.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
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
#include <Lmcons.h>
#include <windows.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

std::unordered_map<std::string, operator_t> getProcessOpsMap() {
    return {
        {"exit", __process_exit__},
        {"sleep", __process_sleep__},
        {"whoami", __process_whoami__},
        {"cpu_count", __process_cpu_count__},
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

// Ends the program with the given exit code: see camel/core/process_exit.h for how the run is
// unwound. The process leaves through main, not through std::exit, so that buffered output is
// written and what the run holds is released as after a normal return.
slot_t __process_exit__(ArgsView &with, ArgsView &norm, Context &ctx) {
    throw camel::core::ProcessExit{static_cast<int>(norm.get<Int64>(0))};
}
