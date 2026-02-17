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
 * Created: Sep. 25, 2025
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "struct.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/operator.h"

slot_t __len_str__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *s = norm.get<String *>(0);
    return toSlot(static_cast<Int>(s->size()));
}

slot_t __len_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr = norm.get<Array *>(0);
    return toSlot(static_cast<Int>(arr->size()));
}

slot_t __zip__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *lhs = norm.get<Array *>(0);
    Array *rhs = norm.get<Array *>(1);

    size_t n = lhs->size();
    if (rhs->size() != n) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<zip> requires both sequences to have the same length");
        return NullSlot;
    }

    auto resArrayType = norm.type(0);
    auto arrType      = tt::as_ptr<ArrayType>(resArrayType);
    auto resTupleType = tt::as_ptr<TupleType>(arrType->elemType());
    size_t tupSize    = resTupleType->size();

    Array *result = Array::create(mm::autoSpace(), n);
    for (size_t i = 0; i < n; ++i) {
        Tuple *t = Tuple::create(tupSize, mm::autoSpace());
        t->set(0, lhs->get<Int>(i));
        t->set(1, rhs->get<Int>(i));
        result->set(i, t);
    }
    return toSlot(result);
}

slot_t __head_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr = norm.get<Array *>(0);
    if (!arr || arr->size() == 0) {
        return NullSlot;
    }
    return arr->get<slot_t>(0);
}

slot_t __tail_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr = norm.get<Array *>(0);

    size_t n = arr->size();
    if (n <= 1) {
        Array *empty = Array::create(mm::autoSpace(), 0);
        return toSlot(empty);
    }

    Array *res = Array::create(mm::autoSpace(), n - 1);
    for (size_t i = 1; i < n; ++i) {
        res->set(i - 1, arr->get<slot_t>(i));
    }
    return toSlot(res);
}

slot_t __range__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int start = norm.get<Int>(0);
    Int stop  = norm.get<Int>(1);
    Int step  = (norm.size() == 3) ? norm.get<Int>(2) : 1;

    if (step == 0) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<range> step cannot be zero");
        return NullSlot;
    }

    size_t count = 0;
    if ((step > 0 && start < stop) || (step < 0 && start > stop)) {
        count = static_cast<size_t>((stop - start + step - (step > 0 ? 1 : -1)) / step);
        if (step < 0) // correction for negative step and integer division
            count = static_cast<size_t>((start - stop - 1) / (-step) + 1);
    }

    Array *result = Array::create(mm::autoSpace(), count);

    Int value = start;
    for (size_t i = 0; i < count; ++i, value += step)
        result->set(i, value);

    return toSlot(result);
}

slot_t __slice_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr = norm.get<Array *>(0);
    Int start  = with.get<Int>(0);
    Int end    = with.get<Int>(1);

    Int size = static_cast<Int>(arr->size());
    if (start < 0)
        start += size;
    if (end < 0)
        end += size;
    start = std::max<Int>(0, std::min(start, size));
    end   = std::max<Int>(0, std::min(end, size));
    if (end < start)
        end = start;

    size_t newSize = static_cast<size_t>(end - start);
    Array *res     = Array::create(mm::autoSpace(), newSize);

    for (size_t i = 0; i < newSize; ++i)
        res->set(i, arr->get<slot_t>(start + i));

    return toSlot(res);
}

slot_t __concat_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *lhs = norm.get<Array *>(0);
    Array *rhs = norm.get<Array *>(1);

    size_t n1 = lhs->size(), n2 = rhs->size();
    Array *res = Array::create(mm::autoSpace(), n1 + n2);

    for (size_t i = 0; i < n1; ++i)
        res->set(i, lhs->get<slot_t>(i));
    for (size_t i = 0; i < n2; ++i)
        res->set(n1 + i, rhs->get<slot_t>(i));

    return toSlot(res);
}

slot_t __append_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr    = norm.get<Array *>(0);
    slot_t elem   = with.get<slot_t>(0);
    Type *arrType = norm.type(0);
    arr->append(elem, tt::as_ptr<ArrayType>(arrType));
    return toSlot(arr);
}

slot_t __extend_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr    = norm.get<Array *>(0);
    Array *more   = with.get<Array *>(0);
    Type *arrType = norm.type(0);
    auto *at      = tt::as_ptr<ArrayType>(arrType);

    size_t n1 = arr->size(), n2 = more->size();
    arr->reserve(n1 + n2, at);

    for (size_t i = 0; i < n2; ++i)
        arr->append(more->get<slot_t>(i), at);

    return toSlot(arr);
}

slot_t __contains_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr            = norm.get<Array *>(0);
    slot_t item           = with.get<slot_t>(0);
    Type *arrType         = norm.type(0);
    auto *at              = tt::as_ptr<ArrayType>(arrType);
    TypeCode elemTypeCode = at->elemTypeCode();

    Bool found = false;
    size_t n   = arr->size();

    if (isGCTraced(elemTypeCode)) {
        Type *elemType = at->elemType();
        for (size_t i = 0; i < n; ++i) {
            if (arr->get<Object *>(i)->equals(fromSlot<Object *>(item), elemType, false)) {
                found = true;
                break;
            }
        }
    } else {
        for (size_t i = 0; i < n; ++i) {
            if (arr->get<slot_t>(i) == item) {
                found = true;
                break;
            }
        }
    }

    return toSlot(found);
}
