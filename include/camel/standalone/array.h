// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `T[]` in the programs std::cpp writes (docs/design/cpp-standalone/README.cn.md, 4 and 5.2).
 *
 * An Array<T> is one pointer to a block {references, size, capacity, elements}; the empty
 * array is the null pointer. The elements are the C++ values of T, so an `int[]` is a run of
 * int64_t. An array is a value (design principles 11.3: `append` returns a new array): two
 * holders of one block never see a change, because a block is changed only while it has one
 * holder. The operations that give a new array take their first argument by value; when the
 * generated code passes its last use (std::move) and nothing else holds the block, they change
 * it in place, so `acc = acc->append<x>` in a loop is linear.
 *
 * The operations follow the runtime's (src/builtin/operators/struct.cpp, ops.cpp), faults
 * included.
 */

#pragma once

#include "camel/standalone/core.h"
#include "camel/standalone/str.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace camel::standalone {

template <typename T> class Array {
  public:
    using value_type = T;

    Array() noexcept = default;
    Array(std::initializer_list<T> items) {
        reserve(items.size());
        for (const T &item : items) {
            push(item);
        }
    }

    /// A literal of `items` and the shape `shape` states: the elements lie flat in row order --
    /// the order a literal of several dimensions writes them -- and the shape travels in the same
    /// block, so a copy of the array is a copy of both. `shape` is what the type states: the writer
    /// reads it off the type, and a shape that does not multiply to the element count is left as
    /// the one dimension the elements alone give (no value is worth a crash over a wrong constant).
    Array(std::initializer_list<T> items, std::initializer_list<std::size_t> shape) {
        reserve(items.size());
        for (const T &item : items) {
            push(item);
        }
        if (rep_ == nullptr) {
            return;
        }
        std::size_t product = 1;
        for (std::size_t extent : shape) {
            product *= extent;
        }
        if (shape.size() == 0 || shape.size() > kMaxRank || product != items.size()) {
            return;
        }
        rep_->rank = static_cast<std::uint32_t>(shape.size());
        std::size_t i = 0;
        for (std::size_t extent : shape) {
            rep_->extents[i++] = extent;
        }
    }

    /// A literal of `items` with the shape written as the words of `shape`, which the generated
    /// program writes from the type of the literal (`rt::Array<double>::shaped`).
    static Array shaped(std::initializer_list<T> items, std::initializer_list<std::size_t> shape) {
        return Array(items, shape);
    }

    /// The elements of `source` from `offset` on, carrying the dimensions `source` states between
    /// `from` and `to` (`atRow` takes one dimension off a value). The elements are copied: the
    /// result is a value of its own, and writing it leaves `source` alone.
    static Array window(const Array<T> &source, std::size_t offset, std::size_t from, std::size_t to) {
        std::size_t count = 1;
        for (std::size_t i = from; i < to; ++i) {
            count *= source.extent(i);
        }
        Array out;
        out.reserve(count);
        for (std::size_t i = 0; i < count && offset + i < source.size(); ++i) {
            out.push(source[offset + i]);
        }
        out.shapeFrom(source, from, to);
        return out;
    }

    Array(const Array &other) noexcept : rep_(other.rep_) {
        if (rep_ != nullptr) {
            ++rep_->references;
        }
    }
    Array(Array &&other) noexcept : rep_(std::exchange(other.rep_, nullptr)) {}
    Array &operator=(Array other) noexcept {
        std::swap(rep_, other.rep_);
        return *this;
    }
    ~Array() { release(rep_); }

    std::size_t size() const noexcept { return rep_ != nullptr ? rep_->size : 0; }
    bool empty() const noexcept { return size() == 0; }
    /// The dimensions of the value, and one of them. An array built by appending is one
    /// dimensional; a literal of several dimensions states its shape (design principles 5.4: the
    /// shape is part of the value, not of the name). `size()` is the number of elements either way,
    /// which is what the operators that run over the whole value read.
    static constexpr std::size_t kMaxRank = 8;
    std::size_t rank() const noexcept { return rep_ != nullptr ? rep_->rank : 0; }
    std::size_t extent(std::size_t i) const noexcept {
        return rep_ != nullptr && i < rep_->rank ? rep_->extents[i] : 0;
    }
    const T &operator[](std::size_t i) const noexcept { return rep_->items()[i]; }
    const T *begin() const noexcept { return rep_ != nullptr ? rep_->items() : nullptr; }
    const T *end() const noexcept { return begin() + size(); }

    /// Makes the block this array's alone (copying it when it is shared), with room for
    /// `capacity` elements at least.
    void reserve(std::size_t capacity) {
        if (rep_ != nullptr && rep_->references == 1 && rep_->capacity >= capacity) {
            return;
        }
        // Growing a block of its own doubles it, so that adding one element at a time is
        // linear; a shared block is copied at the size asked for.
        const bool growing = rep_ != nullptr && rep_->references == 1;
        Rep *fresh         = allocate(std::max(
            {capacity, size(), growing ? rep_->capacity * 2 : std::size_t{0}, std::size_t{4}}));
        if (rep_ != nullptr) {
            // The shape is the value's too: a block copied because it was shared keeps it.
            fresh->rank = rep_->rank;
            for (std::size_t i = 0; i < rep_->rank; ++i) {
                fresh->extents[i] = rep_->extents[i];
            }
        }
        for (std::size_t i = 0; i < size(); ++i) {
            if (rep_->references == 1) {
                new (fresh->items() + i) T(std::move(rep_->items()[i]));
            } else {
                new (fresh->items() + i) T(rep_->items()[i]);
            }
            ++fresh->size;
        }
        release(std::exchange(rep_, fresh));
    }

    /// Adds `item` at the end, in place when the block is this array's alone.
    void push(T item) {
        if (rep_ == nullptr || rep_->references != 1 || rep_->size == rep_->capacity) {
            reserve(std::max(size() + 1, size() * 2));
        }
        new (rep_->items() + rep_->size) T(std::move(item));
        ++rep_->size;
        rep_->rank = 0; // an appended array is one dimension of what it holds
    }

    /// Puts `value` at `index`, in place when the block is this array's alone and a copy of it
    /// otherwise (:op/set_index_in_place): a shared block is the other holder's too.
    void set(std::size_t index, T value) {
        reserve(size());
        rep_->items()[index] = std::move(value);
    }

    friend bool operator==(const Array &a, const Array &b) {
        // By content, element by element (design principles 4.2); not short-cut by identity:
        // an array that holds a NaN does not equal itself.
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (!(a[i] == b[i])) {
                return false;
            }
        }
        return true;
    }

  public:
    // The two shape setters the builders use (`window`, `atAxis`): they state the dimensions of a
    // value made from another's elements. Not part of what a generated program calls.
    /// States the shape a value built from another's elements has (`window`): the dimensions the
    /// source states between `from` and `to`, when they multiply to the elements this value holds.
    void shapeFrom(const Array<T> &source, std::size_t from, std::size_t to) {
        if (rep_ == nullptr || to <= from || to - from > kMaxRank) {
            return;
        }
        std::size_t product = 1;
        for (std::size_t i = from; i < to; ++i) {
            product *= source.extent(i);
        }
        if (product != size()) {
            return;
        }
        rep_->rank = static_cast<std::uint32_t>(to - from);
        for (std::size_t i = 0; i < to - from; ++i) {
            rep_->extents[i] = source.extent(from + i);
        }
    }

    /// States the shape the builder read from the type (`reshape`): `rank` extents, when they
    /// multiply to the elements this value holds.
    void shapeOf(const std::size_t *shape, std::size_t rank) {
        if (rep_ == nullptr || rank == 0 || rank > kMaxRank) {
            return;
        }
        std::size_t product = 1;
        for (std::size_t i = 0; i < rank; ++i) {
            product *= shape[i];
        }
        if (product != size()) {
            return;
        }
        rep_->rank = static_cast<std::uint32_t>(rank);
        for (std::size_t i = 0; i < rank; ++i) {
            rep_->extents[i] = shape[i];
        }
    }

    /// States the shape a value sliced along one dimension has (`sliceSub`): the source's dimensions,
/// the sliced one replaced by the length the bounds gave it.
    void shapeSliced(const Array<T> &source, std::size_t axis, std::size_t count) {
        if (rep_ == nullptr || axis >= source.rank()) {
            return;
        }
        std::size_t product = count;
        for (std::size_t i = 0; i < source.rank(); ++i) {
            if (i != axis) {
                product *= source.extent(i);
            }
        }
        if (product != size()) {
            return;
        }
        rep_->rank = static_cast<std::uint32_t>(source.rank());
        for (std::size_t i = 0; i < source.rank(); ++i) {
            rep_->extents[i] = i == axis ? count : source.extent(i);
        }
    }

    /// States the shape a value built from another's elements has (`atAxis`): every dimension of
    /// the source except `axis`, when they multiply to the elements this value holds.
    void shapeWithout(const Array<T> &source, std::size_t axis) {
        if (rep_ == nullptr || source.rank() < 2 || source.rank() - 1 > kMaxRank) {
            return;
        }
        std::size_t product = 1;
        for (std::size_t i = 0; i < source.rank(); ++i) {
            if (i != axis) {
                product *= source.extent(i);
            }
        }
        if (product != size()) {
            return;
        }
        std::size_t at = 0;
        for (std::size_t i = 0; i < source.rank(); ++i) {
            if (i != axis) {
                rep_->extents[at++] = source.extent(i);
            }
        }
        rep_->rank = static_cast<std::uint32_t>(at);
    }

  private:
    struct Rep {
        std::size_t references;
        std::size_t size;
        std::size_t capacity;
        // The shape of a literal of several dimensions; a block built by appending has rank 0 here
        // and reads as one dimension of `size` elements.
        std::uint32_t rank;
        std::size_t extents[Array::kMaxRank];
        T *items() noexcept { return reinterpret_cast<T *>(this + 1); }
    };
    static Rep *allocate(std::size_t capacity) {
        // Checked here rather than at the class scope: the array of a struct that holds the array
        // (`Node { children: Node[] }`, design principles 5.2) names its element type before that
        // type is complete, and the alignment is only ever needed to allocate.
        static_assert(alignof(T) <= alignof(Rep), "elements must not need more alignment");
        auto *rep       = static_cast<Rep *>(::operator new(sizeof(Rep) + capacity * sizeof(T)));
        rep->references = 1;
        rep->size       = 0;
        rep->capacity   = capacity;
        rep->rank       = 0;
        for (std::size_t &extent : rep->extents) {
            extent = 0;
        }
        return rep;
    }

    static void release(Rep *rep) noexcept {
        if (rep != nullptr && --rep->references == 0) {
            for (std::size_t i = 0; i < rep->size; ++i) {
                rep->items()[i].~T();
            }
            ::operator delete(rep);
        }
    }

    Rep *rep_ = nullptr;
};

/// `len(xs)` (:struct/len_arr): the length of the first dimension, which is what the interpreter
/// answers for a value of several dimensions (the element count only for a one-dimensional one).
template <typename T> int64_t length(const Array<T> &xs) {
    return static_cast<int64_t>(xs.rank() >= 1 ? xs.extent(0) : xs.size());
}

/// `xs[i]` (:op/idx_arr): an index outside the array is IndexOutOfRange at `site` (values: the
/// index, "an array", the length).
template <typename T> T at(const Array<T> &xs, int64_t index, const Site &site) {
    if (index < 0 || static_cast<std::size_t>(index) >= xs.size()) [[unlikely]] {
        fault(site, {std::to_string(index), "an array", std::to_string(xs.size())});
    }
    return xs[static_cast<std::size_t>(index)];
}

/// `xs[i]` on a value of several dimensions: the i-th row -- one dimension less than the value and
/// of the row's own extent, which is the slab the interpreter hands back (design principles 5.4). A
/// position outside the first dimension is IndexOutOfRange at `site`, as it is on a value of one
/// dimension. The elements are copied, so the row is a value of its own and a write to either is no
/// write to the other.
template <typename T> Array<T> atRow(const Array<T> &xs, int64_t index, const Site &site) {
    const std::size_t first = xs.rank() >= 1 ? xs.extent(0) : xs.size();
    if (index < 0 || static_cast<std::size_t>(index) >= first) [[unlikely]] {
        fault(site, {std::to_string(index), "an array", std::to_string(first)});
    }
    if (xs.rank() < 2) {
        // The type the writer read promises several dimensions, so this is defensive only: the
        // value is taken as one row of one element.
        return Array<T>({xs[static_cast<std::size_t>(index)]});
    }
    std::size_t inner = 1;
    for (std::size_t i = 1; i < xs.rank(); ++i) {
        inner *= xs.extent(i);
    }
    return Array<T>::window(xs, static_cast<std::size_t>(index) * inner, 1, xs.rank());
}

/// Copies the plane `index` along `axis` of `xs`, in the order the remaining dimensions run: the
/// walk of `atAxis`, one element per combination of the other dimensions' positions.
template <typename T>
void gatherAxisPlane(Array<T> &out,
                     const Array<T> &xs,
                     std::size_t axis,
                     std::size_t index,
                     const std::size_t *stride,
                     std::size_t dimension,
                     std::size_t offset) {
    if (dimension == xs.rank()) {
        out.push(xs[offset]);
        return;
    }
    if (dimension == axis) {
        gatherAxisPlane(out, xs, axis, index, stride, dimension + 1, offset + index * stride[dimension]);
        return;
    }
    for (std::size_t i = 0; i < xs.extent(dimension); ++i) {
        gatherAxisPlane(out, xs, axis, index, stride, dimension + 1, offset + i * stride[dimension]);
    }
}

/// `xs[i, axis]` -- a position that names the dimension it drops: the value without that dimension,
/// every other one kept whole, which is the slab the interpreter hands back (design principles
/// 5.4). `m[1, 2]` of an `f32[2, 3]` is written as the position on axis 1 and then the position on
/// axis 0, so a position on a whole dimension is what this must answer, not a prefix of the value.
template <typename T>
Array<T> atAxis(const Array<T> &xs, int64_t index, int64_t axis, const Site &site) {
    const std::size_t rank = xs.rank();
    if (rank < 2 || axis < 0 || static_cast<std::size_t>(axis) >= rank) {
        // One dimension is `atRow`'s (the only axis it has), and an axis outside the value's own is
        // a defect of the generated program rather than a value.
        return atRow(xs, index, site);
    }
    const auto dimension = static_cast<std::size_t>(axis);
    if (index < 0 || static_cast<std::size_t>(index) >= xs.extent(dimension)) [[unlikely]] {
        fault(site, {std::to_string(index), "an array", std::to_string(xs.extent(dimension))});
    }
    std::size_t stride[Array<T>::kMaxRank];
    std::size_t count = 1;
    for (std::size_t i = rank; i-- > 0;) {
        stride[i] = count;
        count *= xs.extent(i);
    }
    Array<T> out;
    out.reserve(xs.extent(dimension) == 0 ? 0 : count / xs.extent(dimension));
    gatherAxisPlane(out, xs, dimension, static_cast<std::size_t>(index), stride, 0, 0);
    out.shapeWithout(xs, dimension);
    return out;
}

/// `xs[i] = value` (:op/set_index_in_place): the array with that element changed, the index
/// checked as a read checks it (the same fault, at the same site). The array another holder has
/// is unchanged -- the block is copied when it is shared, so values stay values (design
/// principles 10.5).
template <typename T>
Array<T> setAt(Array<T> xs, int64_t index, std::type_identity_t<T> value, const Site &site) {
    (void)at(xs, index, site);
    xs.set(static_cast<std::size_t>(index), std::move(value));
    return xs;
}

/// A type name applied to an array (:op/atoi, :op/atol, :op/atof, :op/atod): every element converted
/// by the C++ conversion, which is what the runtime's does (core/array_convert.h).
template <typename To, typename From> Array<To> convertArray(const Array<From> &xs) {
    Array<To> out;
    out.reserve(xs.size());
    for (const From &x : xs) {
        out.push(static_cast<To>(x));
    }
    return out;
}

/// `xs->append<x>` (:struct/append_arr): a new array, `xs` followed by `x`.
template <typename T> Array<T> append(Array<T> xs, std::type_identity_t<T> item) {
    xs.push(std::move(item));
    return xs;
}

/// `xs->extend<ys>` (:struct/extend_arr), `concat(xs, ys)` (:struct/concat_arr).
template <typename T> Array<T> concat(Array<T> xs, const Array<T> &ys) {
    xs.reserve(xs.size() + ys.size());
    for (const T &y : ys) {
        xs.push(y);
    }
    return xs;
}

/// `xs->slice<start, end>` (:struct/slice_arr): a bound outside 0..len is SliceOutOfRange at
/// `site` (values: the start, the end, "an array", the length); an end before the start gives
/// the empty array (design principles 3.9).
template <typename T>
Array<T> slice(const Array<T> &xs, int64_t start, int64_t end, const Site &site) {
    const auto n = static_cast<int64_t>(xs.size());
    if (start < 0 || start > n || end < 0 || end > n) [[unlikely]] {
        fault(
            site,
            {std::to_string(start), std::to_string(end), "an array", std::to_string(xs.size())});
    }
    Array<T> out;
    if (end > start) {
        out.reserve(static_cast<std::size_t>(end - start));
        for (int64_t i = start; i < end; ++i) {
            out.push(xs[static_cast<std::size_t>(i)]);
        }
    }
    return out;
}

/// `xs[a:b]`, `xs[a:b:s]`, `xs[:b]`, `xs[a:]`, `xs[::-1]` and `slice(xs, 0, a, b, s)` (:op/slice_sub)
/// as struct.cpp's __slice_sub__ reads them: `written` says which bounds the program wrote (bit 1
/// the end, bit 2 the start), a step of 0 or an axis outside the value is InvalidArgument at
/// `invalid` (values: "[]", what is wrong), and a bound that leaves the dimension is
/// SliceOutOfRange at `outOfRange` (values: the start, the end, "an array", the length).
///
/// One of a value's dimensions is what is sliced, and every other dimension is kept whole
/// (design principles 5.4): `m[0:2]` of a rank-2 value is its first two rows, `m[:, 1]` its second
/// column, and the dimension sliced takes the length the bounds give it. A negative axis counts
/// from the last dimension, which for a value of one dimension is 0 either way.
template <typename T> void sliceGatherPlane(Array<T> &out,
                                           const Array<T> &xs,
                                           std::size_t axis,
                                           int64_t start,
                                           int64_t step,
                                           std::size_t count,
                                           const std::size_t *stride,
                                           std::size_t dimension,
                                           std::size_t offset) {
    if (dimension == xs.rank()) {
        out.push(xs[offset]);
        return;
    }
    if (dimension == axis) {
        for (std::size_t i = 0; i < count; ++i) {
            sliceGatherPlane(
                out,
                xs,
                axis,
                start,
                step,
                count,
                stride,
                dimension + 1,
                offset + static_cast<std::size_t>(start + static_cast<int64_t>(i) * step) *
                             stride[dimension]);
        }
        return;
    }
    for (std::size_t i = 0; i < xs.extent(dimension); ++i) {
        sliceGatherPlane(
            out, xs, axis, start, step, count, stride, dimension + 1, offset + i * stride[dimension]);
    }
}

template <typename T>
Array<T> sliceSub(
    const Array<T> &xs, int64_t axis, int64_t startRaw, int64_t endRaw, int64_t step,
    int64_t written, const Site &outOfRange, const Site &invalid) {
    if (step == 0) [[unlikely]] {
        fault(invalid, {"[]", "a slice step must not be 0"});
    }
    const bool severalDimensions = xs.rank() >= 2;
    if (!severalDimensions && axis != 0 && axis != -1) [[unlikely]] {
        fault(invalid, {"[]", "a slice axis must be between 0 and 0, not " + std::to_string(axis)});
    }
    if (axis == -1) {
        axis = severalDimensions ? static_cast<int64_t>(xs.rank()) - 1 : 0;
    }
    if (severalDimensions &&
        (axis < 0 || static_cast<std::size_t>(axis) >= xs.rank())) [[unlikely]] {
        fault(
            invalid,
            {"[]",
             "a slice axis must be between 0 and " + std::to_string(xs.rank() - 1) + ", not " +
                 std::to_string(axis)});
    }
    const auto extent = static_cast<int64_t>(
        severalDimensions ? xs.extent(static_cast<std::size_t>(axis)) : xs.size());
    const bool wroteEnd   = (written & 1) != 0;
    const bool wroteStart = (written & 2) != 0;
    int64_t start         = 0;
    int64_t count         = 0;
    if (step > 0) {
        start             = wroteStart ? startRaw : 0;
        const int64_t end = wroteEnd ? endRaw : extent;
        if (start < 0 || start > extent || end < 0 || end > extent) [[unlikely]] {
            fault(
                outOfRange,
                {std::to_string(start), std::to_string(end), "an array", std::to_string(extent)});
        }
        count = start < end ? (end - start + step - 1) / step : 0;
    } else {
        start             = wroteStart ? startRaw : extent - 1;
        const int64_t end = wroteEnd ? endRaw : -1;
        if ((wroteStart && (start < 0 || start >= extent)) ||
            (wroteEnd && (end < 0 || end > extent))) [[unlikely]] {
            fault(
                outOfRange,
                {std::to_string(start), std::to_string(end), "an array", std::to_string(extent)});
        }
        count = start > end ? (start - end + (-step) - 1) / (-step) : 0;
    }
    if (severalDimensions) {
        std::size_t stride[Array<T>::kMaxRank];
        std::size_t product = 1;
        for (std::size_t i = xs.rank(); i-- > 0;) {
            stride[i] = product;
            product *= xs.extent(i);
        }
        const auto dimension = static_cast<std::size_t>(axis);
        Array<T> shaped;
        shaped.reserve(xs.extent(dimension) == 0
                           ? 0
                           : (xs.size() / xs.extent(dimension)) * static_cast<std::size_t>(count));
        sliceGatherPlane(
            shaped, xs, dimension, start, step, static_cast<std::size_t>(count), stride, 0, 0);
        shaped.shapeSliced(xs, dimension, static_cast<std::size_t>(count));
        return shaped;
    }
    Array<T> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int64_t i = 0; i < count; ++i) {
        out.push(xs[static_cast<std::size_t>(start + i * step)]);
    }
    return out;
}

/// `head(xs)` (:struct/head_arr): the first element; of the empty array IndexOutOfRange at
/// `site`, as `xs[0]`.
template <typename T> T head(const Array<T> &xs, const Site &site) { return at(xs, 0, site); }

/// `last(xs)` (:struct/last_arr): the last element; of the empty array IndexOutOfRange at `site`
/// with the position -1, the one no array has (struct.cpp's __last_arr__ says the same).
template <typename T> T last(const Array<T> &xs, const Site &site) {
    if (xs.empty()) [[unlikely]] {
        fault(site, {"-1", "an array", "0"});
    }
    return xs[xs.size() - 1];
}

/// `array:sort_d` (src/builtin/operators/array.cpp): the elements in the order `min` and `max` use
/// (design principles 7.7) -- every NaN after every number, -0 below +0, else the usual one -- and
/// ties in the order they stand, so the result is the one `xs[argsort(xs)]` gives, a stable sort
/// of the clone the returning form of the store makes.
inline Array<double> sorted(const Array<double> &xs) {
    const auto order = [](double a, double b) {
        if (std::isnan(b)) {
            return !std::isnan(a);
        }
        if (std::isnan(a)) {
            return false;
        }
        if (a == b) {
            return std::signbit(a) && !std::signbit(b);
        }
        return a < b;
    };
    std::vector<double> items(xs.begin(), xs.end());
    std::stable_sort(items.begin(), items.end(), order);
    Array<double> out;
    out.reserve(items.size());
    for (const double item : items) {
        out.push(item);
    }
    return out;
}

/// `tensor:reshape` (modules/tensor/kernels/layout.cpp): the same elements in row-major order in
/// another shape. The shape travels with the value (design principles 5.4), so the result is a
/// value of that shape -- a target of several dimensions prints its rows, as the interpreter's
/// does -- and a target that changes the element count is a fault at `site` before anything is
/// read, with the input's own count in the message. `count` below zero asks nothing.
template <typename T>
Array<T> reshape(const Array<T> &xs, std::initializer_list<int64_t> shape, int64_t count, const Site &site) {
    if (count >= 0 && static_cast<int64_t>(xs.size()) != count) {
        fault(site, {std::to_string(xs.size())});
    }
    Array<T> out;
    out.reserve(xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i) {
        out.push(xs[i]);
    }
    std::size_t extents[Array<T>::kMaxRank];
    std::size_t rank = 0;
    for (const int64_t extent : shape) {
        if (extent < 0 || rank >= Array<T>::kMaxRank) {
            return out; // a dimension the type does not state: the elements alone are the shape
        }
        extents[rank++] = static_cast<std::size_t>(extent);
    }
    out.shapeOf(extents, rank);
    return out;
}

/// `xs[idx]` with `idx` an array of positions (:op/idx_take, design principles 5.4): the elements
/// those positions name, in that order, as a copy. A position outside the array is
/// IndexOutOfRange at `site` with the position as written, as a plain read is; a position taken
/// twice is taken twice. The value read is a rank-1 one, so the third word of the call -- the
/// axis -- is the only axis there is; an array of another shape runs through the bridge.
template <typename T>
Array<T> take(const Array<T> &xs, const Array<int64_t> &positions, const Site &site) {
    Array<T> out;
    out.reserve(positions.size());
    for (const int64_t position : positions) {
        out.push(at(xs, position, site));
    }
    return out;
}

/// `array:sum_d` (src/builtin/operators/array_reduce.cpp): the elements added from +0, in the order
/// they lie, in `double` -- the accumulator the runtime's reductions use for a floating element
/// type, whichever width the elements have.
inline double sum(const Array<double> &xs) {
    double total = 0.0;
    for (const double x : xs) {
        total += x;
    }
    return total;
}

/// `tail(xs)` (:struct/tail_arr): all but the first element; the empty array for one or none.
template <typename T> Array<T> tail(const Array<T> &xs) {
    Array<T> out;
    for (std::size_t i = 1; i < xs.size(); ++i) {
        out.push(xs[i]);
    }
    return out;
}

/// `xs->contains<x>` (:struct/contains_arr): by the `==` of the element type.
template <typename T> bool contains(const Array<T> &xs, const std::type_identity_t<T> &item) {
    for (const T &x : xs) {
        if (x == item) {
            return true;
        }
    }
    return false;
}

/// `xs->join<sep>` (:str/join, str.cpp __join__): the strings with `separator` between them.
inline Str join(const Array<Str> &xs, const Str &separator) {
    std::string out;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        if (i > 0) {
            out += separator.view();
        }
        out += xs[i].view();
    }
    return Str(out);
}

/// `for c in s` (:struct/iter_str): the code points of `s`, each a one-character string, in
/// order — what `s[i]` gives for every `i` (design principles 11.1).
inline Array<Str> chars(const Str &s) {
    const std::string_view text = s.view();
    Array<Str> out;
    out.reserve(camel::utf8::count(text));
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t start = at;
        (void)camel::utf8::decodeOne(text, at);
        out.push(Str(text.substr(start, at - start)));
    }
    return out;
}

/// `range(start, stop[, step])` (:struct/range): a step of 0 is InvalidArgument at `site`
/// (values: "range", what is wrong).
inline Array<int64_t> range(int64_t start, int64_t stop, int64_t step, const Site &site) {
    if (step == 0) [[unlikely]] {
        fault(
            site,
            {"range",
             "the step is 0 (from " + std::to_string(start) + " to " + std::to_string(stop) +
                 "); a range needs a step that moves"});
    }
    Array<int64_t> out;
    std::size_t count = 0;
    if ((step > 0 && start < stop) || (step < 0 && start > stop)) {
        count = step > 0 ? static_cast<std::size_t>((stop - start + step - 1) / step)
                         : static_cast<std::size_t>((start - stop - 1) / (-step) + 1);
    }
    out.reserve(count);
    int64_t value = start;
    for (std::size_t i = 0; i < count; ++i, value += step) {
        out.push(value);
    }
    return out;
}

/// `zip(xs, ys, ...)` (:struct/zip): the array of tuples `R::value_type{xs[i], ys[i], ...}`;
/// arrays of different lengths are InvalidArgument at `site` (values: "zip", what is wrong,
/// worded as struct.cpp's __zip__ words it).
template <typename R, typename First, typename... Rest>
R zip(const Site &site, const Array<First> &first, const Array<Rest> &...rest) {
    const std::size_t n = first.size();
    std::size_t index   = 1;
    auto check          = [&](std::size_t size) {
        ++index;
        if (size == n) {
            return;
        }
        fault(
            site,
            {"zip",
             sizeof...(Rest) == 1
                 ? "the two sequences have " + std::to_string(n) + " and " +
                       std::to_string(size) + " elements, and they must have the same number"
                 : "sequences 1 and " + std::to_string(index) + " have " + std::to_string(n) +
                       " and " + std::to_string(size) +
                       " elements, and all must have the same number"});
    };
    (check(rest.size()), ...);
    R out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.push(typename R::value_type{first[i], rest[i]...});
    }
    return out;
}

} // namespace camel::standalone
