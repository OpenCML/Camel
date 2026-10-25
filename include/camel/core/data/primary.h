// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/data/base.h"
#include "camel/core/rtdata/half.h"
#include "camel/utils/assert.h"
#include "camel/utils/number_text.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <type_traits>

namespace camel::core::data {

template <typename T> class PrimaryData : public Data {
  private:
    mutable T data_;

  public:
    PrimaryData() = delete;
    PrimaryData(const T &data) : Data(), data_(data) {
        if constexpr (std::is_same_v<T, int32_t>) {
            type_ = type::Type::Int32();
        } else if constexpr (std::is_same_v<T, int64_t>) {
            type_ = type::Type::Int64();
        } else if constexpr (std::is_same_v<T, float>) {
            type_ = type::Type::Float32();
        } else if constexpr (std::is_same_v<T, double>) {
            type_ = type::Type::Float64();
        } else if constexpr (std::is_same_v<T, bool>) {
            type_ = type::Type::Bool();
        } else {
            static_cml_assert(!std::is_same_v<T, T>, "Unsupported type");
        }
    }

    T &data() const { return data_; }

    bool isZero() const {
        if constexpr (
            std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t> || std::is_same_v<T, float> ||
            std::is_same_v<T, double>) {
            return data_ == 0;
        } else if constexpr (std::is_same_v<T, bool>) {
            return data_ == false;
        } else {
            static_cml_assert(!std::is_same_v<T, T>, "Unsupported type");
        }
    }

    virtual bool equals(const data_ptr_t &other) const override {
        if (auto o = std::dynamic_pointer_cast<PrimaryData<T>>(other)) {
            return data_ == o->data_;
        }
        return false;
    }

    virtual data_ptr_t clone(bool deep = false) const override {
        return std::make_shared<PrimaryData<T>>(data_);
    }

    virtual const std::string toString() const override {
        if constexpr (std::is_same_v<T, bool>) {
            return data_ ? "true" : "false";
        } else if constexpr (std::is_same_v<T, float>) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(3) << data_;
            return oss.str();
        } else if constexpr (std::is_same_v<T, double>) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(6) << data_;
            return oss.str();
        } else if constexpr (std::is_same_v<T, int32_t>) {
            return std::to_string(data_);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(data_) + "L";
        } else {
            return std::to_string(data_);
        }
    }

    virtual data_ptr_t convertTo(type::Type *type) override;
};

using IntData    = PrimaryData<int32_t>;
using LongData   = PrimaryData<int64_t>;
using FloatData  = PrimaryData<float>;
using DoubleData = PrimaryData<double>;
using BoolData   = PrimaryData<bool>;

/// The data of a `bf16` or `f16` literal: the two bytes of the format, already rounded. The value
/// is not held as an f32, because an f32 cannot represent a bf16 exactly in two bytes and a
/// literal of the type must be an exact member of it (design principles 5.1). `toFloat` gives the
/// f32 it widens to, which is the whole of its value. The slot form is the same 16 bits in the
/// low half (camel/core/rtdata/half.h).
class HalfData : public Data {
  public:
    HalfData() = delete;
    /// `bits` is the stored pattern; the type it is given says which of the two formats it is in.
    HalfData(uint16_t bits, type::Type *type) : Data(type), bits_(bits) {
        ASSERT(
            type != nullptr && (type->code() == type::TypeCode::BFloat16 ||
                                type->code() == type::TypeCode::Float16),
            "HalfData needs bf16 or f16");
    }

    uint16_t bits() const { return bits_; }
    float toFloat() const {
        return type_->code() == type::TypeCode::BFloat16
                   ? camel::core::rtdata::BFloat16::fromBits(bits_).toFloat()
                   : camel::core::rtdata::Float16::fromBits(bits_).toFloat();
    }

    virtual bool equals(const data_ptr_t &other) const override {
        const auto value = std::dynamic_pointer_cast<HalfData>(other);
        return value && type_->code() == value->type_->code() && bits_ == value->bits_;
    }

    virtual data_ptr_t clone(bool deep = false) const override {
        (void)deep;
        return std::make_shared<HalfData>(bits_, type_);
    }

    /// The float text of the value it holds, as every float of the language prints (11.5, 7.7):
    /// the shortest that reads back as the same element of its own type, not the text of the f32
    /// it widens to (`0.1` for bf16 `0.1`, not `0.100097656`). Every path that prints a constant
    /// -- `--inspect`, a tag argument's written form -- reads its text here.
    virtual const std::string toString() const override {
        return type_->code() == type::TypeCode::BFloat16
                   ? camel::core::rtdata::halfText(camel::core::rtdata::BFloat16::fromBits(bits_))
                   : camel::core::rtdata::halfText(camel::core::rtdata::Float16::fromBits(bits_));
    }

    /// A bf16 or f16 value becomes the other of the two only by rounding through a wider type,
    /// which the language writes (`bf16(f16(x))` is a conversion of a conversion): the value here
    /// is already the exact member of its own type, so only its own type takes it.
    virtual data_ptr_t convertTo(type::Type *type) override {
        if (type != nullptr && type->equals(type_)) {
            return shared_from_this();
        }
        return nullptr;
    }

  private:
    uint16_t bits_ = 0;
};

using BFloat16Data = HalfData;
using Float16Data  = HalfData;

// Whether the value of a literal can be held by the narrower primitive type `to`: an int64 in
// an i32, a finite double in an f32. A double too large for an f32 would become an infinity
// and one too small would become zero; neither is an f32 value, so neither fits (i).
template <typename T> bool primaryFitsType(T value, type::TypeCode to) {
    if constexpr (std::is_same_v<T, int64_t>) {
        if (to == type::TypeCode::Int32) {
            return value >= std::numeric_limits<int32_t>::min() &&
                   value <= std::numeric_limits<int32_t>::max();
        }
    } else if constexpr (std::is_same_v<T, double>) {
        if (to == type::TypeCode::Float32) {
            if (!std::isfinite(value)) {
                return true;
            }
            // A nonzero value that becomes zero in an f32 is below half its smallest
            // subnormal: the type has no value to give it.
            if (value != 0.0 && static_cast<float>(value) == 0.0f) {
                return false;
            }
            return std::abs(value) <= static_cast<double>(std::numeric_limits<float>::max());
        }
    }
    return true;
}

/// The data of a complex literal (`c64` or `c128`): two IEEE parts. A c64 rounds both parts to
/// f32 when it is made, so the stored value is exactly a c64 value.
class ComplexData : public Data {
  public:
    ComplexData() = delete;
    ComplexData(double real, double imaginary, type::Type *type) : Data(type) {
        ASSERT(camel::core::type::isComplex(type->code()), "ComplexData needs a complex type");
        if (type->code() == type::TypeCode::Complex64) {
            real      = static_cast<float>(real);
            imaginary = static_cast<float>(imaginary);
        }
        real_      = real;
        imaginary_ = imaginary;
    }

    double real() const { return real_; }
    double imaginary() const { return imaginary_; }

    virtual bool equals(const data_ptr_t &other) const override {
        const auto value = std::dynamic_pointer_cast<ComplexData>(other);
        return value && type_->code() == value->type_->code() && real_ == value->real_ &&
               imaginary_ == value->imaginary_;
    }

    virtual data_ptr_t clone(bool deep = false) const override {
        (void)deep;
        return std::make_shared<ComplexData>(real_, imaginary_, type_);
    }

    virtual const std::string toString() const override {
        return type_->code() == type::TypeCode::Complex64
                   ? camel::utils::complexText(
                         static_cast<float>(real_),
                         static_cast<float>(imaginary_))
                   : camel::utils::complexText(real_, imaginary_);
    }

    virtual data_ptr_t convertTo(type::Type *type) override {
        if (type->equals(type_)) {
            return shared_from_this();
        }
        if (!camel::core::type::isComplex(type->code())) {
            // A complex value does not become a real one by itself: `re(z)` and `im(z)` are
            // the explicit operations (design principles 5.1, no implicit conversion).
            return nullptr;
        }
        if (type->code() == type::TypeCode::Complex64 &&
            !(primaryFitsType(real_, type::TypeCode::Float32) &&
              primaryFitsType(imaginary_, type::TypeCode::Float32))) {
            // A value a c64 cannot hold is not cut to it: the literal is reported instead.
            return nullptr;
        }
        return std::make_shared<ComplexData>(real_, imaginary_, type);
    }

  private:
    double real_      = 0.0;
    double imaginary_ = 0.0;
};

// Why a literal datum cannot be a value of `type`, as the reason clause of a diagnostic; empty
// when there is no such case. An overflow says which range the type has; an underflow says that
// the nearest value would be zero (i), which is not the same statement and is not written as a
// range.
inline std::string literalNotFitReason(const data_ptr_t &data, type::Type *type) {
    if (!data || !type) {
        return {};
    }
    const type::TypeCode from = data->type()->code(), to = type->code();
    if (camel::core::type::isComplex(from)) {
        if (to == type::TypeCode::Complex64) {
            const auto value = std::dynamic_pointer_cast<ComplexData>(data);
            if (value == nullptr) {
                return {};
            }
            const auto underflows = [](double part) {
                return part != 0.0 && static_cast<float>(part) == 0.0f;
            };
            if (underflows(value->real()) || underflows(value->imaginary())) {
                return "too small to be represented (the nearest value is zero)";
            }
            if (!(primaryFitsType(value->real(), type::TypeCode::Float32) &&
                  primaryFitsType(value->imaginary(), type::TypeCode::Float32))) {
                return "out of range (each part at most about 3.4e38 in magnitude)";
            }
        }
        return {};
    }
    if (!type->isPrimitive() || !data->type()->isPrimitive()) {
        return {};
    }
    if (from == type::TypeCode::Int64 && to == type::TypeCode::Int32) {
        const auto value = std::dynamic_pointer_cast<PrimaryData<int64_t>>(data);
        if (value && !primaryFitsType(value->data(), to)) {
            return "out of range (-2147483648 to 2147483647)";
        }
    } else if (from == type::TypeCode::Float64 && to == type::TypeCode::Float32) {
        const auto value = std::dynamic_pointer_cast<PrimaryData<double>>(data);
        if (value && !primaryFitsType(value->data(), to)) {
            if (value->data() != 0.0 && static_cast<float>(value->data()) == 0.0f) {
                return "too small to be represented (the nearest value is zero)";
            }
            return "out of range (at most about 3.4e38 in magnitude)";
        }
    }
    return {};
}

// The text of a numeric literal as the user wrote such a value (no `L` suffix).
inline std::string literalValueText(const data_ptr_t &data) {
    if (const auto real = std::dynamic_pointer_cast<PrimaryData<double>>(data)) {
        std::ostringstream oss;
        oss << std::setprecision(15) << real->data();
        return oss.str();
    }
    std::string text = data->toString();
    if (!text.empty() && text.back() == 'L') {
        text.pop_back();
    }
    return text;
}

template <typename T> data_ptr_t PrimaryData<T>::convertTo(type::Type *type) {
    if (type->equals(type_)) {
        return shared_from_this();
    }
    if (camel::core::type::isComplex(type->code())) {
        // A real literal takes a complex type by widening: `let z: c64 = 2` is the value 2+0i.
        if constexpr (
            std::is_floating_point_v<T> || (std::is_integral_v<T> && !std::is_same_v<T, bool>)) {
            return std::make_shared<ComplexData>(static_cast<double>(data_), 0.0, type);
        } else {
            return nullptr;
        }
    }
    if (type->isPrimitive()) {
        type::TypeCode from = type_->code(), to = type->code();
        if (from == type::TypeCode::Int32 && to == type::TypeCode::Int64) {
            return std::make_shared<LongData>(static_cast<int64_t>(data_));
        } else if (from == type::TypeCode::Int64 && to == type::TypeCode::Int32) {
            // A literal an i32 cannot hold is not converted (it used to be cut to 32 bits:
            // `let b: i32 = 3000000000` was -1294967296). The caller reports it.
            if (!primaryFitsType(data_, to)) {
                return nullptr;
            }
            return std::make_shared<IntData>(static_cast<int32_t>(data_));
        } else if (from == type::TypeCode::Float32 && to == type::TypeCode::Float64) {
            return std::make_shared<DoubleData>(static_cast<double>(data_));
        } else if (from == type::TypeCode::Float64 && to == type::TypeCode::Float32) {
            if (!primaryFitsType(data_, to)) {
                return nullptr;
            }
            return std::make_shared<FloatData>(static_cast<float>(data_));
        }
    }
    return nullptr;
}

class StringData : public Data {
  private:
    std::string data_;

  public:
    StringData() = delete;
    StringData(const std::string &data);

    const std::string &data() const;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual data_ptr_t convertTo(type::Type *type) override;
};

} // namespace camel::core::data
