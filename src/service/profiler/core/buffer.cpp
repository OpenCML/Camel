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
 * Created: Sep. 27, 2025
 * Updated: Oct. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "buffer.h"

void TraceBuffer::push(const TraceEvent &event) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(event);
}

std::vector<TraceEvent> TraceBuffer::pop_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TraceEvent> result;
    while (!queue_.empty()) {
        result.push_back(queue_.front());
        queue_.pop();
    }
    return result;
}
