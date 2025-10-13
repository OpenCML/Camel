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
 * Updated: Oct. 13, 2025
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

    const data_ptr_t &lhs = frame.get(ins[0]);
    const data_ptr_t &rhs = frame.get(ins[1]);

    const type_ptr_t &lhsType = lhs->type();
    const type_ptr_t &rhsType = rhs->type();

    func_ptr_t lhsFunc = lhs->as<FunctionData>(Type::Func());
    func_type_ptr_t lhsFuncType = lhsFunc->funcType();
    // Graph &lhsGraph = lhs->as<FunctionData>(Type::Func())->graph();

    func_ptr_t rhsFunc = rhs->as<FunctionData>(Type::Func());
    func_type_ptr_t rhsFuncType = rhsFunc->funcType();
    // Graph &rhsGraph = rhs->as<FunctionData>(Type::Func())->graph();

    return OperatorReturnCode::OK;
}
