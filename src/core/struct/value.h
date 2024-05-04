/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Apr. 9, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "type.h"

class Value {
  protected:
    type_ptr_t type_ = nullptr;
    std::shared_ptr<std::any> data_ = nullptr;

  public:
    Value() = delete;
    Value(type_ptr_t type = nullptr, std::shared_ptr<std::any> data = nullptr)
        : type_(type), data_(data) {}
    virtual ~Value() = default;

    bool isNull() const { return data_ == nullptr; }
    type_ptr_t type() const { return type_; }
    std::shared_ptr<std::any> data() const { return data_; }

    virtual const std::shared_ptr<Value> convert(type_ptr_t target) const = 0;
};

using value_ptr_t = std::shared_ptr<Value>;

template <typename T> class PrimeValue : public Value {
  public:
    PrimeValue() = delete;
    PrimeValue(const T &data) : Value() {
        if constexpr (std::is_same_v<T, int32_t>) {
            type_ = int32TypePtr;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            type_ = int64TypePtr;
        } else if constexpr (std::is_same_v<T, float>) {
            type_ = floatTypePtr;
        } else if constexpr (std::is_same_v<T, double>) {
            type_ = doubleTypePtr;
        } else if constexpr (std::is_same_v<T, bool>) {
            type_ = boolTypePtr;
        } else if constexpr (std::is_same_v<T, char>) {
            type_ = boolTypePtr;
        } else if constexpr (std::is_same_v<T, std::string>) {
            type_ = stringTypePtr;
        } else {
            type_ = nullptr;
            throw std::runtime_error("Unsupported type");
        }
        data_ = std::make_shared<std::any>(data);
    }

    virtual const value_ptr_t convert(type_ptr_t target) const override = 0;
};

class StructValue : public Value {
  public:
    StructValue() = delete;
    StructValue(type_ptr_t type) : Value(type) {}

    virtual const value_ptr_t convert(type_ptr_t target) const override = 0;
};