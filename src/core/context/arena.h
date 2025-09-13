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
 * Created: Jul. 21, 2025
 * Updated: Jul. 21, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <memory>
#include <vector>

#include "core/data/data.h"
#include "utils/assert.h"

enum class DataTypeEnum {
    StaticConstant =
        0b00, // Compile-time constant, shared among all copies of the graph and never changed
    StaticVariable =
        0b01, // Compile-time variable, shared among graphs and may be changed during runtime
    RuntimeConstant = 0b10, // Produced during runtime and never changed, not shared
    RuntimeVariable = 0b11, // Produced during runtime and may be changed, not shared
};

struct DataType {
    bool shared;
    bool constant;

    constexpr DataType(bool s = true, bool c = true) : shared(s), constant(c) {}
    constexpr DataType(DataTypeEnum type) {
        switch (type) {
        case DataTypeEnum::StaticConstant:
            shared = true;
            constant = true;
            break;
        case DataTypeEnum::StaticVariable:
            shared = true;
            constant = false;
            break;
        case DataTypeEnum::RuntimeConstant:
            shared = false;
            constant = true;
            break;
        case DataTypeEnum::RuntimeVariable:
            shared = false;
            constant = false;
            break;
        }
    }

    bool operator==(const DataType &other) const {
        return shared == other.shared && constant == other.constant;
    }
    DataType &operator=(DataTypeEnum type) {
        switch (type) {
        case DataTypeEnum::StaticConstant:
            shared = true;
            constant = true;
            break;
        case DataTypeEnum::StaticVariable:
            shared = true;
            constant = false;
            break;
        case DataTypeEnum::RuntimeConstant:
            shared = false;
            constant = true;
            break;
        case DataTypeEnum::RuntimeVariable:
            shared = false;
            constant = false;
            break;
        }
        return *this;
    }

    operator std::string() const {
        if (shared && constant) {
            return "SRD./CST.";
        } else if (shared && !constant) {
            return "SRD./VAR.";
        } else if (!shared && constant) {
            return "RTM./CST.";
        } else {
            return "RTM./VAR.";
        }
    }
};

struct DataIndex {
    DataType type;
    size_t index;
};

class DataArray;
class DataArena;

using array_ptr_t = std::shared_ptr<DataArray>;
using array_wptr_t = std::weak_ptr<DataArray>;
using arena_ptr_t = std::shared_ptr<DataArena>;
using arena_wptr_t = std::weak_ptr<DataArena>;

class DataArray : public std::enable_shared_from_this<DataArray> {
  public:
    DataArray(DataType type) : type_(type) {}
    virtual ~DataArray() = default;

    DataType type() const { return type_; }
    size_t size() const { return dataArr_.size(); }

    virtual void set(const data_ptr_t &data, size_t index) = 0;
    virtual data_ptr_t get(size_t index) = 0;
    virtual bool has(size_t index) { return index < dataArr_.size() && dataArr_[index] != nullptr; }

    virtual array_ptr_t clone() = 0;

  protected:
    DataType type_;
    data_vec_t dataArr_;
};

class ConstantArray : public DataArray {
  public:
    ConstantArray(bool shared) : DataArray(DataType(shared, true)) {}

    DataIndex emplace(const data_ptr_t &data) {
        // TODO: Uncomment the assertion when data validation is needed
        // ASSERT(data != nullptr, "Cannot emplace null data in a constant array.");
        dataArr_.push_back(data);
        return {type_, dataArr_.size() - 1};
    }

    void set(const data_ptr_t &data, size_t index) override {
        ASSERT(false, "Cannot set data in a constant array.");
    }
    data_ptr_t get(size_t index) override {
        ASSERT(index < dataArr_.size(), "Data index out of bounds.");
        data_ptr_t data = dataArr_[index];
        ASSERT(data != nullptr, "Accessing not-initialized data in a constant array.");
        return data;
    }

    array_ptr_t clone() override { return shared_from_this(); }
};

class VariableArray : public DataArray {
  public:
    VariableArray(bool shared) : DataArray(DataType(shared, false)) {}
    VariableArray(bool shared, const array_wptr_t &refs)
        : DataArray(DataType(shared, false)), refs_(refs) {}

    DataIndex emplace(DataIndex constIndex) {
        ASSERT(refs_.lock(), "Cannot emplace data in a variable array without references.");
        ASSERT(indices_, "Indices vector is not initialized.");
        ASSERT(constIndex.type.constant, "Cannot emplace non-constant data in a variable array.");
        ASSERT(constIndex.type.shared == type_.shared, "Data type mismatch in variable array.");

        size_t index = indices_->size();
        indices_->push_back(constIndex.index);
        dataArr_.resize(index + 1, nullptr);
        return {type_, index};
    }

    void set(const data_ptr_t &data, size_t index) override {
        ASSERT(data != nullptr, "Cannot set null data in a variable array.");
        dataArr_[index] = data;
    }
    data_ptr_t get(size_t index) override {
        ASSERT(index < dataArr_.size(), "Data index out of bounds.");
        data_ptr_t data = dataArr_[index];
        if (data == nullptr) {
            array_ptr_t refs = refs_.lock();
            ASSERT(refs, "Initializing data from a null reference array.");
            size_t ref_index = indices_->at(index);
            ASSERT(ref_index < refs->size(), "Reference index out of bounds.");
            data = refs->get(ref_index)->clone(true);
            dataArr_[index] = data; // Cache the data for future access
        }
        return data;
    }

    array_ptr_t clone() override {
        auto cloned = std::make_shared<VariableArray>(type_.shared);
        cloned->refs_ = refs_;
        cloned->dataArr_.reserve(dataArr_.size());
        cloned->indices_ = indices_;
        return cloned;
    }

  private:
    std::shared_ptr<std::vector<size_t>> indices_;
    array_wptr_t refs_;
};

class DataArena : public std::enable_shared_from_this<DataArena> {
  public:
    DataArena() {
        sharedConstants_ = std::make_shared<ConstantArray>(true);
        sharedVariables_ = std::make_shared<VariableArray>(true, sharedConstants_);
        runtimeConstants_ = std::make_shared<ConstantArray>(false);
        runtimeVariables_ = std::make_shared<VariableArray>(false, runtimeConstants_);
    }
    DataArena(
        std::shared_ptr<ConstantArray> sharedConsts, std::shared_ptr<VariableArray> sharedVars) {
        sharedConstants_ = sharedConsts;
        sharedVariables_ = sharedVars;
        runtimeConstants_ = std::make_shared<ConstantArray>(false);
        runtimeVariables_ = std::make_shared<VariableArray>(false, runtimeConstants_);
    }

    DataIndex addConstant(const data_ptr_t &data, bool shared) {
        if (shared) {
            return sharedConstants_->emplace(data);
        } else {
            return runtimeConstants_->emplace(data);
        }
    }

    DataIndex addVariable(DataIndex index) {
        ASSERT(index.type.constant, "Cannot insert non-constant data as a variable.");
        if (index.type.shared) {
            return sharedVariables_->emplace(index);
        } else {
            return runtimeVariables_->emplace(index);
        }
    }

    void set(const DataIndex &index, const data_ptr_t &data) {
        if (index.type.shared) {
            if (index.type.constant) {
                sharedConstants_->set(data, index.index);
            } else {
                sharedVariables_->set(data, index.index);
            }
        } else {
            if (index.type.constant) {
                runtimeConstants_->set(data, index.index);
            } else {
                runtimeVariables_->set(data, index.index);
            }
        }
    }
    data_ptr_t get(const DataIndex &index) {
        if (index.type.shared) {
            if (index.type.constant) {
                return sharedConstants_->get(index.index);
            } else {
                return sharedVariables_->get(index.index);
            }
        } else {
            if (index.type.constant) {
                return runtimeConstants_->get(index.index);
            } else {
                return runtimeVariables_->get(index.index);
            }
        }
    }
    bool has(const DataIndex &index) {
        if (index.type.shared) {
            if (index.type.constant) {
                return sharedConstants_->has(index.index);
            } else {
                return sharedVariables_->has(index.index);
            }
        } else {
            if (index.type.constant) {
                return runtimeConstants_->has(index.index);
            } else {
                return runtimeVariables_->has(index.index);
            }
        }
    }

    arena_ptr_t clone() { return std::make_shared<DataArena>(sharedConstants_, sharedVariables_); }

  private:
    std::shared_ptr<ConstantArray> sharedConstants_;
    std::shared_ptr<VariableArray> sharedVariables_;
    std::shared_ptr<ConstantArray> runtimeConstants_;
    std::shared_ptr<VariableArray> runtimeVariables_;
};