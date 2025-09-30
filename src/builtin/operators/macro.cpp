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
 * Created: Sep. 29, 2025
 * Updated: Sep. 30, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "macro.h"

#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"
#include "utils/assert.h"

using namespace GraphIR;

OperatorReturnCode __cmp__(node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    if (ins.size() != 2) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<cmp>", "2 args", std::to_string(ins.size()));
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    const data_ptr_t &lhs = frame.get(ins[0]);
    const data_ptr_t &rhs = frame.get(ins[1]);

    const type_ptr_t &lhsType = lhs->type();
    const type_ptr_t &rhsType = rhs->type();

    if (lhsType->code() != TypeCode::Func || rhsType->code() != TypeCode::Func) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<cmp>", "functor", lhsType->toString() + " and " + rhsType->toString());
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    func_ptr_t lhsFunc = lhs->as<FunctionData>(Type::Func());
    func_type_ptr_t lhsFuncType = lhsFunc->funcType();
    graph_ptr_t lhsGraph = lhs->as<FunctionData>(Type::Func())->graph();

    func_ptr_t rhsFunc = rhs->as<FunctionData>(Type::Func());
    func_type_ptr_t rhsFuncType = rhsFunc->funcType();
    graph_ptr_t rhsGraph = rhs->as<FunctionData>(Type::Func())->graph();

    if (rhsFuncType->normParams().size() != 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(
                0,
                "<cmp>",
                "functor with 1 norm param",
                "functor with " + std::to_string(rhsFuncType->normParams().size()) +
                    " norm params");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    // check if the return type of rhs matches the type of the only norm param of lhs
    type_ptr_t lhsReturnType = lhsFuncType->returnType();
    type_ptr_t rhsOnlyNormParamType = std::get<1>(rhsFuncType->normParams()[0]);

    if (!Type::castSafetyCheck(lhsReturnType, rhsOnlyNormParamType)) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(
                0,
                "<cmp>",
                "return type of the first functor",
                lhsReturnType->toString() + " and the only norm param type of the second functor " +
                    rhsOnlyNormParamType->toString());
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    // create a new graph for the composed function
    graph_ptr_t newGraph = Graph::create(self->graph().shared_from_this());

    return OperatorReturnCode::OK;
}
