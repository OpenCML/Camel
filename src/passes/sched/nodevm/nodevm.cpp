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
 * Created: Sep. 08, 2025
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "nodevm.h"
#include "camel/core/global_config.h"
#include "camel/core/module/module.h"
#include "camel/core/operator.h"
#include "camel/execute/executor.h"
#include "camel/execute/fault_isolation.h"
#include "camel/execute/function_call.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/execute/loop_driver.h"
#include "camel/execute/loop_frame.h"
#include "camel/execute/observer.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/loop.h"
#include "camel/utils/log.h"

#include "camel/core/context/frame.h"
#include "camel/core/effect_trace.h"
#include "camel/core/error/runtime.h"

#include <algorithm>
#include <span>

using namespace std;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using namespace camel::core::error;

namespace {


using camel::runtime::gc_data_idx_t;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCAccsBody;
using camel::runtime::GCBrchBody;
using camel::runtime::GCGraph;
using camel::runtime::GCNode;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::kInvalidNodeRef;

void bindMarkedFunctionFrame(
    Frame *frame, Function *func, std::span<const slot_t> normArgs,
    std::span<const slot_t> withArgs = {}) {
    ASSERT(frame != nullptr, "Marked operator call frame is null.");
    ASSERT(func != nullptr && func->graph() != nullptr, "Marked operator callee is null.");
    auto *graph             = func->graph();
    const auto normPorts    = graph->normPorts();
    const auto withPorts    = graph->withPorts();
    const auto closureNodes = graph->closureNodes();
    ASSERT(normArgs.size() == normPorts.size(), "Marked operator norm-arity mismatch.");
    ASSERT(withArgs.size() == withPorts.size(), "Marked operator with-arity mismatch.");

    Tuple *closure = func->tuple();
    ASSERT(closureNodes.empty() || closure != nullptr, "Marked operator closure tuple is null.");
    ASSERT(
        closure == nullptr || closure->size() == closureNodes.size(),
        "Marked operator closure-arity mismatch.");

    for (size_t i = 0; i < normPorts.size(); ++i) {
        const auto *port = graph->node(normPorts[i]);
        ASSERT(port != nullptr, "Marked operator norm port is missing.");
        frame->set(port->dataIndex, normArgs[i]);
    }
    for (size_t i = 0; i < withPorts.size(); ++i) {
        const auto *port = graph->node(withPorts[i]);
        ASSERT(port != nullptr, "Marked operator with port is missing.");
        frame->set(port->dataIndex, withArgs[i]);
    }
    if (closure != nullptr) {
        for (size_t i = 0; i < closureNodes.size(); ++i) {
            const auto *port = graph->node(closureNodes[i]);
            ASSERT(port != nullptr, "Marked operator closure node is missing.");
            frame->set(port->dataIndex, closure->get<slot_t>(i));
        }
    }
}

inline gc_data_idx_t planDataIndex(const camel::runtime::ExecPlan &plan, gc_node_ref_t nodeRef) {
    ASSERT(
        nodeRef < plan.dataIndexByRef.size(),
        "NodeVM cached data-index lookup is out of range.");
    return plan.dataIndexByRef[nodeRef];
}

inline gc_data_idx_t dataIndexOf(const GCGraph *graph, gc_node_ref_t nodeRef) {
    const auto *node = graph ? graph->node(nodeRef) : nullptr;
    ASSERT(node != nullptr, "NodeVM runtime node lookup resolved to null.");
    return node->dataIndex;
}

void bindDirectCallFrameSlots(
    Frame *sourceFrame, Frame *targetFrame, const camel::runtime::ExecPlan *layout,
    std::span<const runtime_data_idx_t> argSlots, std::vector<slot_t> &scratch) {
    ASSERT(sourceFrame != nullptr, "NodeVM direct-call source frame is null.");
    ASSERT(targetFrame != nullptr, "NodeVM direct-call target frame is null.");
    ASSERT(layout != nullptr, "NodeVM direct-call execution plan is null.");
    ASSERT(
        argSlots.size() == layout->calleePortSlots.size(),
        "NodeVM direct-call cache arity mismatch.");

    if (sourceFrame == targetFrame) {
        scratch.resize(argSlots.size());
        for (size_t argIndex = 0; argIndex < argSlots.size(); ++argIndex) {
            scratch[argIndex] = sourceFrame->get<slot_t>(argSlots[argIndex]);
        }
        for (size_t argIndex = 0; argIndex < argSlots.size(); ++argIndex) {
            targetFrame->set(layout->calleePortSlots[argIndex], scratch[argIndex]);
        }
        return;
    }

    for (size_t argIndex = 0; argIndex < argSlots.size(); ++argIndex) {
        targetFrame->set(
            layout->calleePortSlots[argIndex],
            sourceFrame->get<slot_t>(argSlots[argIndex]));
    }
}

} // namespace

NodeVMSchedPass::~NodeVMSchedPass() = default;

// =============================================================================
// Tail-call optimization: frame lifetime
// =============================================================================
//
// FramePool is a stack allocator, so frames must be released in LIFO order and
// every live frame must stay below the pool top (the GC only scans frames below
// it). A call keeps its caller-owned rootFrame plus at most two tail-call frames
// above it, tailLow and tailHigh (tailHigh on top). currFrame is always one of
// rootFrame, tailLow, tailHigh.
//
// A tail call to a different graph T (see tailCallFrame):
//   - T already owns tailLow or tailHigh (mutual recursion, e.g. branch arms):
//     bind arguments into that frame and switch to it; nothing is released.
//   - A free position exists directly above currFrame: acquire T there.
//   - Otherwise stage the arguments in scratch, release the tail frames that
//     are not needed (top first), and acquire T in the freed position.
// Self recursion rebinds the current frame through the same staging path.
//
// On exit (normal or exceptional) tailHigh, tailLow, and rootFrame are released
// in that order.
// =============================================================================

Frame *NodeVMSchedPass::tailCallFrame(
    camel::runtime::GCGraph *target, Frame *rootFrame, Frame *source, Frame *&tailLow,
    Frame *&tailHigh, const camel::runtime::ExecPlan *layout,
    std::span<const runtime_data_idx_t> argSlots) {
    // Existing live frame for the target: self recursion or an alternating
    // partner (branch arms). rootFrame qualifies too; it is reused, never released.
    for (Frame *candidate : {source, tailHigh, tailLow, rootFrame}) {
        if (candidate != nullptr && candidate->graph() == target) {
            bindDirectCallFrameSlots(source, candidate, layout, argSlots, callArgScratch_);
            return candidate;
        }
    }
    // A free position directly above the source frame.
    if (source == rootFrame && tailLow == nullptr) {
        tailLow = framePool_.acquire(target);
        bindDirectCallFrameSlots(source, tailLow, layout, argSlots, callArgScratch_);
        return tailLow;
    }
    if (source == tailLow && tailHigh == nullptr) {
        tailHigh = framePool_.acquire(target);
        bindDirectCallFrameSlots(source, tailHigh, layout, argSlots, callArgScratch_);
        return tailHigh;
    }
    if (source == tailLow) {
        // Replace the partner above the source; the source itself stays live.
        framePool_.release(tailHigh);
        tailHigh = framePool_.acquire(target);
        bindDirectCallFrameSlots(source, tailHigh, layout, argSlots, callArgScratch_);
        return tailHigh;
    }
    // The source is rootFrame with tail frames above it, or tailHigh: stage the
    // arguments, drop both tail frames (top first), and start over above root.
    callArgScratch_.resize(argSlots.size());
    for (size_t argIndex = 0; argIndex < argSlots.size(); ++argIndex) {
        callArgScratch_[argIndex] = source->get<slot_t>(argSlots[argIndex]);
    }
    releaseTailFrames(tailLow, tailHigh);
    tailLow = framePool_.acquire(target);
    for (size_t argIndex = 0; argIndex < argSlots.size(); ++argIndex) {
        tailLow->set(layout->calleePortSlots[argIndex], callArgScratch_[argIndex]);
    }
    return tailLow;
}

void NodeVMSchedPass::releaseTailFrames(Frame *&tailLow, Frame *&tailHigh) {
    if (tailHigh != nullptr) {
        framePool_.release(tailHigh);
        tailHigh = nullptr;
    }
    if (tailLow != nullptr) {
        framePool_.release(tailLow);
        tailLow = nullptr;
    }
}

// Execute one runtime graph call. Arguments for CALL/FUNC are copied from the
// source frame into the callee frame's ports and closure slots.
NodeVMSchedPass::NodeVMSchedPass(const ctx::context_ptr_t &ctx)
    : RuntimeGraphSchedulePass(ctx),
      maxRecursionDepth_(camel::execute::recursionLimit(*ctx).frames),
      framePool_(camel::execute::framePoolBytes(maxRecursionDepth_)) {
    framePool_.registerGcTracer();
}

void NodeVMSchedPass::requireRecursionRoom(const camel::runtime::GCGraph *caller) const {
    // currRecursionDepth_ includes the activation of the entry graph and its call of main,
    // which are not calls of the program (camel/execute/limits.h).
    const size_t openCalls = currRecursionDepth_ - 1;
    if (openCalls >= maxRecursionDepth_ + camel::execute::kEntryCalls) {
        camel::execute::throwRecursionLimitReached(caller->name(), maxRecursionDepth_);
    }
    if (camel::execute::nativeStackNearlyExhausted()) [[unlikely]] {
        camel::execute::throwNativeStackExhausted(caller->name(), openCalls);
    }
}

slot_t NodeVMSchedPass::call(
    camel::runtime::GCGraph *rootRuntimeGraph, Frame *rootFrame,
    const camel::runtime::Program *rootProgram) {
    // Tells a debugger, when one is attached, that an activation starts and ends here
    // (camel/execute/observer.h). One load of a pointer otherwise.
    camel::execute::ActivationScope observedActivation;
    currRecursionDepth_++;
    slot_t result = NullSlot;
    ASSERT(
        rootRuntimeGraph != nullptr,
        "NodeVM runtime graph must be materialized before execution.");

    Frame *currFrame       = rootFrame;
    Frame *tailLow         = nullptr; // tail-call frames above rootFrame, in stack order
    Frame *tailHigh        = nullptr;
    auto *currRuntimeGraph = rootRuntimeGraph;
    auto gcSafepoint       = [&](std::string_view reason) {
        // NVM keeps safepoints at graph boundaries only. Each boundary checks the global
        // slow-path flag (one atomic load), so collections requested during execution, such as
        // the large-object budget, run at the next graph boundary instead of never.
        mm::safepoint(reason);
    };
    // The node being executed, so a fault points at its source expression.
    gc_node_ref_t faultNodeRef = kInvalidNodeRef;
    try {
        const camel::runtime::ExecPlan *currPlan = &currRuntimeGraph->execPlan();
        std::span<const gc_node_ref_t> currNodes = currPlan->topo;
        const camel::runtime::ExecNode *currRecords = currPlan->nodes().data();
        // The program the running graph belongs to: its direct callees are in it.
        const camel::runtime::Program *currProgram =
            rootProgram != nullptr ? rootProgram : &programs_.of(currRuntimeGraph);

        // Branch execution is implemented by skipping until the selected arm's
        // entry, executing through the arm tail, and then resuming at JOIN.
        gc_node_ref_t tillNode = kInvalidNodeRef;
        gc_node_ref_t skipNode = kInvalidNodeRef;
        gc_node_ref_t joinNode = kInvalidNodeRef;
        // A BRCH met while an arm is still running (a branch nested in an arm of the same graph,
        // e.g. after inlining) must not lose that arm's end: the enclosing (tail, join) pairs
        // wait here and are restored when the nested JOIN is reached.
        std::vector<std::pair<gc_node_ref_t, gc_node_ref_t>> enclosingArms;
        // The loops of the graph that are running, the innermost last.
        uint32_t resumedLoop = camel::execute::kNoLoop;

        // Tail-call loop. Rebind currRuntimeGraph/currFrame instead of growing the C++ stack.
    loop_start: {
        gcSafepoint("nodevm graph boundary");
        const size_t nodesSize = currNodes.size();
        const bool hasLoops    = !currPlan->loops.empty();
        resumedLoop = camel::execute::kNoLoop;

        size_t i = 0;
        for (; i < nodesSize; ++i) {
            const camel::runtime::ExecNode &en = currRecords[i];
            const gc_node_ref_t nodeRef = en.ref;
            faultNodeRef                = nodeRef;
            const GCNode *n             = en.node;

            if (tillNode != kInvalidNodeRef) {
                if (tillNode == nodeRef) {
                    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                        "NodeVM",
                        "Reached tillNode [{}/{}] graph={}: ref={}",
                        i + 1,
                        currNodes.size(),
                        currRuntimeGraph->name(),
                        nodeRef));
                    tillNode = kInvalidNodeRef;
                } else {
                    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                        "NodeVM",
                        "Skipping node [{}/{}] graph={}: ref={}",
                        i + 1,
                        currNodes.size(),
                        currRuntimeGraph->name(),
                        nodeRef));
                    continue;
                }
            }
            // A loop of the graph starts, tests its round, or ends one here. These come before the
            // end of an arm is looked for: going back to the start of a round does not pass it.
            if (hasLoops) [[unlikely]] {
                const size_t resume = camel::execute::stepLoops(
                    *currPlan,
                    i,
                    *currFrame,
                    resumedLoop,
                    [&](const camel::runtime::ExecLoop &loop, size_t) {
                        try {
                            gcSafepoint("nodevm loop round");
                        } catch (const camel::core::mm::MemoryExhausted &e) {
                            if (loop.collectionCount == 0) {
                                throw;
                            }
                            throw e.during(
                                "collecting the values a loop produces each round",
                                loop.origin);
                        }
                    });
                if (resume != camel::execute::kNoLoopJump) {
                    i = resume - 1;
                    continue;
                }
            }
            if (skipNode == kInvalidNodeRef && !enclosingArms.empty() && nodeRef == joinNode) {
                // The nested branch is over; the enclosing arm continues (its tail may be
                // this very JOIN, which the next check handles).
                skipNode = enclosingArms.back().first;
                joinNode = enclosingArms.back().second;
                enclosingArms.pop_back();
            }
            if (skipNode != kInvalidNodeRef && skipNode == nodeRef) {
                EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                    "NodeVM",
                    "Reached skipNode [{}/{}] graph={}: ref={}",
                    i + 1,
                    currNodes.size(),
                    currRuntimeGraph->name(),
                    nodeRef));
                skipNode = kInvalidNodeRef;
                tillNode = joinNode;
            }

            // Debugger hook: live in release builds, one load of a pointer when no observer
            // is installed. It fires after branch-skip resolution and before the node
            // executes, so breakpoints and steps stop at the node that is about to run.
            if (auto *observer = camel::execute::executionObserver()) [[unlikely]] {
                observer->beforeNode(currRuntimeGraph, currFrame, nodeRef);
            }

            EXEC_WHEN_DEBUG({
                CAMEL_LOG_DEBUG_S(
                    "NodeVM",
                    "Executing node [{}/{}] graph={}: ref={} kind={} slot={}",
                    i + 1,
                    currNodes.size(),
                    currRuntimeGraph->name(),
                    nodeRef,
                    static_cast<int>(n->kind),
                    n->dataIndex);
            });
            switch (n->kind) {
            case GCNodeKind::Cast: {
                const auto normInputs = en.normInputs();
                ASSERT(!normInputs.empty(), "CAST node must have one norm input.");
                const auto srcIdx = planDataIndex(*currPlan, normInputs.front());
                Type *srcType     = currFrame->typeAt<Type>(srcIdx);
                Type *tgtType     = n->dataType();
                slot_t value      = currFrame->get<slot_t>(srcIdx);
                slot_t result     = tgtType->castSlotFrom(value, srcType);
                currFrame->set(n->dataIndex, result);
            } break;

            case GCNodeKind::Copy: {
                const auto normInputs = en.normInputs();
                ASSERT(!normInputs.empty(), "COPY node must have one norm input.");
                gc_data_idx_t srcIdx = planDataIndex(*currPlan, normInputs.front());
                TypeCode srcCode     = currFrame->codeAt(srcIdx);
                if (isGCTraced(srcCode)) {
                    Object *srcData  = currFrame->get<Object *>(srcIdx);
                    Type *srcTypePtr = currFrame->typeAt<Type>(srcIdx);
                    currFrame->set(
                        n->dataIndex,
                        srcData->clone(mm::autoSpace(), srcTypePtr, false));
                } else {
                    currFrame->set(n->dataIndex, currFrame->get<slot_t>(srcIdx));
                }
            } break;

            case GCNodeKind::Fill: {
                const auto normInputs = en.normInputs();
                const auto dataInputs = en.withInputs();
                ASSERT(!normInputs.empty(), "FILL node must have one source input.");
                const auto srcIdx = planDataIndex(*currPlan, normInputs.front());
                Type *srcType     = n->dataType();
                ASSERT(isGCTraced(srcType->code()), "FILL target type is not GC-traced in NodeVM.");
                Object *sourceObj = currFrame->get<Object *>(srcIdx);
                ASSERT(sourceObj != nullptr, "FILL source object is null in NodeVM.");
                Object *srcObj = sourceObj->clone(mm::autoSpace(), srcType, false);
                ASSERT(srcObj != nullptr, "FILL target data is null.");
                std::vector<slot_t> fillValues;
                fillValues.reserve(dataInputs.size());
                for (auto input : dataInputs) {
                    fillValues.push_back(currFrame->get<slot_t>(planDataIndex(*currPlan, input)));
                }
                camel::execute::writeRuntimeFillSlots(
                    srcObj,
                    srcType,
                    en.bodyAs<camel::runtime::GCFillBody>(),
                    fillValues);
                currFrame->set(n->dataIndex, srcObj);
            } break;

            case GCNodeKind::Accs: {
                const auto normInputs = en.normInputs();
                ASSERT(!normInputs.empty(), "ACCS node must have one source input.");
                gc_data_idx_t srcIdx = planDataIndex(*currPlan, normInputs.front());
                const auto *body     = en.bodyAs<GCAccsBody>();
                if (body->accsKind == camel::runtime::GCAccsKind::TupleIndex) {
                    size_t idx = body->value;
                    Tuple *t   = currFrame->get<Tuple *>(srcIdx);
                    ASSERT(idx < t->size(), "Tuple index out of bounds in NodeVM.");
                    currFrame->set(n->dataIndex, t->get<slot_t>(idx));
                } else {
                    const std::string_view keyView = body->key();
                    std::string key                = std::string(keyView.data(), keyView.size());
                    Struct *s                      = currFrame->get<Struct *>(srcIdx);
                    Type *structType               = currFrame->typeAt<Type>(srcIdx);
                    currFrame->set(n->dataIndex, s->get<slot_t>(key, structType));
                }
            } break;

            case GCNodeKind::Brch: {
                const size_t jumpIdx =
                    camel::execute::selectRuntimeBranchArm(*currPlan, en, currFrame);
                currFrame->set(n->dataIndex, fromSlot<Int32>(static_cast<Int32>(jumpIdx)));

                const auto *body = en.bodyAs<GCBrchBody>();
                ASSERT(jumpIdx < en.armCount, "Branch arm index out of range in NodeVM.");
                // The arm runs from its first executable node in the linear order (not from
                // its recorded head: after inlining, nodes of the arm can precede the head)
                // through its tail. An arm with nothing to execute resumes at the JOIN.
                if (skipNode != kInvalidNodeRef) {
                    enclosingArms.emplace_back(skipNode, joinNode);
                }
                joinNode = body->join;
                tillNode = currPlan->branchArmEntriesOf(i)[jumpIdx];
                skipNode = tillNode == joinNode
                               ? kInvalidNodeRef
                               : currPlan->branchArmTailsOf(i)[jumpIdx];
                EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                    "NodeVM",
                    "BRCH ref {}: jumpIdx={}, branches={}, tillNode={}, skipNode={}, joinNode={}",
                    nodeRef,
                    jumpIdx,
                    arms.size(),
                    tillNode,
                    skipNode,
                    joinNode));
            } break;

            case GCNodeKind::Join: {
                const auto nargs = en.normInputs();
                const auto wargs = en.withInputs();
                ASSERT(!nargs.empty(), "JOIN must have norm input (branch index).");
                int32_t brIndex = currFrame->get<int32_t>(planDataIndex(*currPlan, nargs.front()));
                ASSERT(
                    brIndex >= 0 && static_cast<size_t>(brIndex) < wargs.size(),
                    "JOIN branch index out of range in NodeVM.");
                if (n->dataIndex == 0) {
                    break;
                }
                if (n->dataType() == Type::Void()) {
                    currFrame->set(n->dataIndex, NullSlot);
                    break;
                }
                // The selected arm has already executed sequentially. Its tail
                // slot now holds the branch result consumed by JOIN.
                slot_t branchResult = currFrame->get<slot_t>(
                    planDataIndex(*currPlan, wargs[static_cast<size_t>(brIndex)]));
                currFrame->set(n->dataIndex, branchResult);
            } break;

            case GCNodeKind::Call: {
                const auto withInputs = en.withInputs();
                ASSERT(!withInputs.empty(), "CALL node must have exactly one callee input");
                Function *func =
                    currFrame->get<Function *>(planDataIndex(*currPlan, withInputs.front()));
                ASSERT(func != nullptr, "NodeVM CALL resolved a null Function callee.");
                auto *funcRuntimeGraph = programs_.graphOf(func);
                ASSERT(
                    funcRuntimeGraph != nullptr,
                    "NodeVM indirect CALL requires a materialized runtime graph target.");

                requireRecursionRoom(currRuntimeGraph);
                Frame *funcFrame = framePool_.acquire(funcRuntimeGraph);
                camel::execute::fillFrameForIndirectCall(
                    currFrame,
                    funcFrame,
                    currRuntimeGraph,
                    nodeRef);

                slot_t callResult = call(funcRuntimeGraph, funcFrame);
                if (n->dataIndex != 0) {
                    currFrame->set(n->dataIndex, callResult);
                }
            } break;

            case GCNodeKind::Func: {
                auto *callerRuntimeGraph = currRuntimeGraph;
                auto *runtimeTarget      = currProgram->graph(currPlan->directCallTargetIds[i]);
                ASSERT(
                    runtimeTarget != nullptr,
                    "NodeVM direct FUNC target must have a materialized runtime graph.");
                const bool isTailCall = currPlan->directCallTailEligible[i] != 0 ||
                                        currPlan->directCallFeedsTailJoin[i] != 0;
                if (isTailCall) {
                    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                        "NodeVM",
                        "Optimizing tail-call for node [{}/{}] graph={}: ref={}",
                        i + 1,
                        currNodes.size(),
                        currRuntimeGraph->name(),
                        nodeRef));
                    // Reuse the current C++ stack frame by rebinding graph/frame state.
                    if (auto *observer = camel::execute::executionObserver()) [[unlikely]] {
                        observer->tailCall();
                    }
                    Frame *lastFrame = currFrame;
                    tillNode         = kInvalidNodeRef;
                    skipNode         = kInvalidNodeRef;
                    enclosingArms.clear();

                    const auto argSlots = currPlan->directCallArgSlotsOf(i);
                    if (runtimeTarget != currRuntimeGraph) {
                        // Switch to the callee graph and its topo sequence.
                        currRuntimeGraph = runtimeTarget;
                        currPlan         = &currRuntimeGraph->execPlan();
                        currRecords      = currPlan->nodes().data();
                        currNodes        = currPlan->topo;
                    }
                    const auto *layout = &runtimeTarget->execPlan();
                    currFrame = tailCallFrame(
                        runtimeTarget,
                        rootFrame,
                        lastFrame,
                        tailLow,
                        tailHigh,
                        layout,
                        argSlots);
                    goto loop_start;
                }

                requireRecursionRoom(currRuntimeGraph);
                Frame *funcFrame    = framePool_.acquire(runtimeTarget);
                const auto argSlots = currPlan->directCallArgSlotsOf(i);
                const auto *layout = &runtimeTarget->execPlan();
                bindDirectCallFrameSlots(currFrame, funcFrame, layout, argSlots, callArgScratch_);
                slot_t callResult = call(runtimeTarget, funcFrame, currProgram);

                if (n->dataIndex != 0) {
                    currFrame->set(n->dataIndex, callResult);
                }
            } break;

            case GCNodeKind::Oper: {
                const auto *body = en.bodyAs<GCOperBody>();
                operator_t opFunc =
                    currPlan->resolvedOpers[nodeRef].load(std::memory_order_relaxed);
                if (!opFunc) {
                    const std::string uri(body->uri());
                    auto found = context_->execMgr().find(uri);
                    if (found) {
                        opFunc = *found;
                        currPlan->resolvedOpers[nodeRef].store(opFunc, std::memory_order_relaxed);
                    } else {
                        if (camel::core::OperatorTable::instance().isMarked(body->oper)) {
                            evalMarkedOperator(body->oper, currRuntimeGraph, nodeRef, *currFrame);
                            break;
                        }
                        throwRuntimeFault(RuntimeDiag::UnrecognizedOperatorURI, uri);
                    }
                }

                slot_t result = evalNativeOperator(currRuntimeGraph, en, *currPlan, *currFrame, opFunc);

                currFrame->set(n->dataIndex, result);
            } break;

            case GCNodeKind::Loop:
                // The rounds ran at the events of the plan; the state is the value.
                ASSERT(hasLoops, "NodeVM runs only loops that are regions of their graph.");
                break;

            case GCNodeKind::Port:
                [[fallthrough]];
            case GCNodeKind::Data:
                [[fallthrough]];
            case GCNodeKind::Sync:
                [[fallthrough]];
            case GCNodeKind::Gate:
                break;

            default: {
                ASSERT(
                    false,
                    std::format(
                        "Node type {} should not appear in NodeVM execution.",
                        static_cast<int>(n->kind)));
            } break;
            }

            EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                "NodeVM",
                "Executed node [{}/{}]: ref={}",
                i + 1,
                currNodes.size(),
                nodeRef));
        }
    }

        currRecursionDepth_--;

        result = camel::execute::readRuntimeGraphReturn(currRuntimeGraph, currFrame);

        releaseTailFrames(tailLow, tailHigh);
        framePool_.release(rootFrame);

        return result;
    } catch (const RuntimeFault &fault) {
        // A debugger stops here, while every frame of the stack is still alive.
        if (auto *observer = camel::execute::executionObserver()) [[unlikely]] {
            observer->faultRaised(
                fault,
                faultIsolationDepth() != 0 && isIsolatableFault(fault.diag()));
        }
        currRecursionDepth_--;
        releaseTailFrames(tailLow, tailHigh);
        if (rootFrame) {
            framePool_.release(rootFrame);
        }
        auto sourceContext      = context_ ? context_->sourceContext() : nullptr;
        auto *faultRuntimeGraph = currFrame ? currFrame->runtimeGraph() : currRuntimeGraph;
        auto site               = makeGraphExecutionSite(
            sourceContext,
            faultRuntimeGraph ? faultRuntimeGraph : static_cast<camel::runtime::GCGraph *>(nullptr),
            currRecursionDepth_);
        if (currRuntimeGraph && faultNodeRef != kInvalidNodeRef) {
            if (const uint64_t origin = currRuntimeGraph->nodeOrigin(faultNodeRef)) {
                site.cachedOrigin = origin;
            }
        }
        throw reportRuntimeFault(*context_, fault, site);
    } catch (Diagnostic &) {
        currRecursionDepth_--;
        releaseTailFrames(tailLow, tailHigh);
        if (rootFrame) {
            framePool_.release(rootFrame);
        }
        throw;
    }
}

const camel::runtime::Program *
NodeVMSchedPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    camel::runtime::GCGraph *graph = program->root();
    (void)os;
    ASSERT(graph != nullptr, "NodeVM requires a non-null runtime root graph.");
    camel::execute::RunScope runScope(programs_, *context_, program);
    Frame *rootFrame = framePool_.acquire(graph);
    slot_t result    = call(graph, rootFrame);
    context_->captureProcessExitCode(graph, result);
    return nullptr;
}

slot_t NodeVMSchedPass::callGraph(
    const camel::runtime::Program *program, camel::runtime::GCGraph *graph,
    std::span<const slot_t> arguments) {
    camel::execute::RunScope runScope(programs_, *context_, program);
    Frame *frame      = framePool_.acquire(graph);
    const auto ports  = graph->normPorts();
    ASSERT(ports.size() == arguments.size(), "callGraph needs one argument per parameter.");
    for (size_t i = 0; i < ports.size(); ++i) {
        frame->set(graph->node(ports[i])->dataIndex, arguments[i]);
    }
    return call(graph, frame, program);
}

void NodeVMSchedPass::evalMarkedOperator(
    camel::core::OperId oper, camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef,
    Frame &currFrame) {
    // The way the operator calls its function is its declaration's (OperatorSemantics::
    // elementCalls); NodeVM runs every call in order, whatever the calls leave open.
    const auto *calls = camel::core::OperatorTable::instance().elementCalls(oper);
    ASSERT(calls != nullptr, "A marked operator declares how it calls its function.");
    switch (calls->shape) {
    case camel::core::sem::ElementShape::Map:
        // `apply` (f from T to T) returns a new array, like `map`: it no longer changes the
        // array it is given (design principles 11.3).
        evalMarkedOperator_map_arr(graph, nodeRef, currFrame);
        break;
    case camel::core::sem::ElementShape::Filter:
        evalMarkedOperator_filter_arr(graph, nodeRef, currFrame);
        break;
    case camel::core::sem::ElementShape::Fold:
        evalMarkedOperator_reduce_arr(graph, nodeRef, currFrame);
        break;
    case camel::core::sem::ElementShape::Foreach:
        evalMarkedOperator_foreach_arr(graph, nodeRef, currFrame);
        break;
    case camel::core::sem::ElementShape::Isolate:
        evalMarkedOperator_isolate(graph, nodeRef, currFrame);
        break;
    }
}

void NodeVMSchedPass::evalMarkedOperator_map_arr(
    camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, Frame &currFrame) {
    const auto normInputs = graph->normInputsOf(nodeRef);
    const auto withInputs = graph->withInputsOf(nodeRef);
    ASSERT(
        !normInputs.empty() && !withInputs.empty(),
        "map_arr requires array and function inputs.");
    const auto arrSlot    = dataIndexOf(graph, normInputs.front());
    const auto funcSlot   = dataIndexOf(graph, withInputs.front());
    const auto resultSlot = dataIndexOf(graph, nodeRef);
    const size_t arrSize  = currFrame.get<Array *>(arrSlot)->size();
    auto *resultType      = currFrame.typeAt<ArrayType>(resultSlot);
    Array *res            = Array::create(mm::autoSpace(), resultType, arrSize);
    currFrame.set(resultSlot, res);

    for (size_t i = 0; i < arrSize; ++i) {
        Array *arr     = currFrame.get<Array *>(arrSlot);
        Function *func = currFrame.get<Function *>(funcSlot);
        slot_t element = arr->getSlot(i);
        requireRecursionRoom(graph);
        Frame *frame = framePool_.acquire(programs_.graphOf(func));
        bindMarkedFunctionFrame(frame, func, std::span<const slot_t>(&element, 1));
        slot_t result = call(programs_.graphOf(func), frame);
        currFrame.get<Array *>(resultSlot)->set<slot_t>(i, result, resultType);
    }
}

void NodeVMSchedPass::evalMarkedOperator_filter_arr(
    camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, Frame &currFrame) {
    const auto normInputs = graph->normInputsOf(nodeRef);
    const auto withInputs = graph->withInputsOf(nodeRef);
    ASSERT(
        !normInputs.empty() && !withInputs.empty(),
        "filter_arr requires array and function inputs.");
    const auto arrSlot    = dataIndexOf(graph, normInputs.front());
    const auto funcSlot   = dataIndexOf(graph, withInputs.front());
    const auto resultSlot = dataIndexOf(graph, nodeRef);
    const size_t arrSize  = currFrame.get<Array *>(arrSlot)->size();
    // Starts empty: Array::create(n) makes n null elements, and the kept ones were appended
    // after them.
    auto *resultType = currFrame.typeAt<ArrayType>(resultSlot);
    Array *filtered  = Array::create(mm::autoSpace(), resultType, 0);
    currFrame.set(resultSlot, filtered);

    for (size_t i = 0; i < arrSize; ++i) {
        Array *arr     = currFrame.get<Array *>(arrSlot);
        Function *func = currFrame.get<Function *>(funcSlot);
        slot_t element = arr->getSlot(i);
        requireRecursionRoom(graph);
        Frame *frame = framePool_.acquire(programs_.graphOf(func));
        bindMarkedFunctionFrame(frame, func, std::span<const slot_t>(&element, 1));
        slot_t result = call(programs_.graphOf(func), frame);
        if (fromSlot<bool>(result)) {
            Array *target = currFrame.get<Array *>(resultSlot);
            target->append(currFrame.get<Array *>(arrSlot)->getSlot(i), resultType);
        }
    }
    currFrame.get<Array *>(resultSlot)->shrinkToFit(resultType);
}

void NodeVMSchedPass::evalMarkedOperator_reduce_arr(
    camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, Frame &currFrame) {
    const auto normInputs = graph->normInputsOf(nodeRef);
    const auto withInputs = graph->withInputsOf(nodeRef);
    ASSERT(
        !normInputs.empty() && withInputs.size() >= 2,
        "reduce_arr requires array, function, and initial value inputs.");
    const auto arrSlot    = dataIndexOf(graph, normInputs.front());
    const auto funcSlot   = dataIndexOf(graph, withInputs[0]);
    const auto initSlot   = dataIndexOf(graph, withInputs[1]);
    const auto resultSlot = dataIndexOf(graph, nodeRef);
    const size_t arrSize  = currFrame.get<Array *>(arrSlot)->size();
    slot_t init           = currFrame.get<slot_t>(initSlot);

    if (arrSize == 0) {
        currFrame.set(resultSlot, init);
        return;
    }
    currFrame.set(resultSlot, init);

    for (size_t i = 0; i < arrSize; ++i) {
        Array *arr          = currFrame.get<Array *>(arrSlot);
        Function *func      = currFrame.get<Function *>(funcSlot);
        const slot_t args[] = {currFrame.get<slot_t>(resultSlot), arr->getSlot(i)};
        requireRecursionRoom(graph);
        Frame *frame = framePool_.acquire(programs_.graphOf(func));
        bindMarkedFunctionFrame(frame, func, std::span<const slot_t>(args, 2));
        currFrame.set(resultSlot, call(programs_.graphOf(func), frame));
    }
}

void NodeVMSchedPass::evalMarkedOperator_foreach_arr(
    camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, Frame &currFrame) {
    const auto normInputs = graph->normInputsOf(nodeRef);
    const auto withInputs = graph->withInputsOf(nodeRef);
    ASSERT(
        !normInputs.empty() && !withInputs.empty(),
        "foreach_arr requires array and function inputs.");
    const auto arrSlot   = dataIndexOf(graph, normInputs.front());
    const auto funcSlot  = dataIndexOf(graph, withInputs.front());
    const size_t arrSize = currFrame.get<Array *>(arrSlot)->size();

    for (size_t i = 0; i < arrSize; ++i) {
        Array *arr     = currFrame.get<Array *>(arrSlot);
        Function *func = currFrame.get<Function *>(funcSlot);
        slot_t element = arr->getSlot(i);
        requireRecursionRoom(graph);
        Frame *frame = framePool_.acquire(programs_.graphOf(func));
        bindMarkedFunctionFrame(frame, func, std::span<const slot_t>(&element, 1));
        call(programs_.graphOf(func), frame);
    }
    currFrame.set(dataIndexOf(graph, nodeRef), NullSlot);
}

// The fault-isolation node (docs/technical/33_fault_isolation.md): a call of the function value
// given first, in which a fault of the program becomes the failed form of the node's `T!`.
void NodeVMSchedPass::evalMarkedOperator_isolate(
    camel::runtime::GCGraph *graph, gc_node_ref_t nodeRef, Frame &currFrame) {
    const auto normInputs = graph->normInputsOf(nodeRef);
    ASSERT(!normInputs.empty(), "The fault-isolation node requires a function input.");
    const auto resultSlot = dataIndexOf(graph, nodeRef);
    Function *func        = currFrame.get<Function *>(dataIndexOf(graph, normInputs.front()));
    ASSERT(
        func != nullptr && programs_.graphOf(func) != nullptr,
        "Fault-isolation callee is null.");
    auto *target = programs_.graphOf(func);

    std::vector<slot_t> args;
    args.reserve(normInputs.size() - 1);
    for (size_t i = 1; i < normInputs.size(); ++i) {
        args.push_back(currFrame.get<slot_t>(dataIndexOf(graph, normInputs[i])));
    }

    // The room for the call is the caller's: a recursion limit reached here is a fault of the
    // enclosing graph, outside the boundary.
    requireRecursionRoom(graph);
    const size_t depthBefore = currRecursionDepth_;
    (void)depthBefore;
    Frame *frame = framePool_.acquire(target);
    bindMarkedFunctionFrame(frame, func, args);
    slot_t value = NullSlot;
    try {
        camel::core::error::FaultIsolationScope boundary;
        value = call(target, frame);
    } catch (const Diagnostic &fault) {
        if (!camel::core::error::isIsolatedFault(fault)) {
            throw;
        }
        // Every activation inside the boundary released its frames and its depth count
        // while the exception passed (see call()); nothing of the boundary is left.
        ASSERT(
            currRecursionDepth_ == depthBefore,
            "A fault left the call depth changed at its isolation boundary.");
        currFrame.set(resultSlot, camel::execute::isolatedFaultToError(fault));
        return;
    }
    const Type *calleeResult = target->funcType() ? target->funcType()->exitType() : nullptr;
    currFrame.set(
        resultSlot,
        camel::execute::wrapIsolatedResult(
            value,
            calleeResult,
            currFrame.typeAt<Type>(resultSlot)));
}

// Keep callback state out of call()'s recursive native frame, like marked-operator helpers.
slot_t NodeVMSchedPass::evalNativeOperator(
    camel::runtime::GCGraph *currRuntimeGraph, const camel::runtime::ExecNode &en,
    const camel::runtime::ExecPlan &plan, Frame &operatorFrame, operator_t opFunc) {
    Frame *currFrame = &operatorFrame;
    operIndices_.clear();
    for (gc_node_ref_t inputRef : en.normInputs())
        operIndices_.push_back(plan.dataIndexByRef[inputRef]);
    size_t normCnt = operIndices_.size();
    for (gc_node_ref_t inputRef : en.withInputs())
        operIndices_.push_back(plan.dataIndexByRef[inputRef]);

    const bool callsGraphs =
        camel::core::OperatorTable::instance().callsGraphs(en.bodyAs<GCOperBody>()->oper);
    // Nested calls reuse operIndices_. Keep this operator's views alive independently.
    std::vector<runtime_data_idx_t> ownedIndices;
    if (callsGraphs)
        ownedIndices = operIndices_;
    const auto &indices = callsGraphs ? ownedIndices : operIndices_;
    data_arr_t nargs{indices.data(), normCnt};
    data_arr_t wargs{indices.data() + normCnt, indices.size() - normCnt};
    FrameArgsView withView(*currFrame, wargs);
    FrameArgsView normView(*currFrame, nargs);
    withView.setResultType(en.node->dataType());
    normView.setResultType(withView.resultType());

    if (camel::core::effect_trace::enabled()) [[unlikely]] {
        camel::core::effect_trace::setSite(currRuntimeGraph->nodeOrigin(en.ref));
    }
    camel::execute::FunctionInvoker invoker;
    if (callsGraphs) {
        invoker = [&](Function *function,
                      std::span<const slot_t>
                          withArgs,
                      std::span<const slot_t>
                          normalArgs) {
            requireRecursionRoom(currRuntimeGraph);
            auto *target = programs_.graphOf(function);
            Frame *frame = framePool_.acquire(target);
            bindMarkedFunctionFrame(frame, function, normalArgs, withArgs);
            return call(target, frame);
        };
    }
    camel::execute::FunctionCallScope callScope(*context_, std::move(invoker));
    return (*opFunc)(withView, normView, *context_);
}
