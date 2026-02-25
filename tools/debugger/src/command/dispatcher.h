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
