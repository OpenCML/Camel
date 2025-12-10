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
 * Created: Jul. 29, 2025
 * Updated: Dec. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "time.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"
#include "utils/strpt.h"

namespace GIR = GraphIR;

#include <chrono>
#include <cstring> // for std::memset
#include <ctime>

#if defined(_WIN32)
#include <windows.h>
#endif

void __now__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    auto now       = std::chrono::system_clock::now();
    auto epoch     = now.time_since_epoch();
    double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count() / 1000.0;

    // Add 8 hours offset (UTC+8)
    seconds += 8 * 3600.0;

    frame.set(self, seconds);
}

void __strftime__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Double timestamp = frame.get<Double>(nargs[0]);
    String *fmt_obj  = frame.get<String *>(nargs[1]);

    // Subtract UTC+8 offset to convert to UTC
    timestamp -= 8 * 3600.0;

    std::time_t tt = static_cast<std::time_t>(timestamp);
    std::tm tm{};
#if defined(_WIN32)
    if (localtime_s(&tm, &tt) != 0)
#else
    if (localtime_r(&tt, &tm) == nullptr)
#endif
    {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<strftime> failed to convert time using localtime");
        frame.set(self, NullSlot);
        return;
    }

    const std::string &fmt = fmt_obj->toString();
    char buffer[128]       = {0};

    if (std::strftime(buffer, sizeof(buffer), fmt.c_str(), &tm) == 0) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<strftime> formatting failed (buffer too small or invalid format)");
        frame.set(self, NullSlot);
        return;
    }

    String *result = String::from(buffer, mm::autoSpace());
    frame.set(self, result);
}

void __strptime__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    String *str_obj = frame.get<String *>(nargs[0]);
    String *fmt_obj = frame.get<String *>(nargs[1]);

    const std::string &time_str = str_obj->toString();
    const std::string &fmt_str  = fmt_obj->toString();

    std::tm tm{};
    if (!myStrptime(time_str, fmt_str, tm)) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<strptime> failed to parse time string with format: " + fmt_str);
        frame.set(self, NullSlot);
        return;
    }

    std::time_t time_epoch = std::mktime(&tm);
    if (time_epoch == -1) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<strptime> mktime conversion failed");
        frame.set(self, NullSlot);
        return;
    }

    // Add UTC+8 offset
    double seconds = static_cast<double>(time_epoch) + 8 * 3600.0;
    frame.set(self, seconds);
}
