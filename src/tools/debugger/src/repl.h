// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <ostream>
#include <string>

namespace debugger {

void printHelp();
void printVersion();
void printStartupBanner();
void repl();

/// The source-level commands of the REPL (repl_debug.cpp): returns false for a command that is
/// not one of them.
bool handleDebugReplCommand(const std::string &cmd, const std::string &arg);
void printDebugReplHelp(std::ostream &os);

} // namespace debugger
