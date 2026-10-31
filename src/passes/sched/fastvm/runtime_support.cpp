// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Shared FastVM interpreter helpers.
 *
 * The switch-based and computed-goto interpreters must agree on bytecode
 * control-flow semantics. Consolidating the BRCH decision logic here prevents
 * them from drifting apart when branch matching rules evolve.
 */

#include "runtime_support.h"

#include "camel/core/rtdata/base.h"
#include "camel/execute/graph_runtime_support.h"

using namespace camel::core::context;
using namespace camel::core::rtdata;
using namespace camel::core::type;

namespace camel::passes::sched::fastvm {

size_t selectBranchArm(const Bytecode &bc, Frame *frame) {
    GUARD(frame != nullptr, "FastVM branch selection requires a frame.");

    const data_arr_t nargs = bc.nargs();
    const data_arr_t wargs = bc.wargs();
    GUARD(nargs.size() == 1, "FastVM BRCH opcode must have exactly one norm operand.");

    if (bc.withCnt() == 0) {
        return frame->get<bool>(nargs[0]) ? 0 : 1;
    }

    const TypeCode condType = frame->codeAt(nargs[0]);
    if (isGCTraced(condType)) {
        Type *condTypePtr = frame->typeAt<Type>(nargs[0]);
        auto *condData    = frame->get<Object *>(nargs[0]);
        for (size_t i = 0; i < bc.withCnt(); ++i) {
            auto *caseData = frame->get<Object *>(wargs[i]);
            if (condData->equals(caseData, condTypePtr, false)) {
                return i;
            }
        }
        return bc.withCnt();
    }

    const slot_t condData = frame->get<slot_t>(nargs[0]);
    for (size_t i = 0; i < bc.withCnt(); ++i) {
        if (condData == frame->get<slot_t>(wargs[i])) {
            return i;
        }
    }
    return bc.withCnt();
}

slot_t funcValueForCall(const BytecodeHeader *call, Frame *callerFrame, slot_t result) {
    // A return that lands in an indirect CALL hands the callee's result on as it is: only a
    // direct call carries the mask of the captured variables its callee writes.
    if (!isDirectCallOpcode(call->opcode)) {
        return result;
    }
    const uint32_t written = getFuncExtraWriteMask(call);
    if (written == 0) {
        return result;
    }
    auto *callee = getFuncExtraRuntimeGraph(call);
    GUARD(callee != nullptr, "A call that writes captured variables needs its callee's graph.");
    // The caller's operands are its arguments in order: its norm arguments first, then its `with`
    // arguments, which are what the callee's with-ports are passed as. The callee's with-ports
    // are its declared `with` parameters and then its captures, in the order it made them, so
    // they are the last withPorts().size() operands.
    const data_arr_t args = call->directCallSrcArgs();
    const size_t withCount = callee->withPorts().size();
    GUARD(args.size() >= withCount, "A call's operands must hold its `with` arguments.");
    std::vector<slot_t> withValues;
    withValues.reserve(withCount);
    for (size_t i = 0; i < withCount; ++i) {
        withValues.push_back(callerFrame->get<slot_t>(args[args.size() - withCount + i]));
    }
    return camel::execute::runtimeFuncValue(
        callerFrame->typeAt<Type>(call->result), callee, written, withValues, result);
}

} // namespace camel::passes::sched::fastvm
