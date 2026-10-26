// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Elementwise kernel implementations.
 *
 * Every kernel works in a single "compute dtype": mixed-dtype operands are
 * first converted (cast to a temporary buffer), so each operation is
 * instantiated once per element type instead of once per operand-type pair.
 *
 * Broadcast iteration (`BroadcastPlan`) right-aligns the operand shapes,
 * assigns stride 0 to broadcast dimensions, and merges adjacent dimensions
 * whose strides stay contiguous for every operand. The innermost merged
 * dimension becomes a tight loop; outer dimensions are walked with an
 * odometer, and the outer range is split across threads.
 */

#include "elementwise.h"

#include "../interop.h"
#include "cpu.h"
#include "minmax.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace camel::tensor::kernels {

namespace detail {
void unaryF32Generic(UnaryOp op, const float *src, float *dst, int64_t count);
#if defined(CAMEL_TENSOR_HAS_AVX2_UNIT)
void unaryF32Avx2(UnaryOp op, const float *src, float *dst, int64_t count);
void unaryF32Avx512(UnaryOp op, const float *src, float *dst, int64_t count);
#endif
} // namespace detail

using type::TypeCode;

namespace {

// One element of a plain elementwise loop (f32 add or multiply over a contiguous buffer) costs
// kNsPerFloat (parallel.h, measured); transcendental operators cost about eight times as much.
constexpr int64_t kElementGrain = grainForWork(kNsPerFloat);

// Up to three operands (lhs, rhs, cond) share one iteration plan.
constexpr size_t kMaxOperands = 3;

struct BroadcastPlan {
    Shape outShape;
    // Merged iteration space: extents[d] with per-operand element strides.
    std::vector<int64_t> extents;
    std::vector<std::array<int64_t, kMaxOperands>> strides;
    uint64_t numel = 1;

    int64_t innerExtent() const { return extents.empty() ? 1 : extents.back(); }
    int64_t outerCount() const {
        int64_t total = 1;
        for (size_t d = 0; d + 1 < extents.size(); ++d) {
            total *= extents[d];
        }
        return total;
    }
};

BroadcastPlan makePlan(std::span<const std::span<const int64_t>> shapes) {
    BroadcastPlan plan;
    size_t rank = 0;
    for (auto shape : shapes) {
        rank = std::max(rank, shape.size());
    }
    plan.outShape.assign(rank, 1);
    for (auto shape : shapes) {
        const size_t offset = rank - shape.size();
        for (size_t i = 0; i < shape.size(); ++i) {
            int64_t &out       = plan.outShape[offset + i];
            const int64_t cand = shape[i];
            if (cand == out || cand == 1) {
                continue;
            }
            if (out != 1) {
                std::string text;
                for (size_t op = 0; op < shapes.size(); ++op) {
                    text += op ? " with [" : "cannot broadcast [";
                    for (size_t d = 0; d < shapes[op].size(); ++d) {
                        text += (d ? ", " : "") + std::to_string(shapes[op][d]);
                    }
                    text += "]";
                }
                throw ShapeError(text);
            }
            out = cand;
        }
    }
    plan.numel = numelOf(plan.outShape);

    // Per-operand contiguous strides, zeroed on broadcast dimensions.
    std::vector<std::array<int64_t, kMaxOperands>> strides(rank);
    for (size_t op = 0; op < shapes.size(); ++op) {
        auto shape          = shapes[op];
        const size_t offset = rank - shape.size();
        int64_t running     = 1;
        for (size_t d = rank; d-- > 0;) {
            const int64_t extent = d >= offset ? shape[d - offset] : 1;
            strides[d][op]       = (extent == 1) ? 0 : running;
            running *= extent;
        }
    }

    // Merge dimension d into d+1 when every operand keeps a linear layout across both.
    for (size_t d = 0; d < rank; ++d) {
        const int64_t extent = plan.outShape[d];
        if (extent == 1) {
            continue; // size-1 dimensions do not affect iteration
        }
        if (!plan.extents.empty()) {
            const int64_t innerExtent = plan.extents.back();
            bool mergeable            = true;
            for (size_t op = 0; op < shapes.size(); ++op) {
                if (plan.strides.back()[op] != strides[d][op] * extent) {
                    mergeable = false;
                    break;
                }
            }
            if (mergeable) {
                plan.extents.back() = innerExtent * extent;
                plan.strides.back() = strides[d];
                continue;
            }
        }
        plan.extents.push_back(extent);
        plan.strides.push_back(strides[d]);
    }
    return plan;
}

/**
 * Walks the plan's outer dimensions for outer indices [begin, end) and calls
 * `inner(offsets, innerExtent, innerStrides)` for each innermost row.
 */
template <typename InnerFn>
void forEachRow(
    const BroadcastPlan &plan, size_t operands, int64_t begin, int64_t end, InnerFn &&inner) {
    const size_t outerDims = plan.extents.empty() ? 0 : plan.extents.size() - 1;
    std::array<int64_t, kMaxOperands> innerStrides{};
    if (!plan.extents.empty()) {
        innerStrides = plan.strides.back();
    }
    // Decompose `begin` into an odometer position.
    std::vector<int64_t> index(outerDims, 0);
    int64_t remaining = begin;
    for (size_t d = outerDims; d-- > 0;) {
        index[d] = remaining % plan.extents[d];
        remaining /= plan.extents[d];
    }
    std::array<int64_t, kMaxOperands> offsets{};
    for (size_t d = 0; d < outerDims; ++d) {
        for (size_t op = 0; op < operands; ++op) {
            offsets[op] += index[d] * plan.strides[d][op];
        }
    }
    const int64_t innerExtent = plan.innerExtent();
    for (int64_t row = begin; row < end; ++row) {
        inner(offsets, row * innerExtent, innerExtent, innerStrides);
        // Advance the odometer.
        for (size_t d = outerDims; d-- > 0;) {
            index[d] += 1;
            for (size_t op = 0; op < operands; ++op) {
                offsets[op] += plan.strides[d][op];
            }
            if (index[d] < plan.extents[d]) {
                break;
            }
            for (size_t op = 0; op < operands; ++op) {
                offsets[op] -= index[d] * plan.strides[d][op];
            }
            index[d] = 0;
        }
    }
}

/// Runs `forEachRow` over the whole plan, splitting outer rows across threads.
template <typename InnerFn>
void runPlan(const BroadcastPlan &plan, size_t operands, InnerFn &&inner) {
    const int64_t rows     = plan.outerCount();
    const int64_t rowWidth = std::max<int64_t>(plan.innerExtent(), 1);
    const int64_t rowGrain = std::max<int64_t>(1, kElementGrain / rowWidth);
    if (plan.numel == 0) {
        return;
    }
    parallelFor(rows, rowGrain, [&](int64_t begin, int64_t end) {
        forEachRow(plan, operands, begin, end, inner);
    });
}

/// Holds either a borrowed operand buffer or a converted copy of it.
struct TypedBuffer {
    const void *data = nullptr;
    std::unique_ptr<std::byte[]> owned;
};

/// The element type two operands are computed in.
///
/// A Camel scalar is a value, not a tensor of some element type: when it meets a tensor it
/// takes that tensor's element type (design principles 5.1, a literal takes its context), so
/// `x * 2.0` keeps an f32 tensor f32 and an f64 tensor f64. A float scalar meeting an integer
/// tensor computes in f32, the way the operator has always returned a float. Two tensors keep
/// their written element types and promote only by widening (`promoteTensorTypes`), so no
/// tensor is converted to a type it was not written with.
TypeCode operandType(const Operand &lhs, const Operand &rhs) {
    if (lhs.scalar != rhs.scalar) {
        const Operand &tensor = lhs.scalar ? rhs : lhs;
        const Operand &scalar = lhs.scalar ? lhs : rhs;
        if (isFloatingTensorType(tensor.dtype)) {
            return tensor.dtype;
        }
        return isFloatingTensorType(scalar.dtype) ? TypeCode::Float32 : tensor.dtype;
    }
    return promoteTensorTypes(lhs.dtype, rhs.dtype);
}

/// Result type of a binary operator: `Div` and `Pow` produce a float, f64 when an operand's
/// element type is f64, f32 otherwise (an integer tensor's division has always produced f32). A
/// bf16 or f16 tensor keeps its element type: the operator computes in f32 and the store rounds
/// once. The element type `operandType` settled decides it: an argument is an array or a value by
/// what it holds, and a rank of zero is a rank like any other, so neither the `scalar` flag (which
/// a rank-0 array shares with a value) nor a rank test may re-decide the width here.
TypeCode binaryResultType(BinaryOp op, const Operand &lhs, const Operand &rhs) {
    const TypeCode base = operandType(lhs, rhs);
    if (!binaryProducesFloat(op)) {
        return base;
    }
    if (isReducedFloatTensorType(base)) {
        return base;
    }
    return base == TypeCode::Float64 ? TypeCode::Float64 : TypeCode::Float32;
}

template <typename To> void convertBuffer(const Operand &src, To *dst) {
    const uint64_t count = numelOf(src.shape);
    dispatchDType(src.dtype, [&]<typename From>() {
        const From *in = static_cast<const From *>(src.data);
        for (uint64_t i = 0; i < count; ++i) {
            if constexpr (std::is_same_v<To, bool_t>) {
                dst[i] = in[i] != From{} ? 1 : 0;
            } else if constexpr (
                camel::core::rtdata::is_reduced_float_v<To> &&
                camel::core::rtdata::is_reduced_float_v<From>) {
                // bf16 to f16 (or back) goes through f32, the compute precision: the stored value
                // widens exactly, then rounds once into the other type (dtype.h).
                dst[i] = To(in[i].toFloat());
            } else {
                dst[i] = static_cast<To>(in[i]);
            }
        }
    });
}

/// Returns the operand's data in dtype `target`, converting when necessary.
TypedBuffer asDType(const Operand &operand, TypeCode target) {
    TypedBuffer buffer;
    if (operand.dtype == target) {
        buffer.data = operand.data;
        return buffer;
    }
    const uint64_t count = numelOf(operand.shape);
    buffer.owned         = std::make_unique<std::byte[]>(count * elementSize(target) + 1);
    dispatchDType(target, [&]<typename T>() {
        convertBuffer<T>(operand, reinterpret_cast<T *>(buffer.owned.get()));
    });
    buffer.data = buffer.owned.get();
    return buffer;
}

template <typename T> T applyBinary(BinaryOp op, T a, T b) {
    switch (op) {
    case BinaryOp::Add:
        return static_cast<T>(a + b);
    case BinaryOp::Sub:
        return static_cast<T>(a - b);
    case BinaryOp::Mul:
        return static_cast<T>(a * b);
    case BinaryOp::Div:
        return static_cast<T>(a / b);
    case BinaryOp::Pow:
        if constexpr (camel::core::rtdata::is_reduced_float_v<T>) {
            // The power functions are computed in f32 and rounded once into the element type,
            // the same path as + - * / (dtype.h).
            return static_cast<T>(std::pow(static_cast<float>(a), static_cast<float>(b)));
        } else {
            return static_cast<T>(std::pow(a, b));
        }
    case BinaryOp::Max:
        return maximum(a, b); // IEEE 754-2019 maximum (minmax.h)
    case BinaryOp::Min:
        return minimum(a, b);
    case BinaryOp::Mod:
        if constexpr (camel::core::rtdata::is_reduced_float_v<T>) {
            // A reduced element type computes in f32 and rounds once when it is stored, the one
            // arithmetic path of a bf16 or f16 value (dtype.h), like + - * / and pow.
            return static_cast<T>(std::fmod(static_cast<float>(a), static_cast<float>(b)));
        } else if constexpr (std::is_floating_point_v<T>) {
            // The language's `%` is the truncated remainder (camel::utils::remainder, int_div.h):
            // on a floating element type that is fmod, the same definition, not a floor.
            return static_cast<T>(std::fmod(a, b));
        } else {
            return static_cast<T>(a % b);
        }
    }
    return T{};
}

template <typename T> bool applyCompare(CompareOp op, T a, T b) {
    switch (op) {
    case CompareOp::Less:
        return a < b;
    case CompareOp::LessEqual:
        return a <= b;
    case CompareOp::Greater:
        return a > b;
    case CompareOp::GreaterEqual:
        return a >= b;
    case CompareOp::Equal:
        return a == b;
    case CompareOp::NotEqual:
        return a != b;
    }
    return false;
}

/**
 * Binary loop with the operator hoisted out of the element loop: the switch
 * selects a lambda once per row, and the compiler vectorizes each row loop.
 */
template <typename T, typename Fn>
void binaryRow(const T *a, int64_t sa, const T *b, int64_t sb, T *out, int64_t n, Fn fn) {
    if (sa == 1 && sb == 1) {
        for (int64_t i = 0; i < n; ++i) {
            out[i] = fn(a[i], b[i]);
        }
    } else if (sa == 1 && sb == 0) {
        const T bv = b[0];
        for (int64_t i = 0; i < n; ++i) {
            out[i] = fn(a[i], bv);
        }
    } else if (sa == 0 && sb == 1) {
        const T av = a[0];
        for (int64_t i = 0; i < n; ++i) {
            out[i] = fn(av, b[i]);
        }
    } else {
        for (int64_t i = 0; i < n; ++i) {
            out[i] = fn(a[i * sa], b[i * sb]);
        }
    }
}

template <typename T>
void binaryTyped(BinaryOp op, const BroadcastPlan &plan, const T *a, const T *b, T *out) {
    auto run = [&](auto rawFn) {
        // Bool results are normalized to 0/1 (e.g. true + true stays true).
        auto fn = [rawFn](T x, T y) -> T {
            if constexpr (std::is_same_v<T, bool_t>) {
                return rawFn(x, y) != 0 ? 1 : 0;
            } else {
                return rawFn(x, y);
            }
        };
        runPlan(
            plan,
            2,
            [&](const std::array<int64_t, kMaxOperands> &offsets,
                int64_t outBase,
                int64_t n,
                const std::array<int64_t, kMaxOperands> &strides) {
                binaryRow<T>(
                    a + offsets[0],
                    strides[0],
                    b + offsets[1],
                    strides[1],
                    out + outBase,
                    n,
                    fn);
            });
    };
    switch (op) {
    case BinaryOp::Add:
        run([](T x, T y) { return static_cast<T>(x + y); });
        break;
    case BinaryOp::Sub:
        run([](T x, T y) { return static_cast<T>(x - y); });
        break;
    case BinaryOp::Mul:
        run([](T x, T y) { return static_cast<T>(x * y); });
        break;
    // Every operator gets its own loop, with the operator a compile-time constant inside it, so
    // the element loop vectorizes.  Handing the runtime `op` to the loop instead (the former
    // `default:` alone) leaves a switch in the innermost loop and a scalar division per element:
    // `x / y` over [512, 256] measured 215 us that way and 63 us with it, against torch's 51 and
    // 46 us in the same two windows (examples/models/naive-in-expert-out/README.md).
    case BinaryOp::Div:
        run([](T x, T y) { return applyBinary(BinaryOp::Div, x, y); });
        break;
    case BinaryOp::Pow:
        run([](T x, T y) { return applyBinary(BinaryOp::Pow, x, y); });
        break;
    case BinaryOp::Max:
        run([](T x, T y) { return applyBinary(BinaryOp::Max, x, y); });
        break;
    case BinaryOp::Min:
        run([](T x, T y) { return applyBinary(BinaryOp::Min, x, y); });
        break;
    case BinaryOp::Mod:
        run([](T x, T y) { return applyBinary(BinaryOp::Mod, x, y); });
        break;
    default:
        // A new operator adds its loop above rather than falling back to the runtime switch.
        throw std::logic_error("binary operator without a hoisted row loop");
    }
}

/// Integer / bool unary function (only the dtype-preserving operators reach these dtypes).
template <UnaryOp Op, typename T> inline T unaryInt(T x) {
    if constexpr (Op == UnaryOp::Neg) {
        return static_cast<T>(-x);
    } else if constexpr (Op == UnaryOp::Abs) {
        return x < T{} ? static_cast<T>(-x) : x;
    } else if constexpr (Op == UnaryOp::Relu) {
        return relu(x);
    } else {
        return x;
    }
}

template <UnaryOp Op, typename T> void unaryIntLoop(const T *src, T *dst, int64_t count) {
    parallelFor(count, kElementGrain, [&](int64_t begin, int64_t end) {
        for (int64_t i = begin; i < end; ++i) {
            dst[i] = unaryInt<Op, T>(src[i]);
        }
    });
}

template <typename T> void runUnaryInt(UnaryOp op, const T *src, T *dst, int64_t count) {
    switch (op) {
    case UnaryOp::Neg:
        return unaryIntLoop<UnaryOp::Neg>(src, dst, count);
    case UnaryOp::Abs:
        return unaryIntLoop<UnaryOp::Abs>(src, dst, count);
    case UnaryOp::Relu:
        return unaryIntLoop<UnaryOp::Relu>(src, dst, count);
    default:
        // Not reached: every other operator produces a float result (unaryProducesFloat).
        throw std::logic_error("integer unary kernel called for a float-producing operator");
    }
}

using UnaryF32Fn = void (*)(UnaryOp, const float *, float *, int64_t);

/// The float unary loops for the widest vector ISA the CPU supports.
UnaryF32Fn unaryF32Kernel() {
    static const UnaryF32Fn kernel = [] {
#if defined(CAMEL_TENSOR_HAS_AVX2_UNIT)
        if (cpuFeatures().avx512f) {
            return &detail::unaryF32Avx512;
        }
        if (cpuFeatures().avx2Fma) {
            return &detail::unaryF32Avx2;
        }
#endif
        return &detail::unaryF32Generic;
    }();
    return kernel;
}

/// Transcendental operators cost roughly an order of magnitude more per element than a memory
/// pass, so one chunk of their work is a proportionally smaller number of elements.
int64_t unaryGrain(UnaryOp op) {
    switch (op) {
    case UnaryOp::Neg:
    case UnaryOp::Abs:
    case UnaryOp::Relu:
        return kElementGrain;
    default:
        return grainForWork(kNsPerFloat * 8.0);
    }
}

void runUnaryF32(UnaryOp op, const float *src, float *dst, int64_t count) {
    const UnaryF32Fn kernel = unaryF32Kernel();
    parallelFor(count, unaryGrain(op), [&](int64_t begin, int64_t end) {
        kernel(op, src + begin, dst + begin, end - begin);
    });
}

/// The f64 unary loops: the same definition as the f32 vector loops, computed in double. An
/// f64 tensor keeps f64 here; the f32 loops are never asked to compute an f64 tensor.
void runUnaryF64(UnaryOp op, const double *src, double *dst, int64_t count) {
    parallelFor(count, unaryGrain(op), [&](int64_t begin, int64_t end) {
        for (int64_t i = begin; i < end; ++i) {
            const double x = src[i];
            switch (op) {
            case UnaryOp::Neg:
                dst[i] = -x;
                break;
            case UnaryOp::Abs:
                dst[i] = std::fabs(x);
                break;
            case UnaryOp::Exp:
                dst[i] = std::exp(x);
                break;
            case UnaryOp::Log:
                dst[i] = std::log(x);
                break;
            case UnaryOp::Sqrt:
                dst[i] = std::sqrt(x);
                break;
            case UnaryOp::Rsqrt:
                dst[i] = 1.0 / std::sqrt(x);
                break;
            case UnaryOp::Sigmoid:
                dst[i] = 1.0 / (1.0 + std::exp(-x));
                break;
            case UnaryOp::Tanh:
                dst[i] = std::tanh(x);
                break;
            case UnaryOp::Relu:
                dst[i] = kernels::relu(x);
                break;
            case UnaryOp::Gelu: {
                constexpr double kScale = 0.7978845608028654; // sqrt(2 / pi)
                dst[i] = 0.5 * x * (1.0 + std::tanh(kScale * (x + 0.044715 * x * x * x)));
                break;
            }
            case UnaryOp::Erf:
                dst[i] = std::erf(x);
                break;
            }
        }
    });
}

} // namespace

ScalarOperand ScalarOperand::fromSlot(TypeCode code, slot_t slot) {
    ScalarOperand scalar{};
    scalar.dtype = code;
    switch (scalar.dtype) {
    case TypeCode::Float32:
        scalar.value.f = static_cast<float>(scalarToDouble(code, slot));
        break;
    case TypeCode::Float64:
        scalar.value.d = scalarToDouble(code, slot);
        break;
    case TypeCode::Int32:
        scalar.value.i32 = static_cast<int32_t>(scalarToInt64(code, slot));
        break;
    case TypeCode::Int64:
        scalar.value.i = scalarToInt64(code, slot);
        break;
    // The two reduced-precision floats keep their own form: a slot holds the two-byte pattern of
    // the type (camel/core/rtdata/half.h), and that pattern is what a kernel computing in the
    // element type reads, with the one rounding the arithmetic already does. A value widened to
    // f32 and left in a wider member of the union would be read as the low half of that f32
    // instead, and the operand would not be the number the program wrote.
    case TypeCode::BFloat16:
        scalar.value.bf16 = rtdata::BFloat16::fromBits(rtdata::halfBitsOfSlot(slot));
        break;
    case TypeCode::Float16:
        scalar.value.f16 = rtdata::Float16::fromBits(rtdata::halfBitsOfSlot(slot));
        break;
    case TypeCode::Bool:
        scalar.value.b = scalarToBool(code, slot) ? 1 : 0;
        break;
    default:
        // A scalar operand is read in the element type the operator declares, and no other type
        // has a member here: reading an unknown one as a bool would hand the kernel a value the
        // program never wrote (design principles 5.1, 7.3).
        throw std::invalid_argument("Unsupported scalar type for a tensor operand");
    }
    return scalar;
}

bool unaryProducesFloat(UnaryOp op) {
    return op != UnaryOp::Neg && op != UnaryOp::Abs && op != UnaryOp::Relu;
}

bool binaryProducesFloat(BinaryOp op) { return op == BinaryOp::Div || op == BinaryOp::Pow; }

Shape broadcastShapes(std::span<const int64_t> lhs, std::span<const int64_t> rhs) {
    const std::span<const int64_t> shapes[] = {lhs, rhs};
    return makePlan(shapes).outShape;
}

Array *unary(UnaryOp op, const Array *input, mm::IAllocator &allocator) {
    // A bf16 or f16 input keeps its element type: the operator is computed by the f32 loops
    // below and each result is rounded once when it is stored, the one arithmetic path of a
    // reduced element type (dtype.h). Widening it to f32 here would change the type the program
    // wrote.
    const bool reduced = isReducedFloatTensorType(input->dtype());
    const TypeCode outType =
        (unaryProducesFloat(op) && !reduced)
            ? (input->dtype() == TypeCode::Float64 ? TypeCode::Float64 : TypeCode::Float32)
            : input->dtype();
    const TypedBuffer in   = asDType(Operand::of(input), outType);
    Array *out      = createTensor(outType, input->shapeSpan(), allocator);
    const auto count       = static_cast<int64_t>(input->numel());
    if (reduced) {
        if (count == 0) {
            return out;
        }
        dispatchDType(outType, [&]<typename T>() {
            const T *src = static_cast<const T *>(in.data);
            std::vector<float> wide(static_cast<size_t>(count));
            for (int64_t i = 0; i < count; ++i) {
                wide[static_cast<size_t>(i)] = static_cast<float>(src[i]);
            }
            runUnaryF32(op, wide.data(), wide.data(), count);
            T *dst = out->dataAs<T>();
            for (int64_t i = 0; i < count; ++i) {
                dst[i] = T(wide[static_cast<size_t>(i)]);
            }
        });
        return out;
    }
    if (outType == TypeCode::Float32) {
        runUnaryF32(op, static_cast<const float *>(in.data), out->dataAs<float>(), count);
        return out;
    }
    if (outType == TypeCode::Float64) {
        runUnaryF64(op, static_cast<const double *>(in.data), out->dataAs<double>(), count);
        return out;
    }
    dispatchDType(outType, [&]<typename T>() {
        runUnaryInt<T>(op, static_cast<const T *>(in.data), out->dataAs<T>(), count);
    });
    return out;
}

void unaryF32(UnaryOp op, const float *src, float *dst, int64_t count) {
    runUnaryF32(op, src, dst, count);
}

Array *binary(BinaryOp op, Operand lhs, Operand rhs, mm::IAllocator &allocator) {
    const TypeCode outType = binaryResultType(op, lhs, rhs);
    const std::span<const int64_t> shapes[] = {lhs.shape, rhs.shape};
    const BroadcastPlan plan                = makePlan(shapes);
    const TypedBuffer a                     = asDType(lhs, outType);
    const TypedBuffer b                     = asDType(rhs, outType);
    Array *out = createTensor(outType, plan.outShape, allocator);
    dispatchDType(outType, [&]<typename T>() {
        binaryTyped<T>(
            op,
            plan,
            static_cast<const T *>(a.data),
            static_cast<const T *>(b.data),
            out->dataAs<T>());
    });
    return out;
}

Array *compare(CompareOp op, Operand lhs, Operand rhs, mm::IAllocator &allocator) {
    const TypeCode computeType              = operandType(lhs, rhs);
    const std::span<const int64_t> shapes[] = {lhs.shape, rhs.shape};
    const BroadcastPlan plan                = makePlan(shapes);
    const TypedBuffer a                     = asDType(lhs, computeType);
    const TypedBuffer b                     = asDType(rhs, computeType);
    Array *out = createTensor(TypeCode::Bool, plan.outShape, allocator);
    bool_t *dst       = out->dataAs<bool_t>();
    dispatchDType(computeType, [&]<typename T>() {
        const T *pa = static_cast<const T *>(a.data);
        const T *pb = static_cast<const T *>(b.data);
        runPlan(
            plan,
            2,
            [&](const std::array<int64_t, kMaxOperands> &offsets,
                int64_t outBase,
                int64_t n,
                const std::array<int64_t, kMaxOperands> &strides) {
                for (int64_t i = 0; i < n; ++i) {
                    dst[outBase + i] = applyCompare(
                                           op,
                                           pa[offsets[0] + i * strides[0]],
                                           pb[offsets[1] + i * strides[1]])
                                           ? 1
                                           : 0;
                }
            });
    });
    return out;
}

Array *where(Operand cond, Operand lhs, Operand rhs, mm::IAllocator &allocator) {
    const TypeCode outType                  = operandType(lhs, rhs);
    const std::span<const int64_t> shapes[] = {lhs.shape, rhs.shape, cond.shape};
    const BroadcastPlan plan                = makePlan(shapes);
    const TypedBuffer a                     = asDType(lhs, outType);
    const TypedBuffer b                     = asDType(rhs, outType);
    const TypedBuffer c                     = asDType(cond, TypeCode::Bool);
    Array *out = createTensor(outType, plan.outShape, allocator);
    dispatchDType(outType, [&]<typename T>() {
        const T *pa      = static_cast<const T *>(a.data);
        const T *pb      = static_cast<const T *>(b.data);
        const bool_t *pc = static_cast<const bool_t *>(c.data);
        T *dst           = out->dataAs<T>();
        runPlan(
            plan,
            3,
            [&](const std::array<int64_t, kMaxOperands> &offsets,
                int64_t outBase,
                int64_t n,
                const std::array<int64_t, kMaxOperands> &strides) {
                for (int64_t i = 0; i < n; ++i) {
                    dst[outBase + i] = pc[offsets[2] + i * strides[2]]
                                           ? pa[offsets[0] + i * strides[0]]
                                           : pb[offsets[1] + i * strides[1]];
                }
            });
    });
    return out;
}

Array *cast(const Array *input, TypeCode dtype, mm::IAllocator &allocator) {
    if (!isSupportedTensorScalar(dtype)) {
        throw std::invalid_argument("Unsupported array element type");
    }
    Array *out = createTensor(dtype, input->shapeSpan(), allocator);
    if (dtype == input->dtype()) {
        if (input->byteSize() > 0) {
            std::memcpy(out->rawData(), input->rawData(), input->byteSize());
        }
        return out;
    }
    dispatchDType(dtype, [&]<typename T>() {
        convertBuffer<T>(Operand::of(input), out->dataAs<T>());
    });
    return out;
}

Array *
full(TypeCode dtype, std::span<const int64_t> shape, double value, mm::IAllocator &allocator) {
    Array *out = createTensor(dtype, shape, allocator);
    dispatchDType(out->dtype(), [&]<typename T>() {
        T converted;
        if constexpr (std::is_same_v<T, bool_t>) {
            converted = value != 0.0 ? 1 : 0;
        } else {
            converted = static_cast<T>(value);
        }
        std::fill_n(out->dataAs<T>(), out->numel(), converted);
    });
    return out;
}

} // namespace camel::tensor::kernels
