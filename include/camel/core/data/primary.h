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
 * Created: Oct. 06, 2024
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/data/base.h"
#include "camel/utils/assert.h"

#include <iomanip>
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
        } else if constexpr (std::is_same_v<T, char>) {
            type_ = type::Type::Byte();
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
        } else if constexpr (std::is_same_v<T, char>) {
            return data_ == '\0';
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
        } else if constexpr (std::is_same_v<T, char>) {
            return std::string(1, data_);
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
using ByteData   = PrimaryData<std::byte>;

template <typename T> data_ptr_t PrimaryData<T>::convertTo(type::Type *type) {
    if (type->equals(type_)) {
        return shared_from_this();
    }
    if (type->isPrimitive()) {
        type::TypeCode from = type_->code(), to = type->code();
        if (from == type::TypeCode::Int32 && to == type::TypeCode::Int64) {
            return std::make_shared<LongData>(static_cast<int64_t>(data_));
        } else if (from == type::TypeCode::Int64 && to == type::TypeCode::Int32) {
            return std::make_shared<IntData>(static_cast<int32_t>(data_));
        } else if (from == type::TypeCode::Float32 && to == type::TypeCode::Float64) {
            return std::make_shared<DoubleData>(static_cast<double>(data_));
        } else if (from == type::TypeCode::Float64 && to == type::TypeCode::Float32) {
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
