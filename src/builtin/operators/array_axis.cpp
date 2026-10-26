// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The layout atoms and the reductions along an axis of the `array` module (stdlib/array.cml):
 * `shape`, `numel`, `dim`, `elements` and `to_bytes`, and `sum_axis`, `mean_axis`, `min_axis`,
 * `max_axis`, `argmax_axis` and `argmin_axis`, each the one native operator under the Camel
 * function that has its name. They are generic declarations (`::<T>`), so there is one executor
 * for each and it reads the element type of the array it is given.
 *
 * An array is a tensor here: a contiguous block in row-major order, its extents beside it. A
 * reduction along axis `a` views it as [outer, extent(a), inner] and reduces the middle
 * dimension; the result has the extents with `a` removed, or kept as 1 with `keepdims`.
 *
 * What each computes is what stdlib/array.cml says of the function:
 *
 *   - a float element type (f32, f64, bf16, f16) is summed in f64 from +0 in ascending order and
 *     rounded once into the element type (design principles 7.3); an integer element type is
 *     summed in itself and wraps around on overflow, as `+` does;
 *   - `mean` is the f64 sum divided by the extent in f64, rounded once, for a float element type;
 *   - `min` and `max` are IEEE 754-2019 minimum and maximum (modules/math/minmax.h, the one
 *     statement of the comparison): a NaN gives NaN, -0 is below +0;
 *   - `argmax` and `argmin` give the position of the winner: the first NaN if there is one, +0
 *     above -0, the earlier position on a tie (design principles 7.7);
 *   - the extent of an axis that is empty sums to 0; `mean`, `min`, `max`, `argmax` and `argmin`
 *     of it are faults, as is an axis outside the rank (kind `InvalidArgument`).
 */

#include "array.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/half.h"
#include "camel/core/type.h"
#include "camel/core/type/composite/array.h"
#include "modules/math/minmax.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;

namespace {

[[noreturn]] void refuse(const char *where, const std::string &message) {
    throwRuntimeFault(RuntimeDiag::InvalidArgument, where, message);
}

/// The axis as an index: a negative axis counts from the last one.
size_t normalizedAxis(Int64 axis, size_t rank, const char *where) {
    const auto extent = static_cast<Int64>(rank);
    if (axis < -extent || axis >= extent) {
        refuse(where, std::format("the axis {} is outside the rank {}", axis, rank));
    }
    return static_cast<size_t>(axis < 0 ? axis + extent : axis);
}

/// An array viewed as [outer, extent, inner] around one axis.
struct AxisSplit {
    int64_t outer  = 1;
    int64_t extent = 1;
    int64_t inner  = 1;
};

AxisSplit splitAt(const Array *array, size_t axis) {
    AxisSplit split;
    const auto shape = array->shapeSpan();
    for (size_t i = 0; i < axis; ++i) {
        split.outer *= shape[i];
    }
    split.extent = shape[axis];
    for (size_t i = axis + 1; i < shape.size(); ++i) {
        split.inner *= shape[i];
    }
    return split;
}

std::vector<int64_t> reducedShape(const Array *array, size_t axis, bool keepdims) {
    std::vector<int64_t> shape = array->shapeVector();
    if (keepdims) {
        shape[axis] = 1;
    } else {
        shape.erase(shape.begin() + static_cast<std::ptrdiff_t>(axis));
    }
    return shape;
}

Array *newArray(TypeCode code, const std::vector<int64_t> &shape) {
    return Array::create(mm::autoSpace(), ArrayType::create(Type::create(code)), shape, false);
}

/// The element as a double, exactly (a bf16 or an f16 widens to f32 first, which is exact).
template <typename T> double widened(T value) {
    if constexpr (is_reduced_float_v<T>) {
        return static_cast<double>(value.toFloat());
    } else {
        return static_cast<double>(value);
    }
}

template <typename T> inline constexpr bool isFloatElement = is_ieee_float_v<T>;

/// Calls `fn` with a value of the element type of `array`, whichever of the six numeric element
/// types it holds; any other element type is refused.
template <typename Fn> slot_t dispatchNumeric(const Array *array, const char *where, Fn &&fn) {
    switch (array->dtype()) {
    case TypeCode::Int32:
        return fn(Int32{});
    case TypeCode::Int64:
        return fn(Int64{});
    case TypeCode::Float32:
        return fn(Float32{});
    case TypeCode::Float64:
        return fn(Float64{});
    case TypeCode::BFloat16:
        return fn(BFloat16{});
    case TypeCode::Float16:
        return fn(Float16{});
    default:
        refuse(where, "the elements are neither integers nor floating-point numbers");
    }
}

enum class Reduce { Sum, Mean, Min, Max, ArgMax, ArgMin };

constexpr const char *nameOf(Reduce op) {
    switch (op) {
    case Reduce::Sum:
        return "array.sum_axis";
    case Reduce::Mean:
        return "array.mean_axis";
    case Reduce::Min:
        return "array.min_axis";
    case Reduce::Max:
        return "array.max_axis";
    case Reduce::ArgMax:
        return "array.argmax_axis";
    default:
        return "array.argmin_axis";
    }
}

/// Whether element `x` is strictly better than the current best `best` for a position search:
/// the first NaN wins and then stays, a larger (smaller) value wins, and between two zeros +0 is
/// above -0.
template <typename T> bool betterPosition(T x, T best, bool largest) {
    if constexpr (isFloatElement<T>) {
        const double a = widened(x);
        const double b = widened(best);
        if (b != b) {
            return false; // the first NaN is kept
        }
        if (a != a) {
            return true;
        }
        if (a == b) {
            if (a != 0.0 || std::signbit(a) == std::signbit(b)) {
                return false;
            }
            return largest ? !std::signbit(a) : std::signbit(a);
        }
        return largest ? a > b : a < b;
    } else {
        return largest ? x > best : x < best;
    }
}

template <typename T> slot_t reduceAxis(Reduce op, ArgsView &norm, bool keepdims, size_t axis) {
    const bool position = op == Reduce::ArgMax || op == Reduce::ArgMin;
    if constexpr (!isFloatElement<T>) {
        if (op == Reduce::Mean) {
            refuse(nameOf(op), "the mean is taken of floating-point elements");
        }
    }
    const Array *input    = norm.get<Array *>(0);
    const AxisSplit split = splitAt(input, axis);
    if (split.extent == 0 && op != Reduce::Sum) {
        refuse(nameOf(op), std::format("the axis {} has no element", axis));
    }
    const std::vector<int64_t> shape = reducedShape(input, axis, keepdims);
    Array *out = newArray(position ? TypeCode::Int64 : input->dtype(), shape);
    // The input is read again after the allocation, which may have moved it.
    input            = norm.get<Array *>(0);
    const T *src     = input->dataAs<T>();
    const int64_t in = split.inner;
    for (int64_t o = 0; o < split.outer; ++o) {
        for (int64_t i = 0; i < in; ++i) {
            const int64_t base = o * split.extent * in + i;
            const int64_t lane = o * in + i;
            auto at            = [&](int64_t k) -> T { return src[base + k * in]; };
            switch (op) {
            case Reduce::Sum:
            case Reduce::Mean: {
                if constexpr (isFloatElement<T>) {
                    double acc = 0.0;
                    for (int64_t k = 0; k < split.extent; ++k) {
                        acc += widened(at(k));
                    }
                    if (op == Reduce::Mean) {
                        acc /= static_cast<double>(split.extent);
                    }
                    out->dataAs<T>()[lane] = T(acc);
                } else {
                    using U = std::make_unsigned_t<T>;
                    U acc{};
                    for (int64_t k = 0; k < split.extent; ++k) {
                        acc = static_cast<U>(acc + static_cast<U>(at(k)));
                    }
                    out->dataAs<T>()[lane] = static_cast<T>(acc);
                }
            } break;
            case Reduce::Min:
            case Reduce::Max: {
                T best = at(0);
                for (int64_t k = 1; k < split.extent; ++k) {
                    if constexpr (isFloatElement<T>) {
                        best = op == Reduce::Min ? camel::core::math::minOf(best, at(k))
                                                 : camel::core::math::maxOf(best, at(k));
                    } else {
                        best = op == Reduce::Min ? std::min(best, at(k)) : std::max(best, at(k));
                    }
                }
                out->dataAs<T>()[lane] = best;
            } break;
            default: {
                int64_t best = 0;
                for (int64_t k = 1; k < split.extent; ++k) {
                    if (betterPosition(at(k), at(best), op == Reduce::ArgMax)) {
                        best = k;
                    }
                }
                out->dataAs<Int64>()[lane] = best;
            } break;
            }
        }
    }
    return toSlot(out);
}

template <Reduce op> slot_t axisKernel(ArgsView &, ArgsView &norm, Context &) {
    const Array *input = norm.get<Array *>(0);
    const size_t axis  = normalizedAxis(norm.get<Int64>(1), input->rank(), nameOf(op));
    const bool keep    = norm.get<Bool>(2);
    return dispatchNumeric(input, nameOf(op), [&](auto tag) {
        return reduceAxis<decltype(tag)>(op, norm, keep, axis);
    });
}

} // namespace

// ---- layout ----

/// `array:shape`: the extents of the array, in order.
slot_t __array_shape__(ArgsView &, ArgsView &norm, Context &) {
    const Array *input = norm.get<Array *>(0);
    const size_t rank  = input->rank();
    std::vector<Int64> extents(input->shape(), input->shape() + rank);
    Array *out = Array::create(mm::autoSpace(), ArrayType::create(Type::Int64()), rank);
    for (size_t i = 0; i < rank; ++i) {
        out->set<Int64>(i, extents[i]);
    }
    return toSlot(out);
}

/// `array:numel`: the number of elements, the product of the extents.
slot_t __array_numel__(ArgsView &, ArgsView &norm, Context &) {
    return toSlot(static_cast<Int64>(norm.get<Array *>(0)->numel()));
}

/// `array:dim`: the extent of one axis; a negative axis counts from the last.
slot_t __array_dim__(ArgsView &with, ArgsView &norm, Context &) {
    const Array *input = norm.get<Array *>(0);
    const size_t axis  = normalizedAxis(with.get<Int64>(0), input->rank(), "array.dim");
    return toSlot(static_cast<Int64>(input->dim(axis)));
}

/// `array:elements`: the elements in row-major order as an array of one dimension. A contiguous
/// array shares its data block.
slot_t __array_elements__(ArgsView &, ArgsView &norm, Context &) {
    Array *input = norm.get<Array *>(0);
    if (input->rank() <= 1 || !input->isCompact()) {
        return toSlot(input);
    }
    const std::array<int64_t, 1> extents{static_cast<int64_t>(input->numel())};
    return toSlot(Array::view(input, extents, 0));
}

/// `array:to_bytes`: the elements as little-endian bytes in row-major order, the array's own
/// bytes.
slot_t __array_to_bytes__(ArgsView &, ArgsView &norm, Context &) {
    const Array *input = norm.get<Array *>(0);
    if (!input->isCompact()) {
        refuse("array.to_bytes", "the elements are not plain data");
    }
    const std::string_view bytes(
        reinterpret_cast<const char *>(input->rawData()), input->byteSize());
    return toSlot(BytesObject::from(bytes, mm::autoSpace()));
}

// ---- reductions along an axis ----

slot_t __array_sum_axis__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return axisKernel<Reduce::Sum>(with, norm, ctx);
}
slot_t __array_mean_axis__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return axisKernel<Reduce::Mean>(with, norm, ctx);
}
slot_t __array_min_axis__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return axisKernel<Reduce::Min>(with, norm, ctx);
}
slot_t __array_max_axis__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return axisKernel<Reduce::Max>(with, norm, ctx);
}
slot_t __array_argmax_axis__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return axisKernel<Reduce::ArgMax>(with, norm, ctx);
}
slot_t __array_argmin_axis__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return axisKernel<Reduce::ArgMin>(with, norm, ctx);
}
