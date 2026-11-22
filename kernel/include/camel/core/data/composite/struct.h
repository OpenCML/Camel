// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/data/composite/composite.h"

#include <map>
#include <memory>

namespace camel::core::data {

class StructDataFactory;

class StructData : public CompositeData {
    friend class StructDataFactory;

  private:
    std::vector<std::string> holeFields_;
    // Store fields in lexicographic order.
    std::map<std::string, data_ptr_t> data_;

    // Used only by StructDataFactory::build() to avoid recomputing Type.
    StructData(
        type::Type *type, std::map<std::string, data_ptr_t> &&data,
        std::vector<std::string> &&holeFields);

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
    virtual std::vector<size_t> holes() const override;
    virtual bool resolved() const override { return holeFields_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual data_ptr_t convertTo(type::Type *type) override;
};

// Factory: collect fields and build Type and StructData once to avoid recomputing Type.
class StructDataFactory {
  public:
    StructDataFactory();
    ~StructDataFactory();

    StructDataFactory &add(const std::string &key, const data_ptr_t &val);
    std::shared_ptr<StructData> build();
    /// Builds the literal as the struct type `type`: every field of the type has a value, by
    /// name, and no other field is present. The fields are in the type's order and the data's
    /// type is `type` itself — a named struct type stays named, which is what a literal at a
    /// position of that type is (design principles 5.2). Null when a field is missing or
    /// extra.
    std::shared_ptr<StructData> buildAs(type::StructType *type);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace camel::core::data
