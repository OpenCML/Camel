// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/context/frame.h"

#include "camel/core/error/runtime.h"
#include "camel/runtime/graph.h"

#include <format>

using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;

namespace camel::core::context {

/// The graph's name is read by the frame's two debug log lines, by the frame printer, and by the
/// virtual machine's fault, recursion-limit and stack-exhaustion paths -- none of them per
/// dispatch -- so it is defined here rather than in the class. It reads the frame's record
/// (camel/runtime/frame_layout.h): no caller of this file's header needs the graph's definition.
const std::string &Frame::graphName() const {
    GUARD(layout_ != nullptr, "Runtime frame has no runtime graph name.");
    return layout_->name;
}

void FramePool::markFrameGraph(Frame *frame, const rtdata::Object::RefRelocator &relocate) {
    camel::runtime::GCGraph *graph = frame->layout_ != nullptr ? frame->layout_->graph : nullptr;
    if (graph == nullptr) {
        return;
    }
    // The frame's record is what the collector is told to look at, but the value it is told about
    // is the graph itself: what comes back is the graph at its new address, and the record's own
    // copy of that address is `GCGraph::onMoved`'s to rewrite, not this call's.
    relocate(
        static_cast<rtdata::Object *>(graph),
        nullptr,
        rtdata::RefTraceInfo{
            .ownerKind = "Frame",
            .slotName  = "graph",
        });
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
