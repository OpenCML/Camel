// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/data/composite/composite.h"

#include <cstdint>
#include <memory>
#include <span>

namespace camel::core::data {

class ArrayDataFactory;

class ArrayData : public CompositeData {
    friend class ArrayDataFactory;

  private:
    // Value-level hole positions for unresolved elements in the array literal.
    std::vector<size_t> holeIndices_;
    std::vector<data_ptr_t> data_;

    // Whether the value is a rank-zero array: no dimension and one element (design principles 5.4
    // -- the rank-zero array is the element itself). The type cannot say it (`f32[..]` takes every
    // rank, and `f32[]` is one dimension of length none), so the value keeps it, and the run makes
    // of it the array with no dimension that a slot of `T[..]` reads.
    bool rankZero_ = false;

    // Used only by ArrayDataFactory::build() to avoid recomputing Type.
    ArrayData(type::Type *arrayType, data_vec_t &&data);

  public:
    ArrayData(type::Type *elemType, data_list_t data = {});
    ArrayData(type::Type *elemType, const data_vec_t &data);
    virtual ~ArrayData() = default;

    static std::shared_ptr<ArrayData> create(type::Type *elemType, data_list_t data = {});
    static std::shared_ptr<ArrayData> from(type::Type *elemType, const data_vec_t &data);
    /// A rank-zero value: a `T[..]` (the only type that takes rank zero) whose one element is
    /// `element` and which has no dimension. It prints and compares as the element itself, so a
    /// program that hands the element to a `T[..]` position sees the value it handed in.
    static std::shared_ptr<ArrayData>
    rankZero(type::ArrayType *arrayType, const data_ptr_t &element);
    bool isRankZero() const { return rankZero_; }

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
    /// The shape the literal writes, when it writes one level per dimension (`[[a, b], [c, d]]`
    /// is [2, 2]): the array is built as one rank-N array of that shape instead of an array of
    /// its elements (design principles 5.1, 5.4). Left unset, the shape is the elements' own
    /// (`elemType[]`), which is what a literal with no stated rank is.
    ArrayDataFactory &setExtents(std::span<const int64_t> extents);
    std::shared_ptr<ArrayData> build();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// The value a position of `type` takes: `value->convertTo(type)`, and on the edge from `T` to
/// `T[..]` the rank-zero value of the element instead (design principles 5.4: a rank-zero array is
/// the element itself, the same value). A literal in such a position becomes that value here, the
/// constant side of the edge the compiler converts at a computed value; the two say the same thing
/// about the same value.
data_ptr_t convertDataTo(const data_ptr_t &value, type::Type *type);

} // namespace camel::core::data
