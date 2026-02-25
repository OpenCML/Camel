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
 * Created: Feb. 25, 2026
 * Updated: Feb. 26, 2026
 * Supported by: National Key Research and Development Program of China
 */
#pragma once

#include <string>

namespace debugger {

struct CommandResult {
    bool success = true;
    std::string command;
    std::string message;
    std::string body;

    static CommandResult
    ok(const std::string &cmd, const std::string &msg, const std::string &jsonBody = "") {
        return {true, cmd, msg, jsonBody};
    }
    static CommandResult error(const std::string &cmd, const std::string &msg) {
        return {false, cmd, msg, ""};
    }
};

class Command {
  public:
    virtual ~Command()                                         = default;
    virtual const char *name() const                           = 0;
    virtual const char *description() const                    = 0;
    virtual CommandResult execute(const std::string &argsJson) = 0;
};

} // namespace debugger
