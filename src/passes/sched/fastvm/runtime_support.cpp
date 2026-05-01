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
 * Author: OpenAI Codex
 * Created: Apr. 11, 2026
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
