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
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/tuple.h"
#include <format>

#include <memory>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;
using namespace camel::core::type;
#include <algorithm>
#include <chrono>
#include <thread>

#ifdef _WIN32
// `windows.h` first: `Lmcons.h` uses DWORD and friends without declaring them, and no header
// this file includes before it reaches windows.h.
#include <windows.h>
#include <Lmcons.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

#include <string>

std::unordered_map<std::string, operator_t> getProcessOpsMap() {
    return {
        {"exit", __process_exit__},
        {"sleep", __process_sleep__},
        {"user_name", __process_user_name__},
        {"cpu_count", __process_cpu_count__},
    };
}

slot_t __process_sleep__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const Float64 seconds = norm.get<Float64>(0);
    if (!(seconds >= 0.0)) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "process.sleep",
            std::format("the duration is {} s; it must be a number that is not negative", seconds));
    }
    // About 285 years is the longest wait a steady clock can hold; a longer one is that long.
    constexpr double kLongestSeconds = 9.0e9;
    try {
        std::this_thread::sleep_for(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::duration<double>(std::min(seconds, kLongestSeconds))));
    } catch (const std::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("<sleep> ") + e.what());
    }
    return NullSlot;
}

slot_t __process_user_name__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const TupleType *resultType = TupleType::fallible(Type::String());
    std::string username;
#ifdef _WIN32
    char buffer[UNLEN + 1];
    DWORD len = UNLEN + 1;
    if (!GetUserNameA(buffer, &len)) {
        return makeError("NotFound", "the system has no name for the user", "<process::user_name>");
    }
    username = buffer;
#else
    struct passwd *pw = getpwuid(getuid());
    if (!pw) {
        return makeError("NotFound", "the system has no name for the user", "<process::user_name>");
    }
    username = pw->pw_name;
#endif
    return makeOk(
        toSlot(checkedString(username, mm::autoSpace(), "process.user_name")),
        resultType);
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
