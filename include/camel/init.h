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
 * Created: Mar. 11, 2026
 * Updated: Mar. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/utils/dll_path.h"

#include <mutex>

namespace camel {

/// 主机端初始化（SDK 工具如 camel、camel-cpp 等）：设置库搜索路径等，在使用 libcamel 前调用；幂等。
/// 适用于运行在 SDK 内的可执行文件，以 exe 所在目录为 base（exe/、exe/libs、exe/../libs）。
inline void initialize() {
    static std::once_flag once;
    std::call_once(once, []() { camel::utils::setupLibrarySearchPathForHost(); });
}

} // namespace camel
