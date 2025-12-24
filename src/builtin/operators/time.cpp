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
 * Updated: Dec. 24, 2025
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

// -----------------------------------------------------------------------------
// 获取当前 UTC 时间戳（以秒为单位，double）
// -----------------------------------------------------------------------------
void __now__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    using namespace std::chrono;
    auto now       = system_clock::now();
    auto epoch     = now.time_since_epoch();
    double seconds = duration_cast<duration<double>>(epoch).count(); // 秒为单位（double）

    frame.set(self, seconds);
}

// -----------------------------------------------------------------------------
// strftime：格式化一个 UTC 时间戳为指定格式的字符串
//   参数：timestamp (double, UTC 秒)，fmt (string)，可选 bool 参数 local
//   - 如果 local==true，则显示为 UTC+8 时间
// -----------------------------------------------------------------------------
void __strftime__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    double timestamp = frame.get<Float64>(nargs[0]);
    String *fmt_obj  = frame.get<String *>(nargs[1]);

    bool as_local = false;
    if (nargs.size > 2) {
        as_local = frame.get<bool>(nargs[2]);
    }

    std::time_t tt = static_cast<std::time_t>(timestamp);

    std::tm tm{};
    if (as_local) {
        // UTC+8 时区转换（手动加8小时但使用gmtime）
        std::time_t local_tt = tt + 8 * 3600;
#if defined(_WIN32)
        if (gmtime_s(&tm, &local_tt) != 0)
#else
        if (gmtime_r(&local_tt, &tm) == nullptr)
#endif
        {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit("<strftime> failed to convert UTC+8 time");
            frame.set(self, NullSlot);
            return;
        }
    } else {
        // 纯UTC时间
#if defined(_WIN32)
        if (gmtime_s(&tm, &tt) != 0)
#else
        if (gmtime_r(&tt, &tm) == nullptr)
#endif
        {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit("<strftime> failed to convert UTC time");
            frame.set(self, NullSlot);
            return;
        }
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

// -----------------------------------------------------------------------------
// strptime：解析时间字符串（假设是“本地时间 UTC+8”）为 UTC 时间戳(double)
// -----------------------------------------------------------------------------
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

    // mktime() 认为 tm 是本地时间（例如系统为 UTC+8）
    // 它返回 UTC 时区下的 timestamp（单位: 秒）
    std::time_t local_tt = std::mktime(&tm);
    if (local_tt == static_cast<std::time_t>(-1)) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<strptime> mktime conversion failed");
        frame.set(self, NullSlot);
        return;
    }

    // 转换成本地时间的 timestamp（假设输入字符串是北京时间），减 8 小时得到 UTC
    double utc_seconds = static_cast<double>(local_tt) - 8 * 3600.0;

    frame.set(self, utc_seconds);
}
