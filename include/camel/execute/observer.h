/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Oct. 12, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * What a sequential scheduler tells a debugger while it runs a program
 * (docs/technical/35_debugger.md, 7.1).
 *
 * One observer per process, installed by the debugger (camel-db) and null otherwise. The
 * schedulers that call it are std::nvm and std::fvm; compiled code (std::jit) and the parallel
 * scheduler do not. A hook may block (that is how the program is stopped) and may throw to
 * end the run; it must not run Camel code.
 *
 * Cost without an observer: std::nvm loads the pointer once per node and once per call.
 * std::fvm loads it once per nested activation and runs the loop it always ran; while an
 * observer is installed its activations run on a second instance of the loop that calls
 * beforeInstruction (src/passes/sched/fastvm/inspectvm.cpp), so the normal loop has no hook.
 */

#pragma once

#include "camel/core/source/ids.h"
#include "camel/runtime/graph.h"

#include <cstddef>
#include <cstdint>

namespace camel::core::context {
class Frame;
}
namespace camel::core::error {
class RuntimeFault;
}

namespace camel::execute {

/// What the bytecode instruction about to run does to the interpreter's own call stack.
enum class InstructionEffect : uint8_t {
    None,
    Call,     // pushes the frame and continues in the callee's (FUNC)
    Return,   // returns to the frame below, or out of the activation (RETN)
    TailCall, // replaces the frame by the callee's (TAIL)
    Jump,     // only moves the pc: not a node of the graph (JUMP)
};

struct InstructionSite {
    size_t pc                         = 0;
    core::context::Frame *frame       = nullptr;
    camel::source::origin_id_t origin = camel::source::kInvalidOriginId;
    /// The frame slot the instruction writes its result to; 0 when it writes none.
    int32_t resultSlot       = 0;
    InstructionEffect effect = InstructionEffect::None;
};

class ExecutionObserver {
  public:
    virtual ~ExecutionObserver() = default;

    /// One activation of a scheduler's interpreter loop starts: the graph of a call that
    /// nests on the native stack (every call on std::nvm; on std::fvm the entry, a call of a
    /// function value and each element call of a higher-order operator). The first node or
    /// instruction reported after it belongs to the new frame.
    virtual void activationEntered() = 0;
    /// The activation ends, by return or by an exception passing through it.
    virtual void activationLeft() = 0;

    /// std::nvm is about to run `node` of `graph` on `frame`.
    virtual void beforeNode(
        runtime::GCGraph *graph, core::context::Frame *frame, runtime::gc_node_ref_t node) = 0;
    /// std::nvm is about to replace the frame of the running activation by a tail call: the
    /// next node reported belongs to the callee, which took the caller's place.
    virtual void tailCall() = 0;

    /// std::fvm is about to run the instruction at `site.pc` on `site.frame`.
    virtual void beforeInstruction(const InstructionSite &site) = 0;

    /// The node or instruction reported last raised `fault`. Called before any frame is
    /// released. `isolated`: the fault is inside a fault-isolation boundary and will become
    /// an error value there; otherwise it ends the program.
    virtual void faultRaised(const core::error::RuntimeFault &fault, bool isolated) = 0;
};

/// The installed observer, or null.
ExecutionObserver *executionObserver() noexcept;
/// Installs `observer` (null: none). The caller keeps it alive while it is installed.
void setExecutionObserver(ExecutionObserver *observer) noexcept;

/// Reports one activation for its lifetime, when an observer is installed as it starts.
class ActivationScope {
  public:
    ActivationScope() noexcept : observer_(executionObserver()) {
        if (observer_ != nullptr) [[unlikely]] {
            observer_->activationEntered();
        }
    }
    ~ActivationScope() {
        if (observer_ != nullptr) [[unlikely]] {
            observer_->activationLeft();
        }
    }
    ActivationScope(const ActivationScope &)            = delete;
    ActivationScope &operator=(const ActivationScope &) = delete;

  private:
    ExecutionObserver *observer_;
};

} // namespace camel::execute
