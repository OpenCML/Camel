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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/string.h"
#include "camel/utils/strpt.h"

#include <chrono>
#include <cstring>
#include <ctime>

#if defined(_WIN32)
#include <windows.h>
#endif

std::unordered_map<std::string, operator_t> getTimeOpsMap() {
    return {
        {"now", __time_now__},
        {"strftime", __time_strftime__},
        {"strptime", __time_strptime__},
    };
}

slot_t __time_now__(ArgsView &with, ArgsView &norm, Context &ctx) {
    using namespace std::chrono;
    auto now       = system_clock::now();
    double seconds = duration_cast<duration<double>>(now.time_since_epoch()).count();
    return toSlot(seconds);
}

slot_t __time_strftime__(ArgsView &with, ArgsView &norm, Context &ctx) {
    double timestamp = norm.get<Float64>(0);
    String *fmt_obj  = norm.get<String *>(1);
    bool as_local    = false;
    if (norm.size() > 2)
        as_local = norm.get<bool>(2);

    std::time_t tt = static_cast<std::time_t>(timestamp);
    std::tm tm{};
    if (as_local) {
        std::time_t local_tt = tt + 8 * 3600;
#if defined(_WIN32)
        if (gmtime_s(&tm, &local_tt) != 0)
#else
        if (gmtime_r(&local_tt, &tm) == nullptr)
#endif
        {
            throwRuntimeFault(RuntimeDiag::RuntimeError, "<strftime> failed");
        }
    } else {
#if defined(_WIN32)
        if (gmtime_s(&tm, &tt) != 0)
#else
        if (gmtime_r(&tt, &tm) == nullptr)
#endif
        {
            throwRuntimeFault(RuntimeDiag::RuntimeError, "<strftime> failed");
        }
    }
    const std::string &fmt = fmt_obj->toString();
    char buffer[128]       = {0};
    if (std::strftime(buffer, sizeof(buffer), fmt.c_str(), &tm) == 0) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<strftime> formatting failed");
    }
    return toSlot(String::from(buffer, mm::autoSpace()));
}

slot_t __time_strptime__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *str_obj = norm.get<String *>(0);
    String *fmt_obj = norm.get<String *>(1);
    std::tm tm{};
    if (!myStrptime(str_obj->toString(), fmt_obj->toString(), tm)) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<strptime> failed to parse");
    }
    std::time_t local_tt = std::mktime(&tm);
    if (local_tt == static_cast<std::time_t>(-1)) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<strptime> mktime failed");
    }
    double utc_seconds = static_cast<double>(local_tt) - 8 * 3600.0;
    return toSlot(utc_seconds);
}
