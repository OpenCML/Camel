// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/data/composite/composite.h"

#include <memory>

namespace camel::core::data {

class ArrayDataFactory;

class ArrayData : public CompositeData {
    friend class ArrayDataFactory;

  private:
    // Value-level hole positions for unresolved elements in the array literal.
    std::vector<size_t> holeIndices_;
    std::vector<data_ptr_t> data_;

    // Used only by ArrayDataFactory::build() to avoid recomputing Type.
    ArrayData(type::Type *arrayType, data_vec_t &&data);

  public:
    ArrayData(type::Type *elemType, data_list_t data = {});
    ArrayData(type::Type *elemType, const data_vec_t &data);
    virtual ~ArrayData() = default;

    static std::shared_ptr<ArrayData> create(type::Type *elemType, data_list_t data = {});
    static std::shared_ptr<ArrayData> from(type::Type *elemType, const data_vec_t &data);

    const std::vector<data_ptr_t> &raw() const { return data_; }

    virtual std::vector<std::string> refs() const override;
    virtual std::vector<size_t> holes() const override { return holeIndices_; }
    virtual bool resolved() const override { return holeIndices_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual data_ptr_t convertTo(type::Type *type) override;
};

// Factory: collect elements and build Type and ArrayData once to avoid recomputing Type.
class ArrayDataFactory {
  public:
    ArrayDataFactory();
    ~ArrayDataFactory();

    ArrayDataFactory &add(const data_ptr_t &e);
    /// The element type the array is built with, before any element is added. A literal built
    /// for a position that names the element type (`[a, b]` where an `int[]` is expected) is an
    /// array of that type, and its computed elements are holes to be filled later; without it
    /// the type is the one the elements unify to.
    ArrayDataFactory &setElemType(type::Type *elemType);
    std::shared_ptr<ArrayData> build();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace camel::core::data
