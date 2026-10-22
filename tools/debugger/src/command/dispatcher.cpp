// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "dispatcher.h"

namespace debugger {

static CommandDispatcher g_dispatcher;

CommandDispatcher &getDispatcher() { return g_dispatcher; }

void CommandDispatcher::registerCommand(std::shared_ptr<Command> cmd) {
    commands_[cmd->name()] = std::move(cmd);
}

CommandResult CommandDispatcher::dispatch(const std::string &name, const std::string &argsJson) {
    auto it = commands_.find(name);
    if (it == commands_.end())
        return CommandResult::error(name, "Unknown command: " + name);

    CommandResult result = it->second->execute(argsJson);

    if (postHook_)
        postHook_(name, argsJson, result);

    return result;
}

void CommandDispatcher::setPostExecuteHook(PostHook hook) { postHook_ = std::move(hook); }

} // namespace debugger
