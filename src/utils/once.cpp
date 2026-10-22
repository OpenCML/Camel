/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/utils/once.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace camel::utils {

void runOnce(std::string_view key, const std::function<void()> &action) {
    // The map only ever grows: a process has a handful of these keys (one per resource several
    // modules share), and a flag must outlive every caller.
    static std::mutex mutex;
    static std::unordered_map<std::string, std::unique_ptr<std::once_flag>> flags;
    std::once_flag *flag = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::unique_ptr<std::once_flag> &entry = flags[std::string(key)];
        if (!entry) {
            entry = std::make_unique<std::once_flag>();
        }
        flag = entry.get();
    }
    std::call_once(*flag, action);
}

} // namespace camel::utils