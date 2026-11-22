// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/data/base.h"

namespace camel::core::data {

class RefData : public Data {
  private:
    std::string ref_;

  public:
    RefData(const std::string &ref);
    virtual ~RefData() = default;

    const std::string &ref() const;
    /// The name this reference stands for, changed after it was handed out: a literal that hoists
    /// an element it first left to be read by name points the element at the hoisted binding.
    void rename(const std::string &ref) { ref_ = ref; }

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual data_ptr_t convertTo(type::Type *type) override;
};

} // namespace camel::core::data
