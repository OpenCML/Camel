// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/data/base.h"

namespace camel::core::data {

class OtherData : public Data {
  public:
    OtherData() = default;
    OtherData(type::Type *type) : Data(type) {}
    virtual ~OtherData() = default;

    virtual bool equals(const data_ptr_t &other) const override = 0;
    virtual data_ptr_t clone(bool deep = false) const override  = 0;
    virtual const std::string toString() const override         = 0;
    virtual data_ptr_t convertTo(type::Type *type) override     = 0;
};

} // namespace camel::core::data
