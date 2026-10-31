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
#include "camel/standalone/half.h"
#include "camel/standalone/str.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
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
        // `setShape` is the one place that decides whether a shape is recorded, and `{}` is the
        // shape with no dimensions rather than the absence of one: a struct field declared
        // `f32[..]` filled from a literal is that value.
        std::size_t extents[kMaxRank];
        std::size_t rank = 0;
        for (const std::size_t extent : shape) {
            if (rank == kMaxRank) {
                return;
            }
            extents[rank++] = extent;
        }
        setShape(extents, rank);
    }

    /// The array of `items` with the shape written as the words of `shape`, which the generated
    /// program writes from the type of the literal (`rt::Array<double>::shaped`).
    static Array shaped(std::initializer_list<T> items, std::initializer_list<std::size_t> shape) {
        return Array(items, shape);
    }

    /// A value of no rank: the one element *is* the value (design principles 5.4). A position whose
    /// type is `f32[..]` holds such a value when something is written into it, and the difference
    /// between it and an array of one element is what `println` prints (`1.5` and `[1.5]`), so it is
    /// made by its own constructor rather than by appending one element.
    static Array element(T item) {
        Array out;
        out.push(std::move(item));
        out.rep_->rank   = 0;
        out.rep_->zeroDim = true; // the value *is* that element, not an array of one
        return out;
    }

    /// The array of `items` with the extents `extents` states, row order: what a value the runtime
    /// built is handed to the program as (bridge.h). A tensor is a `T[..]` whichever module its
    /// operator belongs to, so its elements are copied out of the runtime's value and its shape
    /// travels with them -- the same array a literal of that type is, which is what lets one
    /// `println` print both.
    static Array fromFlat(std::vector<T> items, const std::vector<int64_t> &extents) {
        Array out;
        out.reserve(items.size());
        for (const T &item : items) {
            out.push(item);
        }
        out.setExtents(extents);
        return out;
    }

    /// The extents of the value, when they are the ones the elements multiply to: a shape that does
    /// not is left off (no value is worth a crash over a wrong constant, the literal's rule).
    template <typename Range> void setExtents(const Range &extents) {
        if (rep_ == nullptr) {
            return;
        }
        std::size_t product = 1;
        std::size_t rank    = 0;
        for (const auto extent : extents) {
            if (extent < 0 || rank == kMaxRank) {
                return;
            }
            product *= static_cast<std::size_t>(extent);
            ++rank;
        }
        if (rank == 0 || product != size()) {
            return;
        }
        rep_->rank        = static_cast<std::uint32_t>(rank);
        std::size_t index = 0;
        for (const auto extent : extents) {
            rep_->extents[index++] = static_cast<std::size_t>(extent);
        }
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
    /// Whether the shape recorded is the one with no dimensions (`setShape` with rank 0): the value
    /// is the element it holds (design principles 5.4), and prints as that element. A block that
    /// records no shape at all is a different thing -- `rank() == 0` too, because there is no rank
    /// to state -- and reads as one dimension of `size()` elements. Rank zero is a fact, not a
    /// number: the shape is there, and it is zero-dimensional.
    bool zeroDimensional() const noexcept { return rep_ != nullptr && rep_->zeroDim; }

    /// Whether the value *is* the element it holds: the zero-dimensional shape, with an element to
    /// be. `Array::element` is what makes one, where `push` grows a value into its first dimension.
    /// The empty array is not an element: it holds nothing to be, and its shape is the one
    /// dimension of nothing -- `[0]` -- as the interpreter answers.
    bool isElement() const noexcept { return zeroDimensional() && size() != 0; }
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
            fresh->zeroDim = rep_->zeroDim;
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
    ///
    /// An appended array is one dimension of what it holds, and it now says so: the rank is left at 0
    /// only for a value that *is* the element (design principles 5.4), and a value of one element with
    /// no rank is indistinguishable from it -- `println` would write `2.0` where the interpreter writes
    /// `[2.0]`. A value grown past one dimension keeps the extents it was given.
    void push(T item) {
        if (rep_ == nullptr || rep_->references != 1 || rep_->size == rep_->capacity) {
            reserve(std::max(size() + 1, size() * 2));
        }
        new (rep_->items() + rep_->size) T(std::move(item));
        ++rep_->size;
        if (rep_->rank <= 1) {
            rep_->rank          = 1;
            rep_->extents[0]    = rep_->size;
        }
        rep_->zeroDim = false; // the dimension it just made is not the empty shape
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
    /// multiply to the elements this value holds. Rank zero is a shape too -- the one with no
    /// dimensions -- and it is recorded as such: a value of one element and no dimensions is the
    /// element, and a block that records nothing is a different thing entirely.
    void setShape(const std::size_t *shape, std::size_t rank) {
        if (rep_ == nullptr || rank > kMaxRank) {
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
        rep_->zeroDim = rank == 0;
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

    /// States the shape a reduction along one dimension leaves (`sumAxis`): the source's dimensions
    /// with the reduced one dropped, or written as one when the caller asked to keep it. The source
    /// may hold another element type, which is why this reads its shape and not its elements: an
    /// argmax answers positions of the value it read.
    template <typename U> void shapeReduced(const Array<U> &source, std::size_t axis, bool keepDims) {
        if (rep_ == nullptr || axis >= source.rank()) {
            return;
        }
        const std::size_t rank = keepDims ? source.rank() : source.rank() - 1;
        std::size_t product    = 1;
        for (std::size_t i = 0; i < rank; ++i) {
            product *= keepDims && i == axis ? std::size_t{1} : source.extent(i < axis ? i : i + 1);
        }
        if (product != size() || rank > kMaxRank) {
            return;
        }
        rep_->rank = static_cast<std::uint32_t>(rank);
        for (std::size_t i = 0; i < rank; ++i) {
            rep_->extents[i] = keepDims && i == axis ? 1 : source.extent(i < axis ? i : i + 1);
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
        // The shape of a literal of several dimensions; a block built by appending has rank 0
        // here at the moment it is made and reads as one dimension of `size` elements. `zeroDim`
        // tells the two apart: the shape with no dimensions (`setShape` with rank 0) against no
        // shape recorded at all.
        std::uint32_t rank;
        bool zeroDim;
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
        rep->zeroDim    = false;
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

/// `shape(xs)` (:array/shape, :tensor/shape): the value's dimensions, one `int` per dimension, as
/// the interpreter's `int[]` -- `[3, 2]` for a rank-2 value, `[4]` for one of a single dimension.
/// It is a value, not a view: the array copies its extents into storage of its own, so a write to
/// the answer cannot reach the value it describes.
///
/// The answer is itself one dimension of `rank` numbers, and it says so: a `shape` nobody shaped
/// would print as an element when it holds one number (`2` for a value of 2) where the interpreter
/// prints `[2]`, and `shape` of a value of no rank -- which *is* the element, and only `Array::element`
/// makes one -- is the empty run. The rank is the test, not the element count: `push` gives every
/// grown array the one dimension it has.
template <typename T> Array<int64_t> shapeOf(const Array<T> &xs) {
    Array<int64_t> out;
    const std::size_t rank = xs.rank();
    if (xs.isElement()) {
        return out;
    }
    if (rank <= 1) {
        out.push(static_cast<int64_t>(xs.size()));
    } else {
        out.reserve(rank);
        for (std::size_t i = 0; i < rank; ++i) {
            out.push(static_cast<int64_t>(xs.extent(i)));
        }
    }
    const std::int64_t count = static_cast<std::int64_t>(out.size());
    out.setExtents(std::initializer_list<std::int64_t>{count});
    return out;
}

/// `zeros::<T>(shape)` and `ones::<T>(shape)` (`tensor:zeros`, `tensor:ones`): the shaped array of
/// the call's element type with every element the fill. The shape is the `int[]` the call is given,
/// so the extents are the value's, read here -- this is the one place in this file that shapes an
/// array from a value rather than from a type. A negative extent is the fault the kernel raises
/// there (`parseShapeArray`, modules/tensor/interop.cpp) rather than an empty array, with the words
/// the kernel's own report carries: the operator's name, then its sentence.
template <typename T>
Array<T> filledArray(const Array<int64_t> &shape, T fill, const char *op, const Site &site) {
    std::vector<int64_t> extents;
    extents.reserve(shape.size());
    std::size_t count = 1;
    for (std::size_t i = 0; i < shape.size(); ++i) {
        const int64_t extent = shape[i];
        if (extent < 0) [[unlikely]] {
            fault(site, {std::string(op) + ": Shape cannot contain negative values"});
        }
        extents.push_back(extent);
        count *= static_cast<std::size_t>(extent);
    }
    return Array<T>::fromFlat(std::vector<T>(count, fill), extents);
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

/// `xs as f32[2, 3]` where the type states the shape (design principles 5.4: the conversion is where
/// a shape is checked). The C++ value is the array either way, so the check is all there is to write:
/// a value that does not hold the extents the type states is the fault the interpreter reports, in
/// its own words and its own place -- `conversion to 'f32[2, 3]' failed: the array has shape [3, 2]`,
/// which is what `ArrayType::castSlotFrom` writes (src/core/type/composite/array.cpp), so the
/// program's report is the interpreter's byte for byte. A stated extent of -1 is one the type leaves
/// open (`f32[?, 7]`) and is not checked.
template <typename T>
Array<T> asShape(
    const Array<T> &xs,
    std::initializer_list<int64_t> extents,
    const char *typeText,
    const Site &site) {
    const std::size_t rank = xs.rank();
    if (rank == 0 && xs.size() > 1) {
        // A value of several elements and no rank is one whose producer did not record a shape: the
        // writer's own array constructions (`rt::Array<int>{{1, 2, 3}}`), a window, a comprehension.
        // The interpreter's value has a shape, so the two cannot be compared here, and a program that
        // is right must not stop because of a record that is missing: the conversion answers the
        // value. Which producers those are is the next defect of this kind (design
        // docs/design/cpp-standalone/README.cn.md 12.2), and the check below is the one that runs
        // wherever the shape did travel with the value.
        return xs;
    }
    bool holds             = rank == extents.size();
    if (holds) {
        std::size_t i = 0;
        for (const int64_t stated : extents) {
            if (stated >= 0 && static_cast<int64_t>(xs.extent(i)) != stated) {
                holds = false;
                break;
            }
            ++i;
        }
    }
    if (!holds) [[unlikely]] {
        std::string actual = "[";
        for (std::size_t i = 0; i < rank; ++i) {
            actual += (i == 0 ? "" : ", ") + std::to_string(xs.extent(i));
        }
        actual += "]";
        fault(
            site,
            {std::string("conversion to '") + typeText + "' failed: the array has shape " + actual});
    }
    return xs;
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

/// `array:elements` (src/builtin/operators/array_axis.cpp): the elements in row-major order as an
/// array of one dimension. An rt::Array keeps the shape its type stated (design principles 5.4),
/// so a value of several dimensions has to lose it here: `len` is the length of the first
/// dimension (stdlib/prelude.cml), and every operator that reads a length after `some_elements`
/// -- `array::mean` divides by `len(es)` -- means the element count, which is the first dimension
/// of the flat run and not of the shaped one. A value of one dimension is returned as it stands.
template <typename T> Array<T> flatten(const Array<T> &xs) {
    if (xs.rank() <= 1) {
        return xs;
    }
    Array<T> out;
    out.reserve(xs.size());
    for (const T &x : xs) {
        out.push(x);
    }
    return out;
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

/// `array:argsort_bf`, `array:argsort_f6` (src/builtin/operators/array.cpp): the positions of the
/// elements in the order `min` and `max` use -- every NaN after every number, -0 below +0, else
/// the usual one -- and ties in the order they stand. It is the order `sorted` states, read as
/// positions instead of values, and it is what a program of a half asks for when it wants
/// `argmax`, `argmin` or a sort rather than a total: `array::argmax` is a loop over
/// `precedes(a, b) = argsort([b, a])[0] == 1`, so the element type of the array decides which
/// argsort is called, and the answer is an index either way.
template <typename Format> Array<int64_t> halfArgsort(const Array<Half<Format>> &xs) {
    const auto order = [](float a, float b) {
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
    std::vector<int64_t> positions(xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i) {
        positions[i] = static_cast<int64_t>(i);
    }
    std::stable_sort(positions.begin(), positions.end(), [&](int64_t l, int64_t r) {
        return order(xs[l].toFloat(), xs[r].toFloat());
    });
    Array<int64_t> out;
    out.reserve(positions.size());
    for (const int64_t position : positions) {
        out.push(position);
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
    out.setShape(extents, rank);
    return out;
}

/// The extents of `xs` between brackets, as the interpreter writes them: `6` for a value of one
/// dimension and `3, 3` for a value of two. A value that states no shape (rank 0) is the one run of
/// elements it holds, which is what its extent reads as.
template <typename T> std::string extentText(const Array<T> &xs) {
    if (xs.rank() == 0) {
        return std::to_string(xs.size());
    }
    std::string out;
    for (std::size_t i = 0; i < xs.rank(); ++i) {
        out += (i == 0 ? "" : ", ") + std::to_string(xs.extent(i));
    }
    return out;
}

/// The shape a program wrote, between brackets, as the interpreter writes it: the elements of the
/// array are the extents (`reshape<[n, 3]>` with n = 3 is `3, 3`). A shape is a run of extents, not
/// a value with a shape of its own, so these elements are read as the list they are.
inline std::string shapeText(const Array<int64_t> &shape) {
    std::string out;
    for (std::size_t i = 0; i < shape.size(); ++i) {
        out += (i == 0 ? "" : ", ") + std::to_string(shape[i]);
    }
    return out;
}

/// `tensor:reshape` with a shape the program computes (`reshape<[n, 3]>(y)`): the target's extents
/// are the elements of `shape`, read while the program runs, because the type does not state them
/// (the caller's contract may state another shape entirely). A target that changes the element
/// count is the fault the interpreter reports, in its own words -- `reshape changes tensor element
/// count: [6] -> [3, 3]`, the two shapes filled in at `site` -- and nothing is read first. An
/// extent of -1 is one the elements fill, as `reshape` above, and asks for no check.
template <typename T>
Array<T> reshapeBy(const Array<T> &xs, const Array<int64_t> &shape, const Site &site) {
    std::size_t extents[Array<T>::kMaxRank];
    std::size_t rank    = 0;
    bool stated         = true;
    std::size_t product = 1;
    for (std::size_t i = 0; i < shape.size(); ++i) {
        const int64_t extent = shape[i];
        if (extent < 0 || rank >= Array<T>::kMaxRank) {
            stated = false; // a dimension the program leaves open: the elements alone are the shape
            break;
        }
        extents[rank++] = static_cast<std::size_t>(extent);
        product *= static_cast<std::size_t>(extent);
    }
    if (stated && product != xs.size()) [[unlikely]] {
        fault(site, {extentText(xs), shapeText(shape)});
    }
    Array<T> out;
    out.reserve(xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i) {
        out.push(xs[i]);
    }
    if (stated) {
        out.setShape(extents, rank);
    }
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

/// `tensor:sum` of the whole value (modules/tensor/ops/reduction_ops.cpp, sumAllKernel): an integer
/// result is the exact int64 accumulator -- int64 as it stands and int32 truncated into its own
/// element type, where truncating once gives the same bits as wrapping at every step -- and every
/// float element type takes the f64 accumulator converted once, added from +0 in the order the
/// elements lie.
///
/// The whole value is what is summed, so the total is the total of the run and no dimension of it
/// has to be known: the grouping never enters. A reduction along an axis is the form that needs the
/// shape, and it is not this one.
template <typename T> T sumAll(const Array<T> &xs) {
    if constexpr (std::is_same_v<T, int64_t>) {
        std::uint64_t acc = 0;
        for (std::size_t i = 0; i < xs.size(); ++i) {
            acc += static_cast<std::uint64_t>(xs[i]);
        }
        return static_cast<int64_t>(acc);
    } else if constexpr (std::is_same_v<T, int32_t>) {
        std::uint64_t acc = 0;
        for (std::size_t i = 0; i < xs.size(); ++i) {
            acc += static_cast<std::uint64_t>(static_cast<int64_t>(xs[i]));
        }
        return static_cast<int32_t>(static_cast<int64_t>(acc));
    } else {
        double total = 0.0;
        for (std::size_t i = 0; i < xs.size(); ++i) {
            total += static_cast<double>(xs[i]);
        }
        return static_cast<T>(total);
    }
}

/// `numel(xs)` (`stdlib/array.cml`): how many elements the value has, which is what the interpreter
/// answers -- one for a value of no dimension, the length for one of a single dimension, and the
/// product of the extents for one of several.
template <typename T> int64_t numel(const Array<T> &xs) { return static_cast<int64_t>(xs.size()); }

/// The lanes of a reduction along one dimension (modules/tensor/kernels/reduce.cpp, `AxisSplit`):
/// `extent` elements `inner` apart, one lane per position of every other dimension, in row-major
/// order of the answer. The lane is read where it lies -- no lane is gathered into a run of its own,
/// and nothing is transposed first -- and the answer keeps the dimensions it did not reduce, or
/// keeps the reduced one as a one when the caller asked for that. `R` is the answer's element type,
/// which is not always the source's: an argmax answers positions.
template <typename T, typename R, typename Lane>
Array<R> axisReduce(
    const Array<T> &xs, int64_t axisArg, bool keepDims, const char *name, const Site &invalid,
    Lane lane) {
    const auto rank    = static_cast<int64_t>(xs.rank());
    const int64_t axis = axisArg < 0 ? axisArg + rank : axisArg;
    if (rank < 2 || axis < 0 || axis >= rank) [[unlikely]] {
        fault(
            invalid,
            {"[]",
             std::string("'") + name + "': the axis " + std::to_string(axisArg) +
                 " is outside the rank " + std::to_string(rank)});
    }
    const auto dimension = static_cast<std::size_t>(axis);
    std::size_t outer    = 1;
    for (std::size_t i = 0; i < dimension; ++i) {
        outer *= xs.extent(i);
    }
    const std::size_t extent = xs.extent(dimension);
    std::size_t inner        = 1;
    for (std::size_t i = dimension + 1; i < xs.rank(); ++i) {
        inner *= xs.extent(i);
    }
    Array<R> out;
    out.reserve(outer * inner);
    for (std::size_t o = 0; o < outer; ++o) {
        for (std::size_t i = 0; i < inner; ++i) {
            out.push(lane(xs, o * extent * inner + i, extent, inner));
        }
    }
    out.shapeReduced(xs, dimension, keepDims);
    return out;
}

/// `tensor:sum_axis` (modules/tensor/kernels/reduce.cpp, reduceAxis): each lane added from +0 in
/// ascending order and rounded into the element type once -- `double` for every float element type
/// and an exact `uint64_t` for integers, as the interpreter's kernel accumulates.
template <typename T>
Array<T> sumAxis(const Array<T> &xs, int64_t axisArg, bool keepDims, const Site &invalid) {
    return axisReduce<T, T>(
        xs,
        axisArg,
        keepDims,
        "tensor:sum_axis",
        invalid,
        [](const Array<T> &v, std::size_t base, std::size_t extent, std::size_t inner) {
            if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, int32_t>) {
                std::uint64_t acc = 0;
                for (std::size_t k = 0; k < extent; ++k) {
                    acc += static_cast<std::uint64_t>(static_cast<int64_t>(v[base + k * inner]));
                }
                return static_cast<T>(static_cast<int64_t>(acc));
            } else {
                double acc = 0.0;
                for (std::size_t k = 0; k < extent; ++k) {
                    acc += static_cast<double>(v[base + k * inner]);
                }
                return static_cast<T>(acc);
            }
        });
}

/// IEEE 754-2019 maximum of two values (modules/math/minmax.h, `maxOf`): a NaN on either side gives
/// the canonical quiet NaN, -0 is below +0, and otherwise the larger operand -- chosen on the values
/// but returned as its own bits, so the sign of a zero survives.
template <typename T> T maxOf(T a, T b) {
    if constexpr (std::is_floating_point_v<T>) {
        if (a != a || b != b) {
            return std::numeric_limits<T>::quiet_NaN();
        }
        if (a != b) {
            return a < b ? b : a;
        }
        using Bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
        return std::bit_cast<T>(static_cast<Bits>(std::bit_cast<Bits>(a) & std::bit_cast<Bits>(b)));
    } else {
        return a < b ? b : a;
    }
}

/// IEEE 754-2019 minimum of two values (the same statement as `maxOf`).
template <typename T> T minOf(T a, T b) {
    if constexpr (std::is_floating_point_v<T>) {
        if (a != a || b != b) {
            return std::numeric_limits<T>::quiet_NaN();
        }
        if (a != b) {
            return b < a ? b : a;
        }
        using Bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
        return std::bit_cast<T>(static_cast<Bits>(std::bit_cast<Bits>(a) | std::bit_cast<Bits>(b)));
    } else {
        return b < a ? b : a;
    }
}

/// `tensor:max_axis` and `tensor:min_axis` (modules/tensor/kernels/reduce.cpp): each lane folded
/// from the operation's identity -- -inf for a maximum of floats, +inf for a minimum, the lowest and
/// the largest value otherwise -- so that one NaN element still makes the answer NaN. Nothing is
/// rounded and no accumulator takes part. A lane with no element has no extreme: the interpreter
/// refuses it, and so does this.
template <bool Maximum, typename T>
Array<T> extremeAxis(
    const Array<T> &xs, int64_t axisArg, bool keepDims, const Site &invalid, const Site &empty) {
    const auto rank    = static_cast<int64_t>(xs.rank());
    const int64_t axis = axisArg < 0 ? axisArg + rank : axisArg;
    if (rank >= 2 && axis >= 0 && axis < rank) {
        const auto dimension = static_cast<std::size_t>(axis);
        if (xs.extent(dimension) == 0) [[unlikely]] {
            fault(
                empty,
                {"[]",
                 std::string("'") + (Maximum ? "tensor:max_axis" : "tensor:min_axis") +
                     "': the axis " + std::to_string(axis) + " has no element"});
        }
    }
    return axisReduce<T, T>(
        xs,
        axisArg,
        keepDims,
        Maximum ? "tensor:max_axis" : "tensor:min_axis",
        invalid,
        [](const Array<T> &v, std::size_t base, std::size_t extent, std::size_t inner) {
            T best;
            if constexpr (std::is_floating_point_v<T>) {
                best = Maximum ? -std::numeric_limits<T>::infinity()
                               : std::numeric_limits<T>::infinity();
            } else {
                best = Maximum ? std::numeric_limits<T>::lowest()
                               : std::numeric_limits<T>::max();
            }
            for (std::size_t k = 0; k < extent; ++k) {
                best = Maximum ? maxOf(best, v[base + k * inner]) : minOf(best, v[base + k * inner]);
            }
            return best;
        });
}

/// `tensor:max_axis`.
template <typename T>
Array<T> maxAxis(
    const Array<T> &xs, int64_t axisArg, bool keepDims, const Site &invalid, const Site &empty) {
    return extremeAxis<true>(xs, axisArg, keepDims, invalid, empty);
}

/// `tensor:min_axis`.
template <typename T>
Array<T> minAxis(
    const Array<T> &xs, int64_t axisArg, bool keepDims, const Site &invalid, const Site &empty) {
    return extremeAxis<false>(xs, axisArg, keepDims, invalid, empty);
}

/// Whether `v`, met after the extreme so far, takes its place (modules/tensor/kernels/minmax.h,
/// `beyondExtreme`): the order is the one of IEEE 754-2019 maximum and minimum, so a NaN is beyond
/// every number and the first NaN is kept, +0 is above -0, and on a tie the earlier position stays.
template <bool Maximum, typename T> bool beyondExtreme(T v, T best) {
    if constexpr (std::is_floating_point_v<T>) {
        if (best != best) {
            return false;
        }
        if (v != v) {
            return true;
        }
        if (v == best) {
            // Only +0 and -0 compare equal with different bits: the maximum takes the +0 and the
            // minimum the -0, so a signed zero is a position the comparison can still tell apart.
            return v == 0 && (Maximum ? (std::signbit(best) && !std::signbit(v))
                                      : (!std::signbit(best) && std::signbit(v)));
        }
    }
    return Maximum ? best < v : v < best;
}

/// `tensor:argmax_axis` and `tensor:argmin_axis` (modules/tensor/kernels/reduce.cpp): the position
/// of the first element of the lane that equals its maximum (minimum), or of its first NaN. The
/// answer's element type is int64 whatever the source holds, so it is a value of positions and not
/// of elements; a lane with no element has no position, and the interpreter refuses that.
template <bool Maximum, typename T>
Array<int64_t> argExtremeAxis(
    const Array<T> &xs, int64_t axisArg, bool keepDims, const Site &invalid, const Site &empty) {
    const auto rank    = static_cast<int64_t>(xs.rank());
    const int64_t axis = axisArg < 0 ? axisArg + rank : axisArg;
    if (rank >= 2 && axis >= 0 && axis < rank) {
        const auto dimension = static_cast<std::size_t>(axis);
        if (xs.extent(dimension) == 0) [[unlikely]] {
            fault(
                empty,
                {"[]",
                 std::string("'") + (Maximum ? "tensor:argmax_axis" : "tensor:argmin_axis") +
                     "': the axis " + std::to_string(axis) + " has no element"});
        }
    }
    return axisReduce<T, int64_t>(
        xs,
        axisArg,
        keepDims,
        Maximum ? "tensor:argmax_axis" : "tensor:argmin_axis",
        invalid,
        [](const Array<T> &v, std::size_t base, std::size_t extent, std::size_t inner) {
            T best          = v[base];
            int64_t bestIdx = 0;
            for (std::size_t k = 1; k < extent; ++k) {
                const T candidate = v[base + k * inner];
                if (beyondExtreme<Maximum>(candidate, best)) {
                    best    = candidate;
                    bestIdx = static_cast<int64_t>(k);
                }
            }
            return bestIdx;
        });
}

/// `tensor:argmax_axis`.
template <typename T>
Array<int64_t> argmaxAxis(
    const Array<T> &xs, int64_t axisArg, bool keepDims, const Site &invalid, const Site &empty) {
    return argExtremeAxis<true>(xs, axisArg, keepDims, invalid, empty);
}

/// `tensor:argmin_axis`.
template <typename T>
Array<int64_t> argminAxis(
    const Array<T> &xs, int64_t axisArg, bool keepDims, const Site &invalid, const Site &empty) {
    return argExtremeAxis<false>(xs, axisArg, keepDims, invalid, empty);
}

/// `tensor:mean` of the whole value (modules/tensor/ops/reduction_ops.cpp, meanAllKernel): the sum
/// divided by the number of elements, both in double, and the answer's element type reached by one
/// conversion -- `stdlib/tensor_definitions.cml` states a mean as `sum(x) / float(numel(x))`, and
/// the kernel is that statement with the sum and the division in f64. That is why the sum is taken
/// here as the double the interpreter's `sumAll` returns and not as the rounded element value: a
/// mean of f32 elements is not the f32 sum divided again.
///
/// `R` is the answer's element type, which the operator states apart from the source's: a mean of
/// integers is an f32 (an integer's mean is not integer division), and a mean of f64 or f32 keeps
/// that element type.
///
/// A value with no element divides by zero, and the interpreter raises that fault rather than
/// answering a NaN; so does this.
template <typename R, typename T> R meanAll(const Array<T> &xs, const Site &division) {
    if (xs.size() == 0) [[unlikely]] {
        fault(division);
    }
    if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, int32_t>) {
        std::uint64_t acc = 0;
        for (std::size_t i = 0; i < xs.size(); ++i) {
            acc += static_cast<std::uint64_t>(static_cast<int64_t>(xs[i]));
        }
        return static_cast<R>(
            static_cast<double>(static_cast<int64_t>(acc)) / static_cast<double>(xs.size()));
    } else {
        double total = 0.0;
        for (std::size_t i = 0; i < xs.size(); ++i) {
            total += static_cast<double>(xs[i]);
        }
        return static_cast<R>(total / static_cast<double>(xs.size()));
    }
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
