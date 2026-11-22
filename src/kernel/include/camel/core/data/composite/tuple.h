// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/data/composite/composite.h"

#include <memory>

namespace camel::core::data {

class TupleDataFactory;

class TupleData : public CompositeData {
    friend class TupleDataFactory;

  private:
    std::vector<size_t> holeIndices_;
    std::vector<data_ptr_t> data_;

    // Used only by TupleDataFactory::build() to avoid recomputing Type.
    TupleData(type::Type *type, data_vec_t &&data, std::vector<size_t> &&holeIndices);

  public:
    TupleData(data_list_t data = {});
    TupleData(type::Type *type, data_vec_t &&data);
    virtual ~TupleData() = default;

    static std::shared_ptr<TupleData> create(data_list_t data = {});
    static std::shared_ptr<TupleData> create(type::Type *type, data_vec_t &&data);

    data_ptr_t get(size_t index) const;
    const std::vector<data_ptr_t> &raw() const { return data_; }
    size_t size() const { return data_.size(); }

    virtual std::vector<std::string> refs() const override;
    virtual std::vector<size_t> holes() const override { return holeIndices_; }
    virtual bool resolved() const override { return holeIndices_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual data_ptr_t convertTo(type::Type *type) override;
};

// Factory: collect elements and build Type and TupleData once to avoid recomputing Type.
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

} // namespace camel::core::data
