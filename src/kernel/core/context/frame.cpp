// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/context/frame.h"
#include "camel/core/error/runtime.h"

#include <format>

using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;

namespace camel::core::context {

/// The graph's name is read by the frame's two debug log lines, by the frame printer, and by the
/// virtual machine's fault, recursion-limit and stack-exhaustion paths -- none of them per
/// dispatch -- so it is defined here rather than in the class: `frame.h` keeps `runtime/graph.h`
/// for the layout accessors beside it, and this one costs a call on paths that already build a
/// message.
const std::string &Frame::graphName() const {
    GUARD(runtimeGraph_ != nullptr, "Runtime frame has no runtime graph name.");
    return runtimeGraph_->name();
}

void throwFramePoolExhausted(const camel::runtime::GCGraph *graph, size_t poolBytes) {
    camel::core::error::throwRuntimeFault(
        camel::core::error::RuntimeDiag::MaxRecursionDepthExceeded,
        graph ? std::string(graph->name()) : std::string("?"),
        std::format(
            "the frames of the open calls fill the {} KiB frame pool before the limit is reached",
            poolBytes / 1024));
}

} // namespace camel::core::context
