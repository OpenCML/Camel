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
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Running bytecode on behalf of another scheduler: see hosted.h.

#include "hosted.h"

using namespace camel::core::context;
using namespace camel::core::error;

namespace {

// The strategy of std::fvm (fastvm.cpp): a graph runs the same bytecode whichever scheduler
// reaches it.
const CompileStrategy kHostedCompileStrategy{
    .enableTailCallDetection = true,
    .enableInlineOperators   = true,
    .optimizationStrategies  = OptimizationStrategyCode::All,
};

} // namespace

FastVmSafepointHook &fastVmSafepointHook() noexcept {
    thread_local FastVmSafepointHook hook;
    return hook;
}

void fastVmSafepointSlowPath() {
    const FastVmSafepointHook &hook = fastVmSafepointHook();
    if (hook.poll != nullptr) {
        hook.poll(hook.host, "fastvm call boundary");
        return;
    }
    camel::core::mm::autoSpace().safepoint("fastvm call boundary");
}

void FastVMHostImage::build(
    const ctx::context_ptr_t &context, std::span<camel::runtime::GCGraph *const> graphs) {
    ASSERT(codes_.empty() && entries_.empty(), "A host image is built once.");
    if (graphs.empty()) {
        return;
    }
    auto linked = compileAndLinkGraphs(
        context,
        graphs,
        kHostedCompileStrategy,
        [](camel::runtime::GCGraph *) { return std::optional<size_t>{}; },
        0);
    codes_   = std::move(linked.codes);
    entries_ = std::move(linked.offsetMap);
}

slot_t FastVMSchedPass::callHosted(size_t pc, Frame *frame, size_t hostOpenCalls) {
    // The limit is on stackDepth_ + nestedCalls_ (push, requireNestedCallRoom). The calls open
    // in the host nest on the native stack below this activation, which is what nestedCalls_
    // counts; a hosted call entered from inside another one (an operator that called back into
    // the host) finds the first one's direct calls in stackDepth_ already.
    const size_t savedNested = nestedCalls_;
    const size_t savedDepth  = stackDepth_;
    nestedCalls_             = hostOpenCalls > stackDepth_ ? hostOpenCalls - stackDepth_ : 0;
    try {
        // callBorrowed, not call(): a host does not hand graphs over while an execution
        // observer is installed, so the inspected loop is never wanted here.
        const CallResult result = callBorrowed(pc, frame);
        nestedCalls_            = savedNested;
        if (result.rootFrame) {
            framePool_.release(result.rootFrame);
        }
        return result.result;
    } catch (const Diagnostic &) {
        // A fault: the loop released every frame of the activation and unwound its stack.
        nestedCalls_ = savedNested;
        throw;
    } catch (...) {
        // Anything else an operator throws (ProcessExit of `exit`, a C++ exception) passes the
        // loop untouched. The thread runs further hosted calls afterwards (the other elements
        // of a `map` run to their end), so the interpreter is put back as it was: the pool is a
        // stack, releasing the root frame releases everything above it.
        stackDepth_  = savedDepth;
        nestedCalls_ = savedNested;
        if (framePool_.isActive(frame)) {
            framePool_.release(frame);
        }
        throw;
    }
}
