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
 * Updated: Sep. 21, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "time.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"
#include "utils/strpt.h"

namespace GIR = GraphIR;

#include <chrono>

OperatorReturnCode __now__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    double seconds = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count() / 1000.0;

    // Add 8 hours offset (UTC+8)
    seconds += 8 * 3600;

    frame.set(self, std::make_shared<DoubleData>(seconds));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __strftime__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &norm = self->normInputs();
    if (norm.size() != 2) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<strftime>", "exactly 2 arguments", std::to_string(norm.size()));
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    const data_ptr_t &time_val = frame.get(norm[0]);
    const data_ptr_t &fmt_val = frame.get(norm[1]);

    if (time_val->type() != Type::Double()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<strftime> first argument must be a float timestamp");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    if (fmt_val->type() != Type::String()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<strftime> second argument must be a string format");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    double timestamp = time_val->as<DoubleData>(Type::Double())->data();

    // Subtract UTC+8 offset to convert to UTC
    timestamp -= 8 * 3600;

    std::time_t tt = static_cast<std::time_t>(timestamp);
    std::tm tm;
    localtime_s(&tm, &tt); // Still uses local time to represent UTC+8

    auto fmt = fmt_val->as<StringData>(Type::String())->data();

    char buffer[128] = {0};
    if (std::strftime(buffer, sizeof(buffer), fmt.c_str(), &tm) == 0) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<strftime> formatting failed (buffer too small or invalid format)");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    frame.set(self, std::make_shared<StringData>(std::string(buffer)));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __strptime__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &norm = self->normInputs();
    if (norm.size() != 2) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<strptime>", "exactly 2 arguments", std::to_string(norm.size()));
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    const data_ptr_t &str_val = frame.get(norm[0]);
    const data_ptr_t &fmt_val = frame.get(norm[1]);

    if (str_val->type() != Type::String() || fmt_val->type() != Type::String()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<strptime> requires two string arguments");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    auto time_str = str_val->as<StringData>(Type::String())->data();
    auto fmt_str = fmt_val->as<StringData>(Type::String())->data();

    std::tm tm = {};
    if (!myStrptime(time_str, fmt_str, tm)) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<strptime> failed to parse time string with format: " + fmt_str);
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    std::time_t time_epoch = std::mktime(&tm);
    if (time_epoch == -1) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<strptime> mktime conversion failed");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    // Add UTC+8 offset
    double seconds = static_cast<double>(time_epoch) + 8 * 3600;

    frame.set(self, std::make_shared<DoubleData>(seconds));
    return OperatorReturnCode::OK;
}
