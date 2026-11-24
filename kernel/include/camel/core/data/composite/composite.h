// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/data/base.h"

namespace camel::core::data {

class CompositeData : public Data {
  public:
    CompositeData() = default;
    CompositeData(type::Type *type) : Data(type) {}
    virtual ~CompositeData() = default;

    virtual std::vector<std::string> refs() const { return std::vector<std::string>(); }
    virtual std::vector<size_t> holes() const { return {}; }
    virtual bool resolved() const { return true; }
    virtual void resolve(const data_vec_t &dataList) {}

    virtual bool equals(const data_ptr_t &other) const override = 0;
    virtual data_ptr_t clone(bool deep = false) const override  = 0;
    virtual const std::string toString() const override         = 0;
    virtual data_ptr_t convertTo(type::Type *type) override     = 0;
};

} // namespace camel::core::data
