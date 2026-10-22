// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "command.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace debugger {

class CommandDispatcher {
  public:
    void registerCommand(std::shared_ptr<Command> cmd);

    CommandResult dispatch(const std::string &name, const std::string &argsJson = "{}");

    using PostHook = std::function<void(
        const std::string &name, const std::string &argsJson, const CommandResult &result)>;
    void setPostExecuteHook(PostHook hook);

    const std::unordered_map<std::string, std::shared_ptr<Command>> &commands() const {
        return commands_;
    }

  private:
    std::unordered_map<std::string, std::shared_ptr<Command>> commands_;
    PostHook postHook_;
};

CommandDispatcher &getDispatcher();

} // namespace debugger
