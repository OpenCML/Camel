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
 * Updated: Dec. 23, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "struct.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"

void __len_str__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    String *s = frame.get<String *>(nargs[0]);
    frame.set(self, static_cast<Int>(s->size()));
}

void __len_arr__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    Array *arr = frame.get<Array *>(nargs[0]);
    frame.set(self, static_cast<Int>(arr->size()));
}

void __zip__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    Array *lhs = frame.get<Array *>(nargs[0]);
    Array *rhs = frame.get<Array *>(nargs[1]);

    size_t n = lhs->size();
    if (rhs->size() != n) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<zip> requires both sequences to have the same length");
        frame.set(self, NullSlot);
        return;
    }

    auto resArrayType = frame.typePtrAt<ArrayType>(self);
    auto resTupleType = tt::as_shared<TupleType>(resArrayType->elemType());

    Array *result = Array::create(resArrayType->layout(), mm::autoSpace(), n);
    for (size_t i = 0; i < n; ++i) {
        Tuple *t = Tuple::create(resTupleType->layout(), mm::autoSpace());
        t->set(0, lhs->get<Int>(i));
        t->set(1, rhs->get<Int>(i));
        result->set(i, t);
    }
    frame.set(self, result);
}

void __head_arr__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    Array *arr = frame.get<Array *>(nargs[0]);
    if (!arr || arr->size() == 0) {
        frame.set(self, NullSlot);
        return;
    }
    frame.set(self, arr->get<slot_t>(0));
}

void __tail_arr__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    Array *arr = frame.get<Array *>(nargs[0]);

    size_t n = arr->size();
    if (n <= 1) {
        Array *empty = Array::create(arr->layout(), mm::autoSpace());
        frame.set(self, empty);
        return;
    }

    Array *res = Array::create(arr->layout(), mm::autoSpace(), n - 1);
    for (size_t i = 1; i < n; ++i) {
        res->set(i - 1, arr->get<slot_t>(i));
    }
    frame.set(self, res);
}

void __range__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    Int start = frame.get<Int>(nargs[0]);
    Int stop  = frame.get<Int>(nargs[1]);
    Int step  = (nargs.size == 3) ? frame.get<Int>(nargs[2]) : 1;

    if (step == 0) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<range> step cannot be zero");
        frame.set(self, NullSlot);
        return;
    }

    size_t count = 0;
    if ((step > 0 && start < stop) || (step < 0 && start > stop)) {
        count = static_cast<size_t>((stop - start + step - (step > 0 ? 1 : -1)) / step);
        if (step < 0) // correction for negative step and integer division
            count = static_cast<size_t>((start - stop - 1) / (-step) + 1);
    }

    auto resArrayType = frame.typePtrAt<ArrayType>(self);

    Array *result = Array::create(resArrayType->layout(), mm::autoSpace(), count);

    Int value = start;
    for (size_t i = 0; i < count; ++i, value += step)
        result->set(i, value);

    frame.set(self, result);
}

void __slice_arr__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Array *arr = frame.get<Array *>(nargs[0]);
    Int start  = frame.get<Int>(wargs[0]);
    Int end    = frame.get<Int>(wargs[1]);

    Int size = static_cast<Int>(arr->size());
    if (start < 0)
        start += size;
    if (end < 0)
        end += size;
    start = std::max<Int>(0, std::min(start, size));
    end   = std::max<Int>(0, std::min(end, size));
    if (end < start)
        end = start;

    size_t newSize    = static_cast<size_t>(end - start);
    auto resArrayType = frame.typePtrAt<ArrayType>(self);
    Array *res        = Array::create(resArrayType->layout(), mm::autoSpace(), newSize);

    for (size_t i = 0; i < newSize; ++i)
        res->set(i, arr->get<slot_t>(start + i));

    frame.set(self, res);
}

void __concat_arr__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    Array *lhs = frame.get<Array *>(nargs[0]);
    Array *rhs = frame.get<Array *>(nargs[1]);

    size_t n1 = lhs->size(), n2 = rhs->size();
    auto resArrayType = frame.typePtrAt<ArrayType>(self);
    Array *res        = Array::create(resArrayType->layout(), mm::autoSpace(), n1 + n2);

    for (size_t i = 0; i < n1; ++i)
        res->set(i, lhs->get<slot_t>(i));
    for (size_t i = 0; i < n2; ++i)
        res->set(n1 + i, rhs->get<slot_t>(i));

    frame.set(self, res);
}

void __append_arr__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Array *arr  = frame.get<Array *>(nargs[0]);
    slot_t elem = frame.get<slot_t>(wargs[0]);
    arr->append(elem);
    frame.set(self, arr);
}

void __extend_arr__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Array *arr  = frame.get<Array *>(nargs[0]);
    Array *more = frame.get<Array *>(wargs[0]);

    size_t n1 = arr->size(), n2 = more->size();
    arr->reserve(n1 + n2);

    for (size_t i = 0; i < n2; ++i)
        arr->append(more->get<slot_t>(i));

    frame.set(self, arr);
}

void __contains_arr__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Array *arr  = frame.get<Array *>(nargs[0]);
    slot_t item = frame.get<slot_t>(wargs[0]);

    Bool found        = false;
    size_t n          = arr->size();
    TypeCode elemType = arr->elemType();

    if (isGCTraced(elemType)) {
        for (size_t i = 0; i < n; ++i) {
            if (arr->get<Object *>(i)->equals(fromSlot<Object *>(item))) {
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

    frame.set(self, found);
}
