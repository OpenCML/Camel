// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include <format>
#include "operators.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/strpt.h"

#include <chrono>
#include <cmath>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;
using namespace camel::core::type;
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

// The text of a moment is always in UTC: the same moment gives the same text on every machine.
slot_t __time_strftime__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const double timestamp = norm.get<Float64>(0);
    String *fmt_obj        = with.get<String *>(0);

    // 1e15 seconds is far beyond any calendar the C library keeps; the bound also keeps the
    // conversion to an integer defined.
    std::tm tm{};
    bool inRange = std::isfinite(timestamp) && std::fabs(timestamp) < 1.0e15;
    if (inRange) {
        const std::time_t tt = static_cast<std::time_t>(std::floor(timestamp));
#if defined(_WIN32)
        inRange = gmtime_s(&tm, &tt) == 0;
#else
        inRange = gmtime_r(&tt, &tm) != nullptr;
#endif
    }
    if (!inRange) {
        throwRuntimeFault(
            RuntimeDiag::TimeFormatFailed,
            "time.strftime",
            std::format("the timestamp {} is outside the range of calendar time", timestamp));
    }
    const std::string &fmt = fmt_obj->toString();
    char buffer[128]       = {0};
    if (std::strftime(buffer, sizeof(buffer), fmt.c_str(), &tm) == 0) {
        throwRuntimeFault(
            RuntimeDiag::TimeFormatFailed,
            "time.strftime",
            std::format(
                "the format '{}' gives no text, or more than {} characters",
                fmt,
                sizeof(buffer) - 1));
    }
    return toSlot(String::from(buffer, mm::autoSpace()));
}

// The fields of the text are taken as UTC, so the moment does not depend on the time zone of
// the machine. A text that does not have the format is an error value: it came from outside.
slot_t __time_strptime__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const TupleType *resultType = TupleType::fallible(Type::Float64());
    String *str_obj = norm.get<String *>(0);
    String *fmt_obj = with.get<String *>(0);
    std::tm tm{};
    if (!myStrptime(str_obj->toString(), fmt_obj->toString(), tm)) {
        return makeError(
            "ParseError",
            std::format(
                "the text '{}' does not have the format '{}'",
                str_obj->toString(),
                fmt_obj->toString()),
            "<time::strptime>");
    }
#if defined(_WIN32)
    const std::time_t utc = _mkgmtime(&tm);
#else
    const std::time_t utc = timegm(&tm);
#endif
    // -1 is also the last second of 1969; the fields say which it is.
    if (utc == static_cast<std::time_t>(-1) && tm.tm_year != 69) {
        return makeError(
            "ParseError",
            std::format(
                "the time read from '{}' cannot be represented as a timestamp",
                str_obj->toString()),
            "<time::strptime>");
    }
    return makeOk(toSlot(static_cast<double>(utc)), resultType);
}
