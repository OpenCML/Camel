// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "struct.h"
#include "camel/core/context/context.h"
#include "camel/core/derivative.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/type.h"
#include "camel/utils/utf8.h"
#include "ops.h"
#include <format>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;
using namespace camel::core::type;

slot_t __len_str__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *s = norm.get<String *>(0);
    // Code points, not bytes (design principles 11.1).
    return rtdata::toSlot(static_cast<Int64>(camel::utf8::count(s->view())));
}

slot_t __iter_str__(ArgsView &with, ArgsView &norm, Context &ctx) {
    // The elements of a string are its code points, each one a one-character string, exactly
    // as `s[i]` gives them (design principles 11.1). `for c in s` takes them in order.
    String *s                   = norm.get<String *>(0);
    const std::string_view text = s != nullptr ? s->view() : std::string_view();
    auto *arrayType             = ArrayType::create(Type::String());
    Array *result               = Array::create(mm::autoSpace(), arrayType, camel::utf8::count(text));
    size_t at                   = 0;
    size_t index                = 0;
    while (at < text.size()) {
        const size_t start = at;
        (void)camel::utf8::decodeOne(text, at);
        result->set<String *>(
            index++,
            String::from(std::string(text.substr(start, at - start)), mm::autoSpace()),
            arrayType);
    }
    return rtdata::toSlot(result);
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
        Array *empty = Array::create(mm::autoSpace(), arrType, 0);
        return rtdata::toSlot(empty);
    }

    Array *res = Array::create(mm::autoSpace(), arrType, n - 1);
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
            std::format(
                "the step is 0 (from {} to {}); a range needs a step that moves",
                start,
                stop));
    }

    size_t count = 0;
    if ((step > 0 && start < stop) || (step < 0 && start > stop)) {
        count = static_cast<size_t>((stop - start + step - (step > 0 ? 1 : -1)) / step);
        if (step < 0) // correction for negative step and integer division
            count = static_cast<size_t>((start - stop - 1) / (-step) + 1);
    }

    auto *intArrayType = ArrayType::create(Type::Int64());
    Array *result      = Array::create(mm::autoSpace(), intArrayType, count);

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
    // 3.9): a bound is a position from 0 to the length, the end one past the last element
    // taken. It used to be cut to the array, and a negative bound counted from the end.
    // `take` and `drop` of the `array` module are the operations that cut by definition.
    const Int64 size = static_cast<Int64>(arr->size());
    if (start < 0 || start > size || end < 0 || end > size) {
        throwRuntimeFault(RuntimeDiag::SliceOutOfRange, start, end, "an array", arr->size());
    }
    // Both bounds inside the array and the end before the start is the empty range, not a
    // fault (design principles 3.9): `xs->slice<2, 1>` is `[]`.
    if (end < start)
        end = start;

    size_t newSize = static_cast<size_t>(end - start);
    Array *res     = Array::create(mm::autoSpace(), arrType, newSize);

    for (size_t i = 0; i < newSize; ++i)
        res->set<slot_t>(i, arr->get<slot_t>(start + i), arrType);

    return rtdata::toSlot(res);
}

// `t[a:b]`, `t[a:b:s]`, `t[:b]`, `t[a:]`, `t[:]`: the slice of the first dimension a subscript
// writes (design principles 5.4). The bounds follow the rules of the slice function (3.9): a
// position from 0 to the length, the end one past the last element taken, a negative bound a
// fault rather than a count from the end, and an end before the start the empty range. A
// negative step runs backwards: the start defaults to the last element and the end, left out,
// stands for the position before the first. The step and which of the start and the end were
// written come with the call as a two-bit mask (bit 0 end, bit 1 start).
//
// A step other than 1 is a view with strides, which needs the multidimensional array
// representation of H17. Until it lands, a stepped slice is a copy: the elements are the same
// and the program does not have to know, but the result does not share the array's block. H17
// records the deviation; step 1 still shares the block.
slot_t __slice_sub__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr    = norm.get<Array *>(0);
    auto *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    const Int64 startRaw = norm.get<Int64>(1);
    const Int64 endRaw   = norm.get<Int64>(2);
    const Int64 step     = norm.get<Int64>(3);
    const Int64 written  = norm.get<Int64>(4);
    const bool wroteEnd   = (written & 1) != 0;
    const bool wroteStart = (written & 2) != 0;

    const Int64 size = static_cast<Int64>(arr->size());
    if (step == 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument, "[]", "a slice step must not be 0");
    }
    Int64 start = 0;
    Int64 end   = 0;
    Int64 count = 0;
    if (step > 0) {
        start = wroteStart ? startRaw : 0;
        end   = wroteEnd ? endRaw : size;
        if (start < 0 || start > size || end < 0 || end > size) {
            throwRuntimeFault(
                RuntimeDiag::SliceOutOfRange, start, end, "an array", arr->size());
        }
        count = start < end ? (end - start + step - 1) / step : 0;
    } else {
        // A negative step starts at an element and stops after `end`; an omitted start is the
        // last element, an omitted end the position before the first (-1).
        start = wroteStart ? startRaw : size - 1;
        end   = wroteEnd ? endRaw : -1;
        if ((wroteStart && (start < 0 || start >= size)) ||
            (wroteEnd && (end < 0 || end > size))) {
            throwRuntimeFault(
                RuntimeDiag::SliceOutOfRange, start, end, "an array", arr->size());
        }
        count = start > end ? (start - end + (-step) - 1) / (-step) : 0;
    }
    if (arr->rank() != 1) {
        // A slice of a dimension other than the first is a view with strides (H17); the type
        // system states rank 1, so this is a defect rather than something a program writes.
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "[]",
            "a slice of an array of rank 1; a slice of another dimension is a view with "
            "strides, which needs the multidimensional array representation of H17");
    }
    if (step == 1 && arr->isCompact()) {
        // A slice is a view sharing the data block (design principles 5.4).
        return rtdata::toSlot(
            Array::view(arr, std::span<const int64_t>(&count, 1), static_cast<uint64_t>(start)));
    }
    // A slot array has no block to share and is copied (H17 records that for slot arrays); so
    // is a stepped slice.
    Array *res = Array::create(mm::autoSpace(), arrType, static_cast<size_t>(count));
    for (Int64 i = 0; i < count; ++i) {
        res->setSlot(
            static_cast<uint64_t>(i),
            arr->getSlot(static_cast<uint64_t>(start + i * step)),
            arrType);
    }
    return rtdata::toSlot(res);
}

// `t[idx]` with `idx` an int array: the rows the indices name, in that order (the `take` of
// design principles 5.4). An index outside the array, negative included, is a fault with the
// index as written; a repeated index repeats its row.
slot_t __idx_take_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr    = norm.get<Array *>(0);
    Array *idx    = norm.get<Array *>(1);
    auto *arrType = tt::as_ptr<ArrayType>(norm.type(0));

    const size_t n    = arr->size();
    const size_t take = idx->size();
    Array *res        = Array::create(mm::autoSpace(), arrType, take);
    for (size_t i = 0; i < take; ++i) {
        const Int64 raw = idx->get<Int64>(i);
        if (raw < 0 || static_cast<size_t>(raw) >= n) {
            throwRuntimeFault(RuntimeDiag::IndexOutOfRange, raw, "an array", n);
        }
        res->set<slot_t>(i, arr->get<slot_t>(static_cast<size_t>(raw)), arrType);
    }
    return rtdata::toSlot(res);
}

slot_t __last_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr     = norm.get<Array *>(0);
    const size_t n = arr->size();
    if (n == 0) {
        // No last element: the same fault as the index of an empty array would be, named with
        // the position that does not exist (design principles 5.4).
        throwRuntimeFault(RuntimeDiag::IndexOutOfRange, Int64{-1}, "an array", size_t{0});
    }
    return arr->get<slot_t>(n - 1);
}

slot_t __concat_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *lhs    = norm.get<Array *>(0);
    Array *rhs    = norm.get<Array *>(1);
    auto *arrType = tt::as_ptr<ArrayType>(norm.type(0));

    size_t n1 = lhs->size(), n2 = rhs->size();
    Array *res = Array::create(mm::autoSpace(), arrType, n1 + n2);

    for (size_t i = 0; i < n1; ++i)
        res->set<slot_t>(i, lhs->get<slot_t>(i), arrType);
    for (size_t i = 0; i < n2; ++i)
        res->set<slot_t>(n1 + i, rhs->get<slot_t>(i), arrType);

    return rtdata::toSlot(res);
}

namespace {
// A new array holding the elements of `arr` and room for `extra` more. The operators below
// return new arrays and leave their argument as it was (design principles 11.3): an array
// that a program still refers to never changes. The room makes the appends that follow
// cheap; it is not visible, only `size()` elements exist.
Array *copyWithRoom(const Array *arr, size_t extra, const ArrayType *arrType) {
    const size_t n = arr->size();
    Array *res     = Array::create(mm::autoSpace(), arrType, 0);
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

// A private copy of an array, made for one state of a loop whose body only appends to it
// (Builder::markLoopStateInPlace, which decides per state): the rounds then append to the copy
// in place, and the literal the program wrote stays the constant it is. Not a name a program can
// reach for.
slot_t __copy_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    const Array *arr = norm.get<Array *>(0);
    auto *arrType    = tt::as_ptr<ArrayType>(norm.type(0));
    return rtdata::toSlot(copyWithRoom(arr, 0, arrType));
}

// `xs->append<v>` on a value the compiler proved no one else can see (the loop-state rewrite,
// Builder::markLoopStateInPlace): the same call, but the array may take the element in place
// when it owns its storage (design principles 10.5). When it cannot - an array of references,
// a view, a block someone else holds - it copies exactly like `append`, so the result is the
// same either way.
slot_t __append_owned_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr        = norm.get<Array *>(0);
    const slot_t elem = with.get<slot_t>(0);
    auto *arrType     = tt::as_ptr<ArrayType>(norm.type(0));
    if (!arr->canWriteInPlace()) {
        return __append_arr__(with, norm, ctx);
    }
    arr->append(elem, arrType);
    return rtdata::toSlot(arr);
}

// The in-place form of `append` (OperatorTable::setInPlaceForm): the array is the one
// :var/take handed the call, which no one else refers to, so it may take the element in place.
// When its storage cannot be written in place (an array of references, a view, a block
// someone else holds) it copies exactly like `append`; the result is the same either way.
slot_t __append_in_place_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return __append_owned_arr__(with, norm, ctx);
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
    // as numbers for floats (design principles 4.2).
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

// ------------------------------------------------------------------- tangent arrays
//
// The tangent of an array is the array of the tangents of its elements (design principles
// 9.9): `T[]` has the tangent `tangent(T)[]`, and an array of a type without a tangent has
// none at all. These three operators are how a gradient graph adds two such arrays, makes a
// zero one shaped like a primal array, and adds one element's gradient at an index (the
// reverse of `xs[i]`, a scatter-add). They are structural: they recurse on the tangent type,
// so an array of structs, tuples or arrays works as well as an array of floats. A tangent
// type owned by a module (a tensor) has no structural definition here yet; that is H17.

namespace {

using type::ArrayType;
using type::StructType;
using type::TupleType;

[[noreturn]] void noStructuralTangent(std::string_view what, Type *type) {
    throwRuntimeFault(
        RuntimeDiag::RuntimeError,
        std::format("autodiff: {} is not defined for tangent type '{}'", what, type->toString()));
}

// The zero tangent of a value of primal type `primalType`, whose tangent type is
// `tangentType`. `primal` is the primal value: an array's zero has the array's length, and a
// struct or tuple's fields line up with the primal's, so the shape is taken from it.
slot_t zeroTangent(Type *primalType, Type *tangentType, slot_t primal) {
    switch (tangentType->code()) {
    case TypeCode::Float64:
        return toSlot<Float64>(0.0);
    case TypeCode::Float32:
        return toSlot<Float32>(0.0f);
    case TypeCode::Struct: {
        auto *tangentStruct = tt::as_ptr<StructType>(tangentType);
        auto *primalStruct  = tt::as_ptr<StructType>(primalType);
        auto *value         = fromSlot<Struct *>(primal);
        Struct *zero        = Struct::create(tangentStruct->size(), mm::autoSpace());
        size_t position     = 0;
        for (size_t i = 0; i < primalStruct->size(); ++i) {
            Type *fieldTangent = camel::core::tangentTypeOf(primalStruct->typeAt(i));
            if (fieldTangent == nullptr) {
                continue;
            }
            zero->set<slot_t>(
                position,
                zeroTangent(primalStruct->typeAt(i), fieldTangent, value->get<slot_t>(i)),
                tangentStruct);
            ++position;
        }
        return rtdata::toSlot(zero);
    }
    case TypeCode::Tuple: {
        auto *tangentTuple = tt::as_ptr<TupleType>(tangentType);
        auto *primalTuple  = tt::as_ptr<TupleType>(primalType);
        auto *value        = fromSlot<Tuple *>(primal);
        Tuple *zero        = Tuple::create(tangentTuple->size(), mm::autoSpace());
        size_t position    = 0;
        for (size_t i = 0; i < primalTuple->size(); ++i) {
            Type *elementTangent = camel::core::tangentTypeOf(primalTuple->typeAt(i));
            if (elementTangent == nullptr) {
                continue;
            }
            zero->set<slot_t>(
                position,
                zeroTangent(primalTuple->typeAt(i), elementTangent, value->get<slot_t>(i)),
                tangentTuple);
            ++position;
        }
        return rtdata::toSlot(zero);
    }
    case TypeCode::Array: {
        auto *tangentArray = tt::as_ptr<ArrayType>(tangentType);
        auto *primalArray  = tt::as_ptr<ArrayType>(primalType);
        auto *value        = fromSlot<Array *>(primal);
        Array *zero        = Array::create(mm::autoSpace(), value->size());
        for (size_t i = 0; i < value->size(); ++i) {
            zero->set<slot_t>(
                i,
                zeroTangent(
                    primalArray->elemType(), tangentArray->elemType(),
                    value->get<slot_t>(i)),
                tangentArray);
        }
        return rtdata::toSlot(zero);
    }
    default:
        noStructuralTangent("a zero tangent", tangentType);
    }
}

// The sum of two tangents of `tangentType`. Both have the same shape.
slot_t addTangent(Type *tangentType, slot_t lhsSlot, slot_t rhsSlot) {
    switch (tangentType->code()) {
    case TypeCode::Float64:
        return toSlot<Float64>(fromSlot<Float64>(lhsSlot) + fromSlot<Float64>(rhsSlot));
    case TypeCode::Float32:
        return toSlot<Float32>(fromSlot<Float32>(lhsSlot) + fromSlot<Float32>(rhsSlot));
    case TypeCode::Struct: {
        auto *structType = tt::as_ptr<StructType>(tangentType);
        auto *lhs        = fromSlot<Struct *>(lhsSlot);
        auto *rhs        = fromSlot<Struct *>(rhsSlot);
        ASSERT(
            lhs->size() == structType->size() && rhs->size() == structType->size(),
            "autodiff: tangent structs of different shapes.");
        Struct *sum = Struct::create(structType->size(), mm::autoSpace());
        for (size_t i = 0; i < structType->size(); ++i) {
            sum->set<slot_t>(
                i,
                addTangent(structType->typeAt(i), lhs->get<slot_t>(i), rhs->get<slot_t>(i)),
                structType);
        }
        return rtdata::toSlot(sum);
    }
    case TypeCode::Tuple: {
        auto *tupleType = tt::as_ptr<TupleType>(tangentType);
        auto *lhs       = fromSlot<Tuple *>(lhsSlot);
        auto *rhs       = fromSlot<Tuple *>(rhsSlot);
        ASSERT(
            lhs->size() == tupleType->size() && rhs->size() == tupleType->size(),
            "autodiff: tangent tuples of different shapes.");
        Tuple *sum = Tuple::create(tupleType->size(), mm::autoSpace());
        for (size_t i = 0; i < tupleType->size(); ++i) {
            sum->set<slot_t>(
                i,
                addTangent(tupleType->typeAt(i), lhs->get<slot_t>(i), rhs->get<slot_t>(i)),
                tupleType);
        }
        return rtdata::toSlot(sum);
    }
    case TypeCode::Array: {
        auto *arrayType = tt::as_ptr<ArrayType>(tangentType);
        auto *lhs       = fromSlot<Array *>(lhsSlot);
        auto *rhs       = fromSlot<Array *>(rhsSlot);
        ASSERT(lhs->size() == rhs->size(), "autodiff: tangent arrays of different lengths.");
        Array *sum = Array::create(mm::autoSpace(), lhs->size());
        for (size_t i = 0; i < lhs->size(); ++i) {
            sum->set<slot_t>(
                i,
                addTangent(arrayType->elemType(), lhs->get<slot_t>(i), rhs->get<slot_t>(i)),
                arrayType);
        }
        return rtdata::toSlot(sum);
    }
    default:
        noStructuralTangent("a sum of tangents", tangentType);
    }
}

} // namespace

// `tangent/add_arr(lhs, rhs)`: the elementwise sum of two tangent arrays.
slot_t __tangent_add_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    auto *arrayType = tt::as_ptr<ArrayType>(norm.type(0));
    auto *lhs       = norm.get<Array *>(0);
    auto *rhs       = norm.get<Array *>(1);
    if (lhs->size() != rhs->size()) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "autodiff: tangent arrays added that have different lengths");
    }
    Array *sum = Array::create(mm::autoSpace(), lhs->size());
    for (size_t i = 0; i < lhs->size(); ++i) {
        sum->set<slot_t>(
            i,
            addTangent(arrayType->elemType(), lhs->get<slot_t>(i), rhs->get<slot_t>(i)),
            arrayType);
    }
    return rtdata::toSlot(sum);
}

// `tangent/zeros_arr(xs)`: the zero tangent of `xs`, an array of its length whose elements are
// the zero tangents of the elements of `xs`.
slot_t __tangent_zeros_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    Type *primalType  = norm.type(0);
    Type *tangentType = norm.resultType();
    if (tangentType == nullptr) {
        tangentType = camel::core::tangentTypeOf(primalType);
    }
    if (tangentType == nullptr || primalType->code() != TypeCode::Array) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "autodiff: a zero tangent array needs an array with a tangent");
    }
    auto *primalArray  = tt::as_ptr<ArrayType>(primalType);
    auto *tangentArray = tt::as_ptr<ArrayType>(tangentType);
    auto *value        = norm.get<Array *>(0);
    Array *zero        = Array::create(mm::autoSpace(), value->size());
    for (size_t i = 0; i < value->size(); ++i) {
        zero->set<slot_t>(
            i,
            zeroTangent(primalArray->elemType(), tangentArray->elemType(), value->get<slot_t>(i)),
            tangentArray);
    }
    return rtdata::toSlot(zero);
}

// `tangent/scatter_add_arr(dx, i, dy)`: `dx` with `dy` added at index `i`. The reverse of
// `xs[i]`: the gradient of one element goes back to its place in the array's gradient.
slot_t __tangent_scatter_add_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    auto *arrayType   = tt::as_ptr<ArrayType>(norm.type(0));
    auto *base        = norm.get<Array *>(0);
    const Int64 index = norm.get<Int64>(1);
    const slot_t delta = norm.slot(2);
    if (index < 0 || static_cast<size_t>(index) >= base->size()) {
        throwRuntimeFault(RuntimeDiag::IndexOutOfRange, index, "an array", base->size());
    }
    Array *result = Array::create(mm::autoSpace(), base->size());
    for (size_t i = 0; i < base->size(); ++i) {
        result->set<slot_t>(i, base->get<slot_t>(i), arrayType);
    }
    const size_t at = static_cast<size_t>(index);
    result->set<slot_t>(
        at, addTangent(arrayType->elemType(), base->get<slot_t>(at), delta), arrayType);
    return rtdata::toSlot(result);
}

// `tangent/init_arr(dy)` and `tangent/last_arr(dy)`: the two halves the reverse of `append`
// needs. Appending `v` to `xs` makes `y`; the gradient of `y` splits into the gradients of `xs`
// (all but the last element) and of `v` (the last element).
slot_t __tangent_init_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    auto *arrayType = tt::as_ptr<ArrayType>(norm.type(0));
    auto *dy        = norm.get<Array *>(0);
    if (dy->size() == 0) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "autodiff: the gradient of an appended array has no element to drop");
    }
    Array *init = Array::create(mm::autoSpace(), dy->size() - 1);
    for (size_t i = 0; i + 1 < dy->size(); ++i) {
        init->set<slot_t>(i, dy->get<slot_t>(i), arrayType);
    }
    return rtdata::toSlot(init);
}

slot_t __tangent_last_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    auto *dy = norm.get<Array *>(0);
    if (dy->size() == 0) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "autodiff: the gradient of an appended array has no last element");
    }
    return dy->get<slot_t>(dy->size() - 1);
}
