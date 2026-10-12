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
 * Created: Sep. 16, 2025
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/context/frame.h"
#include "camel/core/error/runtime.h"

#include <format>

using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;

namespace camel::core::context {

void throwFramePoolExhausted(const camel::runtime::GCGraph *graph, size_t poolBytes) {
    camel::core::error::throwRuntimeFault(
        camel::core::error::RuntimeDiag::MaxRecursionDepthExceeded,
        graph ? std::string(graph->name()) : std::string("?"),
        std::format(
            "the frames of the open calls fill the {} KiB frame pool before the limit is reached",
            poolBytes / 1024));
}

} // namespace camel::core::context
