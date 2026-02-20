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

class TupleDataFactory;

class TupleData : public CompositeData {
    friend class TupleDataFactory;

  private:
    std::vector<size_t> refIndices_;
    std::vector<data_ptr_t> data_;

    // 仅由 TupleDataFactory::build() 使用，避免重复计算 Type
    TupleData(Type *type, data_vec_t &&data, std::vector<size_t> &&refIndices);

  public:
    TupleData(data_list_t data = {});
    TupleData(Type *type, data_vec_t &&data);
    virtual ~TupleData() = default;

    static std::shared_ptr<TupleData> create(data_list_t data = {});
    static std::shared_ptr<TupleData> create(Type *type, data_vec_t &&data);

    data_ptr_t get(size_t index) const;
    const std::vector<data_ptr_t> &raw() const { return data_; }
    size_t size() const { return data_.size(); }

    virtual std::vector<std::string> refs() const override;
    virtual bool resolved() const override { return refIndices_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual data_ptr_t convertTo(Type *type) override;
};

// 工厂：收集元素后一次构建 Type 与 TupleData，避免重复计算
class TupleDataFactory {
  public:
    TupleDataFactory();
    ~TupleDataFactory();

    TupleDataFactory &add(const data_ptr_t &e);
    std::shared_ptr<TupleData> build();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
