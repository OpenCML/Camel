// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "higher_order.h"

#include "camel/core/context/context.h"
#include "camel/core/mm.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/pure.h"
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
    Array *source = arrayOf(norm);
    const bool useViews = source->isCompact() && source->isAggregateElement();
    const type::DataLayout *viewLayout =
        useViews ? type::pureDataLayout(source->elemType()) : nullptr;
    if (useViews) {
        // A view points into the block; promote an inline array once so the bytes do not move
        // under the callback's safepoint.
        source->ensureBlock();
    }
    const size_t n        = arrayOf(norm)->size();
    const ArrayType *type = resultArrayType(norm, functionOf(with));
    Array *created        = Array::create(mm::autoSpace(), type, n);
    mm::RootHandle result(mm::autoSpace(), created, type, "map result");
    for (size_t i = 0; i < n; ++i) {
        const slot_t element = useViews ? arrayOf(norm)->getElementView(i, viewLayout) : arrayOf(norm)->getSlot(i);
        // The result goes straight into the output array, whose store copies the element bytes;
        //  a view here does not outlive the element.
        const slot_t value = invokeFunction(ctx, functionOf(with), {}, std::span(&element, 1));
        result.getAs<Array>()->set<slot_t>(i, value, type);
    }
    return toSlot<Array *>(result.getAs<Array>());
}

slot_t __filter_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    Array *source = arrayOf(norm);
    const bool useViews = source->isCompact() && source->isAggregateElement();
    const type::DataLayout *viewLayout =
        useViews ? type::pureDataLayout(source->elemType()) : nullptr;
    if (useViews) {
        source->ensureBlock();
    }
    const size_t n   = arrayOf(norm)->size();
    const auto *type = static_cast<const ArrayType *>(norm.type(0));
    // Starts empty: Array::create(n) makes n null elements, and the kept ones are appended.
    mm::RootHandle kept(mm::autoSpace(), Array::create(mm::autoSpace(), type, 0), type, "filter result");
    for (size_t i = 0; i < n; ++i) {
        const slot_t element = useViews ? arrayOf(norm)->getElementView(i, viewLayout) : arrayOf(norm)->getSlot(i);
        // The predicate result is a boolean and the kept element is copied by append: nothing
        //  here outlives the element read.
        const slot_t answer = invokeFunction(ctx, functionOf(with), {}, std::span(&element, 1));
        if (fromSlot<bool>(answer)) {
            kept.getAs<Array>()->append(useViews ? arrayOf(norm)->getElementView(i, viewLayout) : arrayOf(norm)->getSlot(i), type);
        }
    }
    kept.getAs<Array>()->shrinkToFit(type);
    return toSlot<Array *>(kept.getAs<Array>());
}

slot_t __reduce_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    Array *source = arrayOf(norm);
    const bool useViews = source->isCompact() && source->isAggregateElement();
    const type::DataLayout *viewLayout =
        useViews ? type::pureDataLayout(source->elemType()) : nullptr;
    if (useViews) {
        source->ensureBlock();
    }
    const size_t n = arrayOf(norm)->size();
    // The running value lives in a rooted cell: it may be an object, and every call is a
    // safepoint.
    const ArrayType *cellType =
        ArrayType::create(functionOf(with)->graph()->funcType()->exitType());
    Array *created = Array::create(mm::autoSpace(), cellType, 1);
    created->set<slot_t>(0, with.slot(1), cellType);
    mm::RootHandle cell(mm::autoSpace(), created, cellType, "reduce accumulator");
    for (size_t i = 0; i < n; ++i) {
        const slot_t args[] = {cell.getAs<Array>()->get<slot_t>(0), useViews ? arrayOf(norm)->getElementView(i, viewLayout) : arrayOf(norm)->getSlot(i)};
        const slot_t raw    = invokeFunction(ctx, functionOf(with), {}, args);
        // The accumulator is the value the next round reads, so it is a value, not a view of the
        //  element this round read. Materialise here, as the bytecode VM does; the store
        // then asserts that no view reached it.
        const slot_t next = materializeCallResult(
            raw, functionOf(with)->graph()->funcType()->exitType());
        storeReduceAccumulator(cell.getAs<Array>(), next, cellType);
    }
    return cell.getAs<Array>()->get<slot_t>(0);
}

slot_t __foreach_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    Array *source = arrayOf(norm);
    const bool useViews = source->isCompact() && source->isAggregateElement();
    const type::DataLayout *viewLayout =
        useViews ? type::pureDataLayout(source->elemType()) : nullptr;
    if (useViews) {
        source->ensureBlock();
    }
    const size_t n = arrayOf(norm)->size();
    for (size_t i = 0; i < n; ++i) {
        const slot_t element = useViews ? arrayOf(norm)->getElementView(i, viewLayout) : arrayOf(norm)->getSlot(i);
        (void)invokeFunction(ctx, functionOf(with), {}, std::span(&element, 1));
    }
    return NullSlot;
}
