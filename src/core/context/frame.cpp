// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
