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
 * Created: Aug. 20, 2025
 * Updated: Aug. 20, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "nlohmann/json.hpp"
#include <string>
#include <unordered_map>

struct TraceEvent {
    std::string name;
    std::string cat;
    std::string ph;
    uint64_t ts;
    int pid;
    int tid;
    std::unordered_map<std::string, std::string> args;
    std::string scope; // for 'i'
    uint64_t dur = 0;  // for 'X'

    nlohmann::json to_json() const;
};