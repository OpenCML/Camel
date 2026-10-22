// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "event.h"

nlohmann::json TraceEvent::to_json() const {
    nlohmann::json j;
    j["name"] = name;
    j["cat"] = cat;
    j["ph"] = ph;
    j["ts"] = ts;
    j["pid"] = pid;
    j["tid"] = tid;

    if (!args.empty()) {
        j["args"] = args;
    }

    if (ph == "i" && !scope.empty()) {
        j["s"] = scope;
    }

    if (ph == "X") {
        j["dur"] = dur;
    }

    return j;
}
