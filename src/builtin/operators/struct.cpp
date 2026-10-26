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
#include "camel/execute/executor.h"
#include "camel/profile/profile.h"
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

namespace {

// The positions a slice takes along the one axis it names. The bounds follow the rules of the
// slice function (design principles 3.9 and 5.4): a position from 0 to the extent, the end one
// past the last element taken, a negative bound a fault rather than a count from the end (`last(t)`
// names the last element, 5.4: "不支持负数下标（负数是故障），最后一个元素用 last(t)"), and an end
// before the start the empty range. A negative step runs backwards: the start defaults to the last
// position and the end, left out, stands for the position before the first. Any non-zero step is
// accepted; the constant 0 is refused where it is written (the static check in
// `src/builtin/modules/globals.cpp`) and is a fault when only the run knows it.
//
// The step and which of the start and the end the program wrote come with the call as a two-bit
// mask (bit 0 end, bit 1 start), which is exactly the lowering the subscript writes
// (`src/compile/gct/builder.cpp`: `intConstant((written[1] ? 1 : 0) | (written[0] ? 2 : 0))`). The
// two spellings of a slice -- the subscript `t[a:b]` and the call `slice(t, axis, a, b, s)` -- are
// one node with one parameter table, and the function form fills the mask "both written" (3),
// which is the value that same lowering gives a subscript that writes both bounds.
//
// The forward slice and the range scatter of its gradient both read the range here, so the
// adjoint puts each element's gradient back where the forward took the element.
struct SliceRange {
    Int64 start = 0; // the first position taken
    Int64 count = 0; // how many positions the slice takes
    Int64 step  = 1; // the distance between two positions taken
};

SliceRange sliceRange(Int64 extent, Int64 startRaw, Int64 endRaw, Int64 step, Int64 written) {
    const bool wroteEnd   = (written & 1) != 0;
    const bool wroteStart = (written & 2) != 0;
    if (step == 0) {
        throwRuntimeFault(RuntimeDiag::InvalidArgument, "[]", "a slice step must not be 0");
    }
    SliceRange range;
    range.step = step;
    if (step > 0) {
        range.start = wroteStart ? startRaw : 0;
        const Int64 end = wroteEnd ? endRaw : extent;
        if (range.start < 0 || range.start > extent || end < 0 || end > extent) {
            throwRuntimeFault(
                RuntimeDiag::SliceOutOfRange, range.start, end, "an array", extent);
        }
        range.count = range.start < end ? (end - range.start + step - 1) / step : 0;
        return range;
    }
    range.start = wroteStart ? startRaw : extent - 1;
    const Int64 end = wroteEnd ? endRaw : -1;
    if ((wroteStart && (range.start < 0 || range.start >= extent)) ||
        (wroteEnd && (end < 0 || end > extent))) {
        throwRuntimeFault(RuntimeDiag::SliceOutOfRange, range.start, end, "an array", extent);
    }
    range.count = range.start > end ? (range.start - end + (-step) - 1) / (-step) : 0;
    return range;
}

/// The axis a slice names, read as the position it is: an axis counts from the end when it is
/// negative, `-1` being the last one. A value does not have the axis is a fault.
Int64 sliceAxis(const Array *arr, Int64 axisArg) {
    const auto rank = static_cast<Int64>(arr->rank());
    const Int64 axis = axisArg < 0 ? axisArg + rank : axisArg;
    if (axis < 0 || axis >= rank) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "[]",
            std::format("a slice axis must be between 0 and {}, not {}", rank - 1, axisArg));
    }
    return axis;
}

/// The flat index in the parent of the element at flat index `flat` of a slice of `axis`: every
/// coordinate but the sliced one is the coordinate the element has in the slice, and the sliced one
/// is the position the range takes there. The parent's row-major strides are computed from its own
/// extents, so a parent whose element type is not pure data (a slot array) maps by the same
/// arithmetic as a compact one.
Int64 parentIndexOfSlice(
    Int64 flat,
    const std::vector<int64_t> &sliceExtents,
    const std::vector<int64_t> &parentStrides,
    size_t axis,
    const SliceRange &range) {
    Int64 rest   = flat;
    Int64 parent = 0;
    for (size_t k = sliceExtents.size(); k-- > 0;) {
        const Int64 extent = sliceExtents[k];
        const Int64 coord  = extent != 0 ? rest % extent : 0;
        if (extent != 0) {
            rest /= extent;
        }
        parent += (k == axis ? range.start + coord * range.step : coord) * parentStrides[k];
    }
    return parent;
}

/// The row-major strides, in elements, of a value with these extents.
std::vector<int64_t> rowMajorStrides(const std::vector<int64_t> &extents) {
    std::vector<int64_t> strides(extents.size(), 1);
    for (size_t k = extents.size(); k-- > 1;) {
        strides[k - 1] = strides[k] * extents[k];
    }
    return strides;
}

/// The extents of the value a slice of `arr` along `axis` has: the extent of the sliced axis is
/// how many positions the range takes, and every other extent is the one it had.
std::vector<int64_t> slicedExtents(const Array *arr, size_t axis, const SliceRange &range) {
    std::vector<int64_t> extents = arr->shapeVector();
    extents[axis]                = range.count;
    return extents;
}

/// How many elements a value with these extents has.
uint64_t numelOf(const std::vector<int64_t> &extents) {
    uint64_t total = 1;
    for (int64_t extent : extents) {
        total *= static_cast<uint64_t>(extent);
    }
    return total;
}

/// Put `dx`'s elements at the positions `result` holds for the slice: the element at flat index
/// `i` of the slice goes to the flat index the parent's own coordinates give it. `result` is
/// already filled with the zeros of the positions the slice does not take.
void scatterSliceGradient(
    Array *result,
    const Array *dx,
    const std::vector<int64_t> &sliceExtents,
    const std::vector<int64_t> &parentExtents,
    size_t axis,
    const SliceRange &range,
    const ArrayType *tangentType) {
    const std::vector<int64_t> strides = rowMajorStrides(parentExtents);
    const uint64_t count               = numelOf(sliceExtents);
    for (uint64_t i = 0; i < count; ++i) {
        const Int64 at = parentIndexOfSlice(
            static_cast<Int64>(i), sliceExtents, strides, axis, range);
        result->set<slot_t>(
            static_cast<uint64_t>(at), dx->get<slot_t>(i), tangentType);
    }
}

} // namespace

// `t[a:b]`, `t[a:b:s]`, `t[:b]`, `t[a:]`, `t[:]` and `slice(t, axis, a, b, s)`: one slice node,
// whichever of the two spellings wrote it (design principles 5.4). The six words are the value,
// the axis, the two bounds, the step and the mask that says which of the bounds the program wrote;
// the argument count no longer tells the two spellings apart.
//
// The slice holds for any rank and along any axis: the elements are the positions the range takes
// along the axis named, with every other coordinate left as it is. A step-1 slice of the first
// axis of a compact array is a run of the block, so it is a view sharing it (5.4). A slice of
// another axis, or a stepped one, has no such run: it is a copy whose elements are the same, which
// is the deviation the flip line records for a stepped slice and records here for another axis
// (that a slice shares the block is what the multidimensional array representation will
// make true of every slice).
slot_t __slice_sub__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    Array *arr    = norm.get<Array *>(0);
    auto *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    // The step and the mask are the last two words; a call that left them out stands for step 1
    // and "both bounds written" (3), which is the value the declaration defaults them to and the
    // value the subscript's own lowering gives a subscript that writes both bounds. The argument
    // count still tells the two spellings apart until the declaration's defaults reach the call
    // (see the report).
    const size_t words  = norm.size();
    const Int64 step    = words >= 5 ? norm.get<Int64>(4) : 1;
    const Int64 written = words >= 6 ? norm.get<Int64>(5) : 3;
    const Int64 axis  = sliceAxis(arr, norm.get<Int64>(1));
    const auto at     = static_cast<size_t>(axis);
    const SliceRange range = sliceRange(
        arr->dim(at),
        norm.get<Int64>(2),
        norm.get<Int64>(3),
        step,
        written);
    std::vector<int64_t> extents = slicedExtents(arr, at, range);
    if (axis == 0 && range.step == 1 && arr->isCompact()) {
        // A slice is a view sharing the data block (design principles 5.4).
        return rtdata::toSlot(Array::view(
            arr, extents, static_cast<uint64_t>(range.start * arr->step(0))));
    }
    // A slot array has no block to share and is copied (the deviation recorded for slot arrays);
    // so is a stepped slice, and a slice of an axis other than the first.
    Array *res = Array::create(mm::autoSpace(), arrType, extents);
    const std::vector<int64_t> strides = rowMajorStrides(arr->shapeVector());
    const uint64_t count               = res->numel();
    for (uint64_t i = 0; i < count; ++i) {
        const Int64 src = parentIndexOfSlice(
            static_cast<Int64>(i), extents, strides, at, range);
        res->set<slot_t>(i, arr->get<slot_t>(static_cast<uint64_t>(src)), arrType);
    }
    return rtdata::toSlot(res);
}

namespace {

/// The type a shape-changing result is built with: the declared result type when it states an
/// element type, and otherwise the element type the argument's value holds (an array literal
/// whose element type nobody fixed yet). Only the element type is read from it; the extents of
/// the value are the ones computed where the result is made.
ArrayType *
resultArrayType(Type *declared, const Array *values, std::span<const int64_t> extents) {
    auto *declaredArray = declared != nullptr ? tt::as_ptr<ArrayType>(declared) : nullptr;
    const bool stated   = declaredArray != nullptr && declaredArray->elemType() != nullptr &&
                        declaredArray->elemType()->code() != TypeCode::Void;
    if (stated) {
        return declaredArray;
    }
    return ArrayType::create(const_cast<Type *>(values->elemType()), extents);
}

} // namespace

// `t[idx]` with `idx` an int array: the rows the indices name, in that order, along the first
// dimension (the `take` of design principles 5.4). The result's shape is
// `[len(idx)] + shape(t)[1:]` -- for a rank-1 array the row is the element -- and it is a copy:
// an arbitrary row order shares no block with the array it was taken from. An index outside the
// first dimension, negative included, is a fault with the index as written; a repeated index
// repeats its row, and its gradient accumulates there (design principles 9.9).
slot_t __idx_take_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    auto *const arr   = norm.get<Array *>(0);
    auto *const idx   = norm.get<Array *>(1);
    const size_t rank = arr->rank();
    if (rank < 1) {
        // A rank-0 array has no first dimension to take a row from. The type system states a
        // rank of at least one for this operator, so this is a defect rather than something a
        // program writes.
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument, "[]", "a subscript of an array of rank 0");
    }
    const uint64_t rows    = static_cast<uint64_t>(arr->dim(0));
    const uint64_t rowSize = rows == 0 ? 0 : arr->numel() / rows;
    const size_t take      = static_cast<size_t>(idx->numel());
    std::vector<int64_t> extents;
    extents.reserve(rank);
    extents.push_back(static_cast<int64_t>(take));
    for (size_t i = 1; i < rank; ++i) {
        extents.push_back(arr->dim(i));
    }
    auto *const resType = resultArrayType(norm.resultType(), arr, extents);
    Array *res          = Array::create(mm::autoSpace(), resType, extents);
    for (size_t k = 0; k < take; ++k) {
        const Int64 raw = idx->get<Int64>(k);
        if (raw < 0 || static_cast<uint64_t>(raw) >= rows) {
            // A rank-1 array is read whole, so the length is the array's; a deeper one is read
            // one row at a time, and the length the index must stay under is the first
            // dimension's.
            throwRuntimeFault(
                RuntimeDiag::IndexOutOfRange,
                raw,
                rank == 1 ? "an array" : "the first dimension of the input array",
                rows);
        }
        const uint64_t src = static_cast<uint64_t>(raw) * rowSize;
        for (uint64_t j = 0; j < rowSize; ++j) {
            res->setSlot(
                static_cast<uint64_t>(k) * rowSize + j, arr->getSlot(src + j), resType);
        }
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
    const bool ownStorage = arr->canWriteInPlace();
    if (camel::profile::enabledInline()) {
        // The array's own count answered, where the plan could not (docs/design/arrays-memory/README.cn.md 4.3.1).
        camel::profile::arrayCountAnswer(ownStorage);
    }
    if (!ownStorage) {
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
// reverse of `xs[i]`, a scatter-add). They recurse on the tangent type, so an array of
// structs, tuples or arrays works as well as an array of floats. A tangent type owned by a
// module (a tensor) is not defined here: the registry says how to make its zero and how to
// add two, and those are operators the module published, reached by URI.

namespace {

using type::ArrayType;
using type::StructType;
using type::TupleType;

[[noreturn]] void noStructuralTangent(std::string_view what, Type *type) {
    throwRuntimeFault(
        RuntimeDiag::RuntimeError,
        std::format("autodiff: {} is not defined for tangent type '{}'", what, type->toString()));
}

/// Arguments of a call to another operator, made of slots and their types. A tangent type owned
/// by a module is handled by that module's own operator (`zeros_like`, `add`), so the builtin
/// module never has to know which module owns a type.
class SlotArgs final : public ArgsView {
  public:
    SlotArgs(std::vector<slot_t> slots, std::vector<Type *> types)
        : slots_(std::move(slots)), types_(std::move(types)) {}
    size_t size() const override { return slots_.size(); }
    slot_t slot(size_t index) const override { return slots_[index]; }
    void setSlot(size_t index, slot_t value) override { slots_[index] = value; }
    TypeCode code(size_t index) const override { return types_[index]->code(); }
    Type *type(size_t index) const override { return types_[index]; }

  private:
    std::vector<slot_t> slots_;
    std::vector<Type *> types_;
};

/// Calls the operator `uri` on `slots`: the tangent-space operator of a module-owned type.
slot_t callTangentOperator(
    Context &ctx, std::string_view uri, Type *resultType, std::vector<slot_t> slots,
    std::vector<Type *> types) {
    const auto op = ctx.execMgr().find(std::string(uri));
    if (!op) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::format(
                "autodiff: the operator '{}' a tangent space names has no implementation", uri));
    }
    SlotArgs norm(std::move(slots), std::move(types));
    SlotArgs with({}, {});
    norm.setResultType(resultType);
    with.setResultType(resultType);
    return (*op)(with, norm, ctx);
}

/// True when a module owns the tangent space of `type`. Two kinds share the array code: a
/// tensor is a numeric array and the module that owns it published its own `add` and
/// `zeros_like`, while an array of values is answered here. A space a module registered for a
/// subset of a code is the one that owns those values.
bool isModuleOwned(Type *type) {
    const auto *space = camel::core::DerivativeRegistry::instance().findTangentSpace(type);
    return space != nullptr && space->owns != nullptr;
}

/// The zero tangent of a value whose tangent type a module owns: the module says how to make it
/// (`zeros_like`, `add`), and the registry passes that operator's URI on. An array whose element
/// type states no tangent of its own (an array of `Void`, the empty array literal's type) has
/// nothing to descend into, and `tensor:zeros_like` reads the element type the value actually has
/// (the element type follows the value).
slot_t moduleZeroTangent(Context &ctx, Type *primalType, Type *tangentType, slot_t primal);
// The zero tangent of a value of primal type `primalType`, whose tangent type is
// `tangentType`. `primal` is the primal value: an array's zero has the array's length, and a
// struct or tuple's fields line up with the primal's, so the shape is taken from it.
//
// A type a module owns (a tensor, a numeric array) is not built here at all: its zero is the
// module's own operator (`tensor:zeros_like`), which answers at the value's dtype and shape.
// Recursing into a tensor's element type would take the numbers out of the tensor's arithmetic
// -- and an array of `Void` has no element type to recurse into, which is the "zero tangent is
// not defined for tangent type 'void'" fault.
slot_t zeroTangent(Context &ctx, Type *primalType, Type *tangentType, slot_t primal) {
    if (isModuleOwned(primalType)) {
        return moduleZeroTangent(ctx, primalType, tangentType, primal);
    }
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
            Type *fieldTangent = camel::core::machineryTangentTypeOf(primalStruct->typeAt(i));
            if (fieldTangent == nullptr) {
                continue;
            }
            zero->set<slot_t>(
                position,
                zeroTangent(ctx, primalStruct->typeAt(i), fieldTangent, value->get<slot_t>(i)),
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
            Type *elementTangent = camel::core::machineryTangentTypeOf(primalTuple->typeAt(i));
            if (elementTangent == nullptr) {
                continue;
            }
            zero->set<slot_t>(
                position,
                zeroTangent(ctx, primalTuple->typeAt(i), elementTangent, value->get<slot_t>(i)),
                tangentTuple);
            ++position;
        }
        return rtdata::toSlot(zero);
    }
    case TypeCode::Array: {
        auto *tangentArray = tt::as_ptr<ArrayType>(tangentType);
        auto *primalArray  = tt::as_ptr<ArrayType>(primalType);
        // An array whose element type is `Void` is an array whose element type nobody stated (the
        // empty array literal's type): `Void` is the "a numeric scalar, which one is not fixed"
        // marker, not a type whose tangent exists, so there is no element to take the zero of. The
        // zero of such a value is its kind's own zero, made from the value
        // (`tensor:zeros_like`), and the element type follows the value
        // elements element by element is not the
        // answer, and doing it refused with "a zero tangent is not defined for tangent type
        // 'void'" although the value it is the zero of is a tensor.
        const bool elementsUnstated = tangentArray->elemType() != nullptr &&
                                      tangentArray->elemType()->code() == TypeCode::Void;
        if (elementsUnstated) {
            return moduleZeroTangent(ctx, primalType, tangentType, primal);
        }
        auto *value = fromSlot<Array *>(primal);
        Array *zero = Array::create(mm::autoSpace(), value->size());
        for (size_t i = 0; i < value->size(); ++i) {
            zero->set<slot_t>(
                i,
                zeroTangent(
                    ctx, primalArray->elemType(), tangentArray->elemType(),
                    value->get<slot_t>(i)),
                tangentArray);
        }
        return rtdata::toSlot(zero);
    }
    default:
        break;
    }
    // A leaf whose tangent type a module owns (a tensor): the module says how to make the zero
    // of a value of its type, and the registry passes that operator's URI on.
    return moduleZeroTangent(ctx, primalType, tangentType, primal);
}

// The zero tangent of a value whose tangent space a module owns: the module published the
// operator, the registry passes its URI on.
slot_t moduleZeroTangent(Context &ctx, Type *primalType, Type *tangentType, slot_t primal) {
    const auto *space = camel::core::DerivativeRegistry::instance().findTangentSpace(primalType);
    if (space == nullptr) {
        noStructuralTangent("a zero tangent", tangentType);
    }
    if (space->zero) {
        return *space->zero;
    }
    if (space->zerosLikeUri.empty()) {
        noStructuralTangent("a zero tangent", tangentType);
    }
    return callTangentOperator(
        ctx, space->zerosLikeUri, tangentType, {primal}, {primalType});
}

// The sum of two tangents of a type whose tangent space a module owns: the module published the
// operator, the registry passes its URI on.
slot_t moduleAddTangent(Context &ctx, Type *tangentType, slot_t lhsSlot, slot_t rhsSlot);

// The sum of two tangents of `tangentType`. Both have the same shape.
//
// As for the zero, a tangent type a module owns (a tensor) is summed by the module's own
// `add`, not element by element here: a tensor's tangent is the tensor, and adding two of them
// is `tensor:add`, which answers at the tensor's dtype and shape.
slot_t addTangent(Context &ctx, Type *tangentType, slot_t lhsSlot, slot_t rhsSlot) {
    if (isModuleOwned(tangentType)) {
        return moduleAddTangent(ctx, tangentType, lhsSlot, rhsSlot);
    }
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
                addTangent(ctx, structType->typeAt(i), lhs->get<slot_t>(i), rhs->get<slot_t>(i)),
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
                addTangent(ctx, tupleType->typeAt(i), lhs->get<slot_t>(i), rhs->get<slot_t>(i)),
                tupleType);
        }
        return rtdata::toSlot(sum);
    }
    case TypeCode::Array: {
        auto *arrayType = tt::as_ptr<ArrayType>(tangentType);
        // As in zeroTangent: an array whose element type is the `Void` marker has no element type
        // to add along, so its two tangents are added by the kind's own operator (`tensor:add`),
        // which reads the element type the values actually have.
        if (arrayType->elemType() != nullptr &&
            arrayType->elemType()->code() == TypeCode::Void) {
            const auto *space =
                camel::core::DerivativeRegistry::instance().findTangentSpace(tangentType);
            if (space == nullptr || space->addUri.empty()) {
                noStructuralTangent("a sum of tangents", tangentType);
            }
            return callTangentOperator(
                ctx, space->addUri, tangentType, {lhsSlot, rhsSlot}, {tangentType, tangentType});
        }
        auto *lhs       = fromSlot<Array *>(lhsSlot);
        auto *rhs       = fromSlot<Array *>(rhsSlot);
        ASSERT(lhs->size() == rhs->size(), "autodiff: tangent arrays of different lengths.");
        Array *sum = Array::create(mm::autoSpace(), lhs->size());
        for (size_t i = 0; i < lhs->size(); ++i) {
            sum->set<slot_t>(
                i,
                addTangent(ctx, arrayType->elemType(), lhs->get<slot_t>(i), rhs->get<slot_t>(i)),
                arrayType);
        }
        return rtdata::toSlot(sum);
    }
    default:
        break;
    }
    // A leaf whose tangent type a module owns (a tensor): the module published the operator that
    // adds two of them, and the registry passes its URI on.
    return moduleAddTangent(ctx, tangentType, lhsSlot, rhsSlot);
}

// The sum of two tangents of a type whose tangent space a module owns: the module published the
// operator, the registry passes its URI on.
slot_t moduleAddTangent(Context &ctx, Type *tangentType, slot_t lhsSlot, slot_t rhsSlot) {
    const auto *space = camel::core::DerivativeRegistry::instance().findTangentSpace(tangentType);
    if (space == nullptr || space->addUri.empty()) {
        noStructuralTangent("a sum of tangents", tangentType);
    }
    return callTangentOperator(
        ctx, space->addUri, tangentType, {lhsSlot, rhsSlot}, {tangentType, tangentType});
}

} // namespace

// `tangent/add_arr(lhs, rhs)`: the elementwise sum of two tangent arrays. The tangent of an
// array is the array of its elements' tangents, so a tangent of any rank is summed element by
// element and the sum has the shape of the left one (design principles 9.9).
slot_t __tangent_add_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    auto *arrayType = tt::as_ptr<ArrayType>(norm.type(0));
    auto *lhs       = norm.get<Array *>(0);
    auto *rhs       = norm.get<Array *>(1);
    if (lhs->numel() != rhs->numel()) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "autodiff: tangent arrays added that have different lengths");
    }
    Array *sum = Array::create(mm::autoSpace(), arrayType, lhs->shapeSpan());
    for (uint64_t i = 0; i < lhs->numel(); ++i) {
        sum->setSlot(
            i,
            addTangent(ctx, arrayType->elemType(), lhs->getSlot(i), rhs->getSlot(i)),
            arrayType);
    }
    return rtdata::toSlot(sum);
}

// `tangent/zeros_arr(xs)`: the zero tangent of `xs`, an array of its shape whose elements are
// the zero tangents of the elements of `xs`.
slot_t __tangent_zeros_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    Type *primalType  = norm.type(0);
    Type *tangentType = norm.resultType();
    if (tangentType == nullptr) {
        tangentType = camel::core::machineryTangentTypeOf(primalType);
    }
    if (tangentType == nullptr || primalType->code() != TypeCode::Array) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "autodiff: a zero tangent array needs an array with a tangent");
    }
    auto *primalArray  = tt::as_ptr<ArrayType>(primalType);
    auto *tangentArray = tt::as_ptr<ArrayType>(tangentType);
    auto *value        = norm.get<Array *>(0);
    Array *zero        = Array::create(mm::autoSpace(), tangentArray, value->shapeSpan());
    for (uint64_t i = 0; i < value->numel(); ++i) {
        zero->setSlot(
            i,
            zeroTangent(
                ctx, primalArray->elemType(), tangentArray->elemType(), value->getSlot(i)),
            tangentArray);
    }
    return rtdata::toSlot(zero);
}

// `tangent/scatter_add_arr(dx, i, dy)`: `dx` with `dy` added at index `i`. The reverse of
// `xs[i]`: the gradient of one element goes back to its place in the array's gradient.
slot_t __tangent_scatter_add_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    auto *arrayType   = tt::as_ptr<ArrayType>(norm.type(0));
    auto *base        = norm.get<Array *>(0);
    const Int64 index = norm.get<Int64>(1);
    const slot_t delta = norm.slot(2);
    if (index < 0 || static_cast<size_t>(index) >= base->size()) {
        throwRuntimeFault(RuntimeDiag::IndexOutOfRange, index, "an array", base->size());
    }
    // The gradient carries the element type of the array it is the gradient of: an array created
    // without its type holds bare slots, and the next operator to read it finds no element type
    // at all (the `sum: Unsupported Tensor dtype` of the flip line).
    Array *result = Array::create(mm::autoSpace(), arrayType, base->size());
    for (size_t i = 0; i < base->size(); ++i) {
        result->set<slot_t>(i, base->get<slot_t>(i), arrayType);
    }
    const size_t at = static_cast<size_t>(index);
    result->set<slot_t>(
        at, addTangent(ctx, arrayType->elemType(), base->get<slot_t>(at), delta), arrayType);
    return rtdata::toSlot(result);
}

// `tangent/scatter_take_arr(base, indices, delta)`: `base` with `delta`'s rows added back into
// the rows the indices name, a repeated index accumulating. The reverse of `t[idx]`, which takes
// whole rows along the first dimension (design principles 5.4, 9.9): the gradient of the rows a
// gather took goes back to the rows it took them from, and every other row of `base` keeps the
// zero tangent it holds. `base` is the zero tangent of the array the subscript was taken from:
// its shape is the array's, its elements give the zeros. The indices are discrete, so nothing
// reaches them.
slot_t __tangent_scatter_take_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    auto *arrayType   = tt::as_ptr<ArrayType>(norm.type(0));
    auto *base        = norm.get<Array *>(0);
    auto *indices     = norm.get<Array *>(1);
    auto *delta       = norm.get<Array *>(2);
    if (base->rank() < 1) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "autodiff: the gradient of a row take needs an array of rank at least one");
    }
    const uint64_t rows    = static_cast<uint64_t>(base->dim(0));
    const uint64_t rowSize = rows == 0 ? 0 : base->numel() / rows;
    const size_t take      = static_cast<size_t>(indices->numel());
    if (delta->numel() != static_cast<uint64_t>(take) * rowSize) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::format(
                "autodiff: the gradient of a row take has {} elements, the take result has {}",
                delta->numel(),
                static_cast<uint64_t>(take) * rowSize));
    }
    Array *result = Array::create(mm::autoSpace(), arrayType, base->shapeSpan());
    for (uint64_t i = 0; i < base->numel(); ++i) {
        result->setSlot(i, base->getSlot(i), arrayType);
    }
    for (size_t k = 0; k < take; ++k) {
        const Int64 row = indices->get<Int64>(k);
        if (row < 0 || static_cast<uint64_t>(row) >= rows) {
            throwRuntimeFault(
                RuntimeDiag::IndexOutOfRange,
                row,
                base->rank() == 1 ? "an array" : "the first dimension of the input array",
                rows);
        }
        const uint64_t dst = static_cast<uint64_t>(row) * rowSize;
        const uint64_t src = static_cast<uint64_t>(k) * rowSize;
        for (uint64_t j = 0; j < rowSize; ++j) {
            result->setSlot(
                dst + j,
                addTangent(
                    ctx,
                    arrayType->elemType(),
                    result->getSlot(dst + j),
                    delta->getSlot(src + j)),
                arrayType);
        }
    }
    return rtdata::toSlot(result);
}

// `tangent/scatter_slice_arr(dx, like, axis, start, end, step, written)`: the tangent of `like`
// whose positions the slice takes along `axis` hold dx's elements, and whose other positions are
// the zero tangent of `like`'s elements. The reverse of a slice: the gradient of the elements a
// slice took goes back to their places in the parent's gradient. A slice takes each position at
// most once, so a place is written rather than added. `like` is the primal array the slice was
// taken from: its shape gives the result's, its elements give the zeros, and the result type is
// the tangent of its type. The axis is the slice's own second word, so the adjoint scatters along
// the axis the forward took the elements from, for any rank.
slot_t __tangent_scatter_slice_arr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    Type *const primalType  = norm.type(1);
    Type *const tangentType = norm.resultType() != nullptr
                                  ? norm.resultType()
                                  : camel::core::machineryTangentTypeOf(primalType);
    if (tangentType == nullptr || primalType->code() != TypeCode::Array ||
        tangentType->code() != TypeCode::Array) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "autodiff: a sliced gradient needs an array with a tangent");
    }
    auto *const primalArray  = tt::as_ptr<ArrayType>(primalType);
    auto *const tangentArray = tt::as_ptr<ArrayType>(tangentType);
    auto *const like         = norm.get<Array *>(1);
    auto *const dx           = norm.get<Array *>(0);
    const Int64 axis         = sliceAxis(like, norm.get<Int64>(2));
    const auto at            = static_cast<size_t>(axis);
    const SliceRange range   = sliceRange(
        like->dim(at),
        norm.get<Int64>(3),
        norm.get<Int64>(4),
        norm.get<Int64>(5),
        norm.get<Int64>(6));
    const std::vector<int64_t> sliceExtents = slicedExtents(like, at, range);
    if (static_cast<uint64_t>(dx->numel()) != numelOf(sliceExtents) ||
        dx->rank() != like->rank()) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::format(
                "autodiff: the gradient of a slice has {} elements of rank {}, the slice takes "
                "{} elements of rank {}",
                dx->numel(),
                dx->rank(),
                numelOf(sliceExtents),
                like->rank()));
    }
    // The result is the tangent of the parent, so it is created with that type: the value the
    // adjoint hands back states the element type the forward value has (design principles 9.9),
    // and a downstream operator reading a slice's gradient reads that type off the value. An
    // array created without a type holds bare slots and names no element type at all.
    Array *result = Array::create(mm::autoSpace(), tangentArray, like->shapeVector());
    // An array whose tangent's element type is `Void` has no element type to take a zero from;
    // its kind's own zero reads the element type the value actually has, and every position the
    // slice does not take holds that zero.
    if (tangentArray->elemType() != nullptr &&
        tangentArray->elemType()->code() == TypeCode::Void) {
        slot_t zero = moduleZeroTangent(ctx, primalType, tangentType, rtdata::toSlot(like));
        for (size_t i = 0; i < like->numel(); ++i) {
            result->set<slot_t>(i, zero, tangentArray);
        }
        scatterSliceGradient(result, dx, sliceExtents, like->shapeVector(), at, range, tangentArray);
        return rtdata::toSlot(result);
    }
    for (size_t i = 0; i < like->numel(); ++i) {
        result->set<slot_t>(
            i,
            zeroTangent(
                ctx,
                primalArray->elemType(),
                tangentArray->elemType(),
                like->get<slot_t>(i)),
            tangentArray);
    }
    scatterSliceGradient(result, dx, sliceExtents, like->shapeVector(), at, range, tangentArray);
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
