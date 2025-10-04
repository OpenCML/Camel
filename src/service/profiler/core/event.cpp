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
 * Created: Sep. 27, 2025
 * Updated: Oct. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

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
