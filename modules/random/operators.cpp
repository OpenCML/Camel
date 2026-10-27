// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operators.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/philox.h"
#include "camel/utils/type.h"

#include "../tensor/dtype.h"
#include "../tensor/interop.h"
#include "../tensor/ops/support.h"
#include "../tensor/tensor.h"

#include <cmath>
#include <mutex>
#include <numbers>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;
namespace philox = camel::utils::philox;

// The random module: the one keyed primitive, the two operators of the global stream, and the
// kernels of the whole-array draws. Everything else of the module is Camel (stdlib/random.cml).
std::unordered_map<std::string, operator_t> getRandomOpsMap() {
    return {
        {"seed", __rand_seed__},
        {"next_key", __rand_next_key__},
        {"philox", __rand_philox__},
        {"uniform_array", __rand_uniform_array__},
        {"normal_array", __rand_normal_array__},
    };
}

namespace {

/// A Key struct value: `{ hi, lo }` in the canonical field order (key.h).
Struct *makeKey(const philox::Key &key) {
    Struct *value = Struct::create(2, mm::autoSpace());
    value->set<Int64>(0, static_cast<Int64>(key.hi));
    value->set<Int64>(1, static_cast<Int64>(key.lo));
    return value;
}

philox::Key readKey(const Struct *value) {
    return philox::Key{
        static_cast<philox::u64>(value->get<Int64>(0)),
        static_cast<philox::u64>(value->get<Int64>(1))};
}

/// The four words of the block for a key and a 64-bit counter: the key's low half is the Philox
/// key, its high half and the counter are the 128-bit counter (the counter is the high word).
philox::Block blockOf(const philox::Key &key, philox::u64 counter) {
    return philox::detail::block(key.lo, key.hi, counter);
}

/// The key derived from `key` at `counter`: the words packed two by two, high word first. The
/// Camel `derive` of stdlib/random.cml writes the same packing with `pack`.
philox::Key deriveKey(const philox::Key &key, philox::u64 counter) {
    const philox::Block w = blockOf(key, counter);
    return philox::Key{
        (static_cast<philox::u64>(w.word[0]) << 32) | w.word[1],
        (static_cast<philox::u64>(w.word[2]) << 32) | w.word[3]};
}

/// The i-th key derived from `key` (the Camel `fold`): the derived keys use the counters from -3
/// downwards, so they never share a word with a key's own draws, which count up from 0.
philox::Key foldKey(const philox::Key &key, philox::u64 index) {
    return deriveKey(key, static_cast<philox::u64>(-3) - index);
}

/// The global stream: the key `seed` set (the default is the key of seed 0) and the number of
/// keys taken from it. `next_key` is `fold(key, taken)`, so the stream is the sequence of the
/// keys the keyed `fold` gives.
struct Stream {
    std::mutex lock;
    philox::Key key{0, 0};
    philox::u64 taken = 0;
};

Stream &stream() {
    static Stream instance;
    return instance;
}

/// The uniform draw of `T` from the words of counter 0 of a key: 53 bits for f64, 24 for f32, 11
/// for f16 and 8 for bf16, the same bits as the scalar `uniform` of the Camel module.
template <typename T> double uniformBits(const philox::Block &w) {
    if constexpr (std::is_same_v<T, double>) {
        const philox::u64 high = w.word[0] >> 5, low = w.word[1] >> 6;
        return static_cast<double>(high * 67108864ull + low) * 1.1102230246251565e-16;
    } else if constexpr (std::is_same_v<T, float>) {
        return static_cast<double>(w.word[0] >> 8) * 5.9604644775390625e-8;
    } else if constexpr (std::is_same_v<T, Float16>) {
        return static_cast<double>(w.word[0] >> 21) * 0.00048828125;
    } else {
        return static_cast<double>(w.word[0] >> 24) * 0.00390625;
    }
}

/// A float of 53 bits from counter `counter` of `key`.
double uniform53(const philox::Key &key, philox::u64 counter) {
    return uniformBits<double>(blockOf(key, counter));
}

/// The standard normal value of a key: Box-Muller on two 53-bit uniforms, rounded once into T.
double normalOf(const philox::Key &key) {
    const double u = 1.0 - uniform53(key, 0);
    const double v = uniform53(key, 1);
    return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * std::numbers::pi * v);
}

camel::core::type::TypeCode elementCodeOf(ArgsView &norm) {
    const auto *result =
        tt::as_ptr<camel::core::type::ArrayType>(norm.resultType());
    return result->elemType()->code();
}

[[noreturn]] void refuseElement(const char *op) {
    throwRuntimeFault(
        RuntimeDiag::OperandTypeUnsupported,
        op,
        "the element type of a whole-array draw is f64, f32, f16 or bf16");
}

/// Fills a tensor of `dtype` and `shape` with `value(i)` for element i, rounded into the element
/// type once.
template <typename Value>
::Array *drawArray(
    camel::core::type::TypeCode dtype, std::span<const int64_t> shape, const char *op,
    Value &&value) {
    namespace tn = camel::tensor;
    ::Array *out         = tn::createTensor(dtype, shape, tn::ops::resultAllocator());
    const uint64_t count = out->numel();
    const auto fill      = [&]<typename T>() {
        T *values = out->dataAs<T>();
        for (uint64_t i = 0; i < count; ++i) {
            values[i] = T(value.template operator()<T>(i));
        }
    };
    switch (dtype) {
    case camel::core::type::TypeCode::Float64:
        fill.template operator()<double>();
        break;
    case camel::core::type::TypeCode::Float32:
        fill.template operator()<float>();
        break;
    case camel::core::type::TypeCode::Float16:
        fill.template operator()<Float16>();
        break;
    case camel::core::type::TypeCode::BFloat16:
        fill.template operator()<BFloat16>();
        break;
    default:
        refuseElement(op);
    }
    return out;
}

} // namespace

slot_t __rand_seed__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Stream &s = stream();
    std::lock_guard guard(s.lock);
    s.key   = philox::Key{0, static_cast<philox::u64>(norm.get<Int64>(0))};
    s.taken = 0;
    return NullSlot;
}

slot_t __rand_next_key__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Stream &s = stream();
    philox::Key next;
    {
        std::lock_guard guard(s.lock);
        next = foldKey(s.key, s.taken++);
    }
    return toSlot(makeKey(next));
}

slot_t __rand_philox__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const philox::Key key = readKey(norm.get<Struct *>(0));
    const philox::Block w = blockOf(key, static_cast<philox::u64>(norm.get<Int64>(1)));
    Array *result         = Array::create(
        mm::autoSpace(),
        camel::core::type::ArrayType::create(camel::core::type::Type::Int64()),
        4);
    for (size_t i = 0; i < 4; ++i) {
        result->set<Int64>(i, static_cast<Int64>(w.word[i]));
    }
    return toSlot(result);
}

slot_t __rand_uniform_array__(ArgsView &with, ArgsView &norm, Context &ctx) {
    namespace tn = camel::tensor;
    return tn::ops::runKernel("rand.uniform_array", [&] {
        const philox::Key key = readKey(norm.get<Struct *>(0));
        const tn::Shape shape = tn::parseShapeArray(with.get<::Array *>(0), with.type(0));
        ::Array *out          = drawArray(
            elementCodeOf(norm),
            shape,
            "rand.uniform_array",
            [&]<typename T>(uint64_t i) {
                return uniformBits<T>(blockOf(foldKey(key, i), 0));
            });
        return tn::ops::wrap(norm.resultType(), out);
    });
}

slot_t __rand_normal_array__(ArgsView &with, ArgsView &norm, Context &ctx) {
    namespace tn = camel::tensor;
    return tn::ops::runKernel("rand.normal_array", [&] {
        const philox::Key key = readKey(norm.get<Struct *>(0));
        const tn::Shape shape = tn::parseShapeArray(with.get<::Array *>(0), with.type(0));
        ::Array *out          = drawArray(
            elementCodeOf(norm),
            shape,
            "rand.normal_array",
            [&]<typename T>(uint64_t i) { return normalOf(foldKey(key, i)); });
        return tn::ops::wrap(norm.resultType(), out);
    });
}
