// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/data/base.h"

namespace camel::core::data {

class NullData : public Data {
  public:
    /// `type` is the nullable type this `null` is the constant of, or null for the `null` of no
    /// nullable type in particular (`void`), which is what the literal has on its own.
    explicit NullData(type::Type *type = nullptr);
    virtual ~NullData() = default;

    virtual bool isNull() const override { return true; }
    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual data_ptr_t convertTo(type::Type *type) override;
};

} // namespace camel::core::data
