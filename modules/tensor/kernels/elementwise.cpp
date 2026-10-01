/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

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

// Elements per chunk below which a loop is not worth splitting across threads.
constexpr int64_t kElementGrain = 1 << 15;

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

template <typename To> void convertBuffer(const Operand &src, To *dst) {
    const uint64_t count = numelOf(src.shape);
    dispatchDType(src.dtype, [&]<typename From>() {
        const From *in = static_cast<const From *>(src.data);
        for (uint64_t i = 0; i < count; ++i) {
            if constexpr (std::is_same_v<To, bool_t>) {
                dst[i] = in[i] != From{} ? 1 : 0;
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
        return static_cast<T>(std::pow(a, b));
    case BinaryOp::Max:
        return std::max(a, b);
    case BinaryOp::Min:
        return std::min(a, b);
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
    default:
        run([op](T x, T y) { return applyBinary(op, x, y); });
        break;
    }
}

/// Integer / bool unary function (only the dtype-preserving operators reach these dtypes).
template <UnaryOp Op, typename T> inline T unaryInt(T x) {
    if constexpr (Op == UnaryOp::Neg) {
        return static_cast<T>(-x);
    } else if constexpr (Op == UnaryOp::Abs) {
        return x < T{} ? static_cast<T>(-x) : x;
    } else if constexpr (Op == UnaryOp::Relu) {
        return x > T{} ? x : T{};
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
/// pass, so they are split across threads at proportionally smaller sizes.
int64_t unaryGrain(UnaryOp op) {
    switch (op) {
    case UnaryOp::Neg:
    case UnaryOp::Abs:
    case UnaryOp::Relu:
        return kElementGrain;
    default:
        return kElementGrain / 8;
    }
}

void runUnaryF32(UnaryOp op, const float *src, float *dst, int64_t count) {
    const UnaryF32Fn kernel = unaryF32Kernel();
    parallelFor(count, unaryGrain(op), [&](int64_t begin, int64_t end) {
        kernel(op, src + begin, dst + begin, end - begin);
    });
}

} // namespace

ScalarOperand ScalarOperand::fromSlot(TypeCode code, slot_t slot) {
    ScalarOperand scalar{};
    scalar.dtype = normalizeTensorDType(code);
    switch (scalar.dtype) {
    case TypeCode::Float32:
        scalar.value.f = static_cast<float>(scalarToDouble(code, slot));
        break;
    case TypeCode::Int64:
        scalar.value.i = scalarToInt64(code, slot);
        break;
    default:
        scalar.value.b = scalarToBool(code, slot) ? 1 : 0;
        break;
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

TensorObject *unary(UnaryOp op, const TensorObject *input, mm::IAllocator &allocator) {
    const TypeCode outType = unaryProducesFloat(op) ? TypeCode::Float32 : input->dtype();
    const TypedBuffer in   = asDType(Operand::of(input), outType);
    TensorObject *out      = TensorObject::create(outType, input->shapeSpan(), allocator);
    const auto count       = static_cast<int64_t>(input->numel());
    if (outType == TypeCode::Float32) {
        runUnaryF32(op, static_cast<const float *>(in.data), out->dataAs<float>(), count);
        return out;
    }
    dispatchDType(outType, [&]<typename T>() {
        runUnaryInt<T>(op, static_cast<const T *>(in.data), out->dataAs<T>(), count);
    });
    return out;
}

TensorObject *binary(BinaryOp op, Operand lhs, Operand rhs, mm::IAllocator &allocator) {
    const TypeCode outType =
        binaryProducesFloat(op) ? TypeCode::Float32 : promoteTensorTypes(lhs.dtype, rhs.dtype);
    const std::span<const int64_t> shapes[] = {lhs.shape, rhs.shape};
    const BroadcastPlan plan                = makePlan(shapes);
    const TypedBuffer a                     = asDType(lhs, outType);
    const TypedBuffer b                     = asDType(rhs, outType);
    TensorObject *out = TensorObject::create(outType, plan.outShape, allocator);
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

TensorObject *compare(CompareOp op, Operand lhs, Operand rhs, mm::IAllocator &allocator) {
    const TypeCode computeType              = promoteTensorTypes(lhs.dtype, rhs.dtype);
    const std::span<const int64_t> shapes[] = {lhs.shape, rhs.shape};
    const BroadcastPlan plan                = makePlan(shapes);
    const TypedBuffer a                     = asDType(lhs, computeType);
    const TypedBuffer b                     = asDType(rhs, computeType);
    TensorObject *out = TensorObject::create(TypeCode::Bool, plan.outShape, allocator);
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

TensorObject *where(Operand cond, Operand lhs, Operand rhs, mm::IAllocator &allocator) {
    const TypeCode outType                  = promoteTensorTypes(lhs.dtype, rhs.dtype);
    const std::span<const int64_t> shapes[] = {lhs.shape, rhs.shape, cond.shape};
    const BroadcastPlan plan                = makePlan(shapes);
    const TypedBuffer a                     = asDType(lhs, outType);
    const TypedBuffer b                     = asDType(rhs, outType);
    const TypedBuffer c                     = asDType(cond, TypeCode::Bool);
    TensorObject *out = TensorObject::create(outType, plan.outShape, allocator);
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

TensorObject *cast(const TensorObject *input, TypeCode dtype, mm::IAllocator &allocator) {
    dtype             = normalizeTensorDType(dtype);
    TensorObject *out = TensorObject::create(dtype, input->shapeSpan(), allocator);
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

TensorObject *
full(TypeCode dtype, std::span<const int64_t> shape, double value, mm::IAllocator &allocator) {
    TensorObject *out = TensorObject::create(dtype, shape, allocator);
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
