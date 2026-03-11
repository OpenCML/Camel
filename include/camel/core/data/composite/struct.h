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

#include "camel/core/data/composite/composite.h"

#include <map>
#include <memory>

namespace camel::core::data {

class StructDataFactory;

class StructData : public CompositeData {
    friend class StructDataFactory;

  private:
    std::vector<std::string> refIndices_;
    // 按字典序存储字段
    std::map<std::string, data_ptr_t> data_;

    // 仅由 StructDataFactory::build() 使用，避免重复计算 Type
    StructData(
        type::Type *type, std::map<std::string, data_ptr_t> &&data,
        std::vector<std::string> &&refIndices);

  public:
    StructData();
    StructData(std::initializer_list<std::pair<std::string, data_ptr_t>> data);
    StructData(std::map<std::string, data_ptr_t> &&data);
    virtual ~StructData() = default;

    static std::shared_ptr<StructData>
    create(std::initializer_list<std::pair<std::string, data_ptr_t>> data);
    static std::shared_ptr<StructData> create(std::map<std::string, data_ptr_t> &&data);

    bool has(const std::string &key) const;
    data_ptr_t get(const std::string &key) const;
    const std::map<std::string, data_ptr_t> &raw() const { return data_; }

    virtual std::vector<std::string> refs() const override;
    virtual bool resolved() const override { return refIndices_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual data_ptr_t convertTo(type::Type *type) override;
};

// 工厂：收集字段后一次构建 Type 与 StructData，避免重复计算
class StructDataFactory {
  public:
    StructDataFactory();
    ~StructDataFactory();

    StructDataFactory &add(const std::string &key, const data_ptr_t &val);
    std::shared_ptr<StructData> build();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace camel::core::data
