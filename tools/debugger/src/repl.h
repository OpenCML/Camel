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
 * Created: Feb. 22, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
