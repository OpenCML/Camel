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
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "composite.h"

#include <memory>

class ArrayDataFactory;

class ArrayData : public CompositeData {
    friend class ArrayDataFactory;

  private:
    std::vector<size_t> refs_;
    std::vector<data_ptr_t> data_;

    // 仅由 ArrayDataFactory::build() 使用，避免重复计算 Type
    ArrayData(Type *arrayType, data_vec_t &&data);

  public:
    ArrayData(Type *elemType, data_list_t data = {});
    ArrayData(Type *elemType, const data_vec_t &data);
    virtual ~ArrayData() = default;

    static std::shared_ptr<ArrayData> create(Type *elemType, data_list_t data = {});
    static std::shared_ptr<ArrayData> from(Type *elemType, const data_vec_t &data);

    const std::vector<data_ptr_t> &raw() const { return data_; }

    virtual std::vector<std::string> refs() const override;
    virtual bool resolved() const override { return refs_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual data_ptr_t convertTo(Type *type) override;
};

// 工厂：收集元素后一次构建 Type 与 ArrayData，避免重复计算
class ArrayDataFactory {
  public:
    ArrayDataFactory();
    ~ArrayDataFactory();

    ArrayDataFactory &add(const data_ptr_t &e);
    std::shared_ptr<ArrayData> build();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
