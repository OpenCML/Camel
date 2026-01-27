/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 06, 2024
 * Updated: Jan. 27, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "composite.h"
#include "utils/assert.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>

class StructTypeLayout {
  public:
    StructTypeLayout(
        std::vector<std::pair<std::string, TypeCode>> fields,
        const std::vector<std::string> &refs) {
        fieldNames_.reserve(fields.size());
        fieldTypes_.reserve(fields.size());
        refs_.reserve(refs.size());

        for (auto &f : fields) {
            fieldNames_.push_back(f.first);
            fieldTypes_.push_back(f.second);
        }

        for (auto &name : refs) {
            auto it = std::find(fieldNames_.begin(), fieldNames_.end(), name);
            ASSERT(it != fieldNames_.end(), "Name not found.");
            refs_.push_back(static_cast<size_t>(std::distance(fieldNames_.begin(), it)));
        }
    }

    size_t fieldCount() const noexcept { return fieldNames_.size(); }

    std::string_view fieldName(size_t index) const {
        ASSERT(index < fieldTypes_.size(), "Index out of range");
        return fieldNames_[index];
    }

    TypeCode fieldType(size_t index) const {
        ASSERT(index < fieldTypes_.size(), "Index out of range");
        return fieldTypes_[index];
    }

    std::optional<size_t> findField(std::string_view name) const {
        for (size_t i = 0; i < fieldNames_.size(); ++i) {
            if (fieldNames_[i] == name) {
                return i;
            }
        }
        return std::nullopt;
    }

    const std::vector<std::string> &fieldNames() const { return fieldNames_; }
    const std::vector<TypeCode> &fieldTypes() const { return fieldTypes_; }

    const std::vector<size_t> &refs() const { return refs_; }

  private:
    std::vector<std::string> fieldNames_;
    std::vector<TypeCode> fieldTypes_;
    std::vector<size_t> refs_;
};

class StructType : public CompositeType {
  private:
    std::vector<std::string> refs_;
    std::map<std::string, Type *> fields_;
    mutable std::shared_ptr<StructTypeLayout> layout_;

    void computeLayout() const;

  public:
    StructType();
    ~StructType() noexcept override = default;

    static StructType *create();

    size_t size() const;
    std::optional<Type *> typeOf(const std::string &idx) const;
    bool add(const std::string &name, Type *type);
    bool has(const std::string &name) const;
    std::optional<Type *> get(const std::string &name) const;
    std::optional<Type *> get(const size_t &idx) const;
    std::optional<size_t> findField(const std::string_view &name) const;
    const StructTypeLayout &layout() const;

    Type *operator|(const StructType &other) const;
    Type *operator&(const StructType &other) const;

    virtual Type *resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual Type *clone(bool deep = false) const override;
    virtual bool equals(Type *type) const override;
    virtual CastSafety castSafetyTo(const Type &other) const override;
    virtual bool assignable(Type *type) const override;
};
