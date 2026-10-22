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

using namespace camel::core::context;
using namespace camel::core::rtdata;
using namespace camel::core::type;

namespace camel::passes::sched::fastvm {

size_t selectBranchArm(const Bytecode &bc, Frame *frame) {
    ASSERT(frame != nullptr, "FastVM branch selection requires a frame.");

    const data_arr_t nargs = bc.nargs();
    const data_arr_t wargs = bc.wargs();
    ASSERT(nargs.size() == 1, "FastVM BRCH opcode must have exactly one norm operand.");

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

} // namespace camel::passes::sched::fastvm
