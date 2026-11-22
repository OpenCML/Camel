// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

namespace debugger {

class CommandDispatcher;

void registerAllCommands(CommandDispatcher &dispatcher);

} // namespace debugger
