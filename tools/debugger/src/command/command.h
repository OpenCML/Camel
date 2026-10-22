// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
