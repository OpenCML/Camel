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
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include <format>
#include "struct.h"
#include "ops.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/type.h"
#include "camel/utils/utf8.h"

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;
using namespace camel::core::type;

slot_t __len_str__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *s = norm.get<String *>(0);
    // Code points, not bytes (design principles 2.17).
    return rtdata::toSlot(static_cast<Int64>(camel::utf8::count(s->view())));
}

slot_t __len_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr = norm.get<Array *>(0);
    return rtdata::toSlot(static_cast<Int64>(arr->size()));
}

slot_t __zip__(ArgsView &with, ArgsView &norm, Context &ctx) {
    // Two or more arrays (the resolver refuses fewer); the result type is an array of tuples
    // with one element per array. With three arrays the tuples used to be built with two
    // elements under a type of three: the third array was dropped and reading `.2` read
    // past the tuple.
    const size_t arity = norm.size();
    std::vector<Array *> arrays;
    std::vector<Type *> elemTypes;
    arrays.reserve(arity);
    elemTypes.reserve(arity);
    for (size_t k = 0; k < arity; ++k) {
        arrays.push_back(norm.get<Array *>(k));
        elemTypes.push_back(tt::as_ptr<ArrayType>(norm.type(k))->elemType());
    }
    auto *tupleType  = TupleType::create(std::move(elemTypes));
    auto *resultType = ArrayType::create(tupleType);

    const size_t n = arrays[0]->size();
    for (size_t k = 1; k < arity; ++k) {
        if (arrays[k]->size() != n) {
            throwRuntimeFault(
                RuntimeDiag::InvalidArgument,
                "zip",
                arity == 2
                    ? std::format(
                          "the two sequences have {} and {} elements, and they must have the "
                          "same number",
                          n,
                          arrays[k]->size())
                    : std::format(
                          "sequences 1 and {} have {} and {} elements, and all must have the "
                          "same number",
                          k + 1,
                          n,
                          arrays[k]->size()));
        }
    }

    Array *result = Array::create(mm::autoSpace(), n);
    for (size_t i = 0; i < n; ++i) {
        Tuple *t = Tuple::create(arity, mm::autoSpace());
        for (size_t k = 0; k < arity; ++k) {
            t->set<slot_t>(k, arrays[k]->get<slot_t>(i), tupleType);
        }
        result->set<rtdata::Object *>(i, t, resultType);
    }
    return rtdata::toSlot(result);
}

slot_t __head_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr = norm.get<Array *>(0);
    if (!arr || arr->size() == 0) {
        // An empty array has no first element: the same fault as `xs[0]`. It used to return
        // a zero slot (0, 0.0, or a null object the caller then read as a value).
        throwRuntimeFault(RuntimeDiag::IndexOutOfRange, Int64{0}, "an array", size_t{0});
    }
    return arr->get<slot_t>(0);
}

slot_t __tail_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr    = norm.get<Array *>(0);
    auto *arrType = tt::as_ptr<ArrayType>(norm.type(0));

    size_t n = arr->size();
    if (n <= 1) {
        Array *empty = Array::create(mm::autoSpace(), 0);
        return rtdata::toSlot(empty);
    }

    Array *res = Array::create(mm::autoSpace(), n - 1);
    for (size_t i = 1; i < n; ++i) {
        res->set<slot_t>(i - 1, arr->get<slot_t>(i), arrType);
    }
    return toSlot(res);
}

slot_t __range__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 start = norm.get<Int64>(0);
    Int64 stop  = norm.get<Int64>(1);
    Int64 step  = (norm.size() == 3) ? norm.get<Int64>(2) : 1;

    if (step == 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "range",
            std::format("the step is 0 (from {} to {}); a range needs a step that moves", start, stop));
    }

    size_t count = 0;
    if ((step > 0 && start < stop) || (step < 0 && start > stop)) {
        count = static_cast<size_t>((stop - start + step - (step > 0 ? 1 : -1)) / step);
        if (step < 0) // correction for negative step and integer division
            count = static_cast<size_t>((start - stop - 1) / (-step) + 1);
    }

    Array *result = Array::create(mm::autoSpace(), count);

    Int64 value = start;
    for (size_t i = 0; i < count; ++i, value += step)
        result->set(i, value);

    return rtdata::toSlot(result);
}

slot_t __slice_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr    = norm.get<Array *>(0);
    auto *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    Int64 start   = with.get<Int64>(0);
    Int64 end     = with.get<Int64>(1);

    // A bound outside the array is a fault, as an index out of range is (design principles
    // 2.10): a bound is a position from 0 to the length, the end one past the last element
    // taken. It used to be cut to the array, and a negative bound counted from the end.
    // `take` and `drop` of the `array` module are the operations that cut by definition.
    const Int64 size = static_cast<Int64>(arr->size());
    if (start < 0 || start > size || end < 0 || end > size) {
        throwRuntimeFault(RuntimeDiag::SliceOutOfRange, start, end, "an array", arr->size());
    }
    // Both bounds inside the array and the end before the start is the empty range, not a
    // fault (design principles 2.11): `xs->slice<2, 1>` is `[]`.
    if (end < start)
        end = start;

    size_t newSize = static_cast<size_t>(end - start);
    Array *res     = Array::create(mm::autoSpace(), newSize);

    for (size_t i = 0; i < newSize; ++i)
        res->set<slot_t>(i, arr->get<slot_t>(start + i), arrType);

    return rtdata::toSlot(res);
}

slot_t __concat_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *lhs    = norm.get<Array *>(0);
    Array *rhs    = norm.get<Array *>(1);
    auto *arrType = tt::as_ptr<ArrayType>(norm.type(0));

    size_t n1 = lhs->size(), n2 = rhs->size();
    Array *res = Array::create(mm::autoSpace(), n1 + n2);

    for (size_t i = 0; i < n1; ++i)
        res->set<slot_t>(i, lhs->get<slot_t>(i), arrType);
    for (size_t i = 0; i < n2; ++i)
        res->set<slot_t>(n1 + i, rhs->get<slot_t>(i), arrType);

    return rtdata::toSlot(res);
}

namespace {
// A new array holding the elements of `arr` and room for `extra` more. The operators below
// return new arrays and leave their argument as it was (design principles 2.12): an array
// that a program still refers to never changes. The room makes the appends that follow
// cheap; it is not visible, only `size()` elements exist.
Array *copyWithRoom(const Array *arr, size_t extra, const ArrayType *arrType) {
    const size_t n = arr->size();
    Array *res     = Array::create(mm::autoSpace(), 0);
    res->reserve(n + extra, arrType);
    for (size_t i = 0; i < n; ++i) {
        res->append(arr->get<slot_t>(i), arrType);
    }
    return res;
}
} // namespace

// `xs->append<v>`: a new array, the elements of xs and then v. xs is unchanged.
slot_t __append_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const Array *arr  = norm.get<Array *>(0);
    const slot_t elem = with.get<slot_t>(0);
    auto *arrType     = tt::as_ptr<ArrayType>(norm.type(0));
    Array *res        = copyWithRoom(arr, 1, arrType);
    res->append(elem, arrType);
    return rtdata::toSlot(res);
}

// `xs->extend<ys>`: a new array, the elements of xs and then those of ys. xs is unchanged.
slot_t __extend_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const Array *arr  = norm.get<Array *>(0);
    const Array *more = with.get<Array *>(0);
    auto *arrType     = tt::as_ptr<ArrayType>(norm.type(0));
    const size_t n2   = more->size();
    Array *res        = copyWithRoom(arr, n2, arrType);
    for (size_t i = 0; i < n2; ++i) {
        res->append(more->get<slot_t>(i), arrType);
    }
    return rtdata::toSlot(res);
}

slot_t __contains_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    // "Equals" is the `==` of the element type: by content for arrays, tuples and structs,
    // as numbers for floats (design principles 2.10).
    Array *arr           = norm.get<Array *>(0);
    const slot_t item    = with.get<slot_t>(0);
    const Type *elemType = tt::as_ptr<ArrayType>(norm.type(0))->elemType();
    const size_t n       = arr->size();
    for (size_t i = 0; i < n; ++i) {
        if (valuesEqualByContent(arr->get<slot_t>(i), item, elemType)) {
            return rtdata::toSlot<Bool>(true);
        }
    }
    return rtdata::toSlot<Bool>(false);
}

// `'hello'->contains<'ell'>`: whether the bytes of `sub` occur in the string. The empty
// string occurs in every string.
slot_t __contains_str__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const String *text = norm.get<String *>(0);
    const String *sub  = with.get<String *>(0);
    return rtdata::toSlot<Bool>(text->view().find(sub->view()) != std::string_view::npos);
}
