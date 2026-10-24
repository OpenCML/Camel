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
    /// The `null` of no nullable type in particular: the value of a `null` literal that stands
    /// where no `T?` is expected, and the empty value of a graph that gives none.
    static data_ptr_t null();
    /// The `null` of the nullable type `type` (design principles 4.4, 5.1): `null` is a
    /// compile-time constant of the `T?` it stands in, so a position that requires a constant --
    /// a parameter default -- gets a value of that type, not a cast of one. Its slot is the null
    /// slot of that type whatever the type is (draft_graph_builder.cpp, `makeStaticSlot`).
    static data_ptr_t null(type::Type *type);

    /// True for the null value of a nullable type (`NullData`). The slot it is read into is the
    /// null slot whatever type it carries, so the one place that turns a constant into a slot asks
    /// this rather than the type.
    virtual bool isNull() const { return false; }

    virtual bool equals(const data_ptr_t &other) const = 0;
    virtual data_ptr_t clone(bool deep = false) const  = 0;
    virtual const std::string toString() const         = 0;
    virtual data_ptr_t convertTo(type::Type *type)     = 0;
};

} // namespace camel::core::data
