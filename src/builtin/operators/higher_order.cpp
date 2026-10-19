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
 * Author: Camel Contributors
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "higher_order.h"

#include "camel/core/context/context.h"
#include "camel/core/mm.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/execute/function_call.h"
#include "camel/runtime/graph.h"

namespace mm = camel::core::mm;
using namespace camel::core::rtdata;
using namespace camel::core::type;
using camel::execute::invokeFunction;

namespace {

/// The function operand and the array operand are read from their slots at every call: a call
/// is a safepoint, and the operands (traced slots of the caller's frame) are where the
/// collector moves them to.
Function *functionOf(ArgsView &with) { return with.get<Function *>(0); }
Array *arrayOf(ArgsView &norm) { return norm.get<Array *>(0); }

/// The type of the array a call of `function` on the elements gives.
const ArrayType *resultArrayType(ArgsView &norm, Function *function) {
    if (const Type *declared = norm.resultType(); declared && declared->code() == TypeCode::Array) {
        return static_cast<const ArrayType *>(declared);
    }
    return ArrayType::create(function->graph()->funcType()->exitType());
}

} // namespace

slot_t __map_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    const size_t n         = arrayOf(norm)->size();
    const ArrayType *type  = resultArrayType(norm, functionOf(with));
    Array *created         = Array::create(mm::autoSpace(), n);
    for (size_t i = 0; i < n; ++i) {
        created->set<slot_t>(i, NullSlot);
    }
    mm::RootHandle result(mm::autoSpace(), created, type, "map result");
    for (size_t i = 0; i < n; ++i) {
        const slot_t element = arrayOf(norm)->get<slot_t>(i);
        const slot_t value   = invokeFunction(ctx, functionOf(with), {}, std::span(&element, 1));
        result.getAs<Array>()->set<slot_t>(i, value, type);
    }
    return toSlot<Array *>(result.getAs<Array>());
}

slot_t __filter_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    const size_t n        = arrayOf(norm)->size();
    const auto *type      = static_cast<const ArrayType *>(norm.type(0));
    // Starts empty: Array::create(n) makes n null elements, and the kept ones are appended.
    mm::RootHandle kept(mm::autoSpace(), Array::create(mm::autoSpace(), 0), type, "filter result");
    for (size_t i = 0; i < n; ++i) {
        const slot_t element = arrayOf(norm)->get<slot_t>(i);
        const slot_t answer  = invokeFunction(ctx, functionOf(with), {}, std::span(&element, 1));
        if (fromSlot<bool>(answer)) {
            kept.getAs<Array>()->append(arrayOf(norm)->get<slot_t>(i), type);
        }
    }
    kept.getAs<Array>()->shrinkToFit(type);
    return toSlot<Array *>(kept.getAs<Array>());
}

slot_t __reduce_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    const size_t n = arrayOf(norm)->size();
    // The running value lives in a rooted cell: it may be an object, and every call is a
    // safepoint.
    const ArrayType *cellType = ArrayType::create(functionOf(with)->graph()->funcType()->exitType());
    Array *created            = Array::create(mm::autoSpace(), 1);
    created->set<slot_t>(0, with.slot(1), cellType);
    mm::RootHandle cell(mm::autoSpace(), created, cellType, "reduce accumulator");
    for (size_t i = 0; i < n; ++i) {
        const slot_t args[] = {cell.getAs<Array>()->get<slot_t>(0), arrayOf(norm)->get<slot_t>(i)};
        const slot_t next   = invokeFunction(ctx, functionOf(with), {}, args);
        cell.getAs<Array>()->set<slot_t>(0, next, cellType);
    }
    return cell.getAs<Array>()->get<slot_t>(0);
}

slot_t __foreach_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    const size_t n = arrayOf(norm)->size();
    for (size_t i = 0; i < n; ++i) {
        const slot_t element = arrayOf(norm)->get<slot_t>(i);
        (void)invokeFunction(ctx, functionOf(with), {}, std::span(&element, 1));
    }
    return NullSlot;
}
