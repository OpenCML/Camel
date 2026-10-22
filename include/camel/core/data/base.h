// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/type.h"
#include "camel/utils/type.h"

namespace camel::core::data {

class Data;
using data_ptr_t  = std::shared_ptr<Data>;
using data_vec_t  = std::vector<data_ptr_t>;
using data_list_t = std::initializer_list<data_ptr_t>;

class Data : public std::enable_shared_from_this<Data> {
  protected:
    type::Type *type_;

  public:
    Data();
    Data(type::Type *type);
    virtual ~Data() = default;

    type::Type *type() const;
    static data_ptr_t null();

    virtual bool equals(const data_ptr_t &other) const = 0;
    virtual data_ptr_t clone(bool deep = false) const  = 0;
    virtual const std::string toString() const         = 0;
    virtual data_ptr_t convertTo(type::Type *type)     = 0;
};

} // namespace camel::core::data
