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
 * Created: Aug. 20, 2025
 * Updated: Aug. 20, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#ifndef NDEBUG

#include <string>

void trace_event_begin(const std::string &name, const std::string &cat = "default");
void trace_event_end(const std::string &name = "");
void trace_event_instant(
    const std::string &name, const std::string &cat = "default", const std::string &scope = "t");
void trace_event_complete(
    const std::string &name, uint64_t start_ts, uint64_t dur, const std::string &cat = "default");
void trace_event_counter(const std::string &name, int value, const std::string &cat = "default");
void trace_event_metadata_thread_name(const std::string &name);
void trace_event_metadata_process_name(const std::string &name);
void trace_event_flush();

#define TRACE_EVENT_BEGIN(name) trace_event_begin(name)
#define TRACE_EVENT_END() trace_event_end()
#define TRACE_EVENT_INSTANT(name) trace_event_instant(name)
#define TRACE_EVENT_COMPLETE(name, start_ts, dur) trace_event_complete(name, start_ts, dur)
#define TRACE_EVENT_COUNTER(name, value) trace_event_counter(name, value)
#define TRACE_THREAD_NAME(name) trace_event_metadata_thread_name(name)
#define TRACE_PROCESS_NAME(name) trace_event_metadata_process_name(name)
#define TRACE_FLUSH() trace_event_flush()

#else

#define TRACE_EVENT_BEGIN(name)
#define TRACE_EVENT_END()
#define TRACE_EVENT_INSTANT(name)
#define TRACE_EVENT_COMPLETE(name, start_ts, dur)
#define TRACE_EVENT_COUNTER(name, value)
#define TRACE_THREAD_NAME(name)
#define TRACE_PROCESS_NAME(name)
#define TRACE_FLUSH()

#endif