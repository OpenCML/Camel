// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
#include <span>
#include <string_view>

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

/// A fusion group the back end is about to run at the node it is reporting (camel/execute/
/// fusion_group.h). The group runs at its tail, which is the node being reported and the last of
/// `members`; the members before it were deferred and do not execute, so they are never reported
/// on their own: a debugger must not stop at a node that does not run (35_debugger.md, 2).
struct FusionGroupSite {
    std::string_view name;
    std::span<const runtime::gc_node_ref_t> members;
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

    /// std::nvm is about to run `node` of `graph` on `frame`. `group` is the fusion group it runs
    /// here, when `node` is that group's tail; the members before the tail do not run and are not
    /// reported. A back end reports a group at its tail and only there (fusion_group.h).
    virtual void beforeNode(
        runtime::GCGraph *graph, core::context::Frame *frame, runtime::gc_node_ref_t node,
        const FusionGroupSite *group = nullptr) = 0;
    /// std::nvm is about to replace the frame of the running activation by a tail call: the
    /// next node reported belongs to the callee, which took the caller's place.
    virtual void tailCall() = 0;

    /// std::fvm is about to run the instruction at `site.pc` on `site.frame`.
    virtual void beforeInstruction(const InstructionSite &site) = 0;

    /// The node or instruction reported last raised `fault`. Called before any frame is
    /// released. `isolated`: the fault is inside a fault-isolation boundary and will become
    /// an error value there; otherwise it ends the program.
    virtual void faultRaised(const core::error::RuntimeFault &fault, bool isolated) = 0;

    /// The pass pipeline is about to run a pass. `schedules` is true for a scheduling pass,
    /// whose apply runs the program; a rewrite or translation pass may evaluate graphs for
    /// its work (the rules library, a macro function), and that is not the program's run.
    /// `root` is the program root the pass is given, or null.
    virtual void passStarting(bool /*schedules*/, runtime::GCGraph * /*root*/) {}
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
