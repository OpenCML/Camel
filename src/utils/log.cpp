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
 * Created: Sep. 04, 2025
 * Updated: Feb. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/utils/log.h"

#ifndef NDEBUG
Logger::Level Logger::globalLogLevel_ = Logger::Level::Debug;
bool Logger::verboseEnabled_          = false;
std::ofstream Logger::logFile_;
std::mutex Logger::logMutex_;

void Logger::SetVerbose(bool enable) { verboseEnabled_ = enable; }
#endif

Logger l("");

#ifndef NDEBUG
const std::string filteredLoggerScope = "";
#endif
