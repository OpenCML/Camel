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
    Value(type_ptr_t type) : type_(type) {}
    virtual ~Value() = default;

    bool isNull() const { return data_ == nullptr; }
    type_ptr_t type() const { return type_; }
    std::shared_ptr<std::any> data() const { return data_; }

    virtual const std::shared_ptr<Value> convert(type_ptr_t target) const = 0;
};

using value_ptr_t = std::shared_ptr<Value>;

class PrimeValue : public Value {
  public:
    PrimeValue() = delete;
    PrimeValue(type_ptr_t type) : Value(type) {}

    virtual const value_ptr_t convert(type_ptr_t target) const override = 0;
};

class StructValue : public Value {
  public:
    StructValue() = delete;
    StructValue(type_ptr_t type) : Value(type) {}

    virtual const value_ptr_t convert(type_ptr_t target) const override = 0;
};

class Int32Value : public PrimeValue {
  public:
    Int32Value(int32_t data) : PrimeValue(int32TypePtr) {
        data_ = std::make_shared<std::any>(data);
    }
};