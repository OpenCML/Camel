/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Jul. 29, 2025
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

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

std::unordered_map<std::string, operator_t> getOSOpsMap() {
    return {
        {"exit", __os_exit__},
        {"sleep", __os_sleep__},
        {"whoami", __os_whoami__},
        {"cpu_count", __os_cpu_count__},
    };
}

slot_t __os_sleep__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 ms = norm.get<Int64>(0);
    if (ms < 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "os.sleep",
            std::format("the duration is {} ms; it must not be negative", ms));
    }
    try {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    } catch (const std::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("<sleep> ") + e.what());
    }
    return NullSlot;
}

slot_t __os_whoami__(ArgsView &with, ArgsView &norm, Context &ctx) {
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
    return toSlot(checkedString(username, mm::autoSpace(), "os.whoami"));
}

slot_t __os_cpu_count__(ArgsView &with, ArgsView &norm, Context &ctx) {
    unsigned int count = std::thread::hardware_concurrency();
    if (count == 0) {
        count = 1;
    }
    return toSlot(static_cast<Int64>(count));
}

// Ends the program with the given exit code: see camel/core/process_exit.h for how the run is
// unwound. The process leaves through main, not through std::exit, so that buffered output is
// written and what the run holds is released as after a normal return.
slot_t __os_exit__(ArgsView &with, ArgsView &norm, Context &ctx) {
    throw camel::core::ProcessExit{static_cast<int>(norm.get<Int64>(0))};
}
