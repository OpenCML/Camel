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
 * Updated: Feb. 25, 2026
 * Supported by: National Key Research and Development Program of China
 */
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
