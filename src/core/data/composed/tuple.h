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
 * Updated: Nov. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "composed.h"

class TupleData : public ComposedData {
  private:
    std::vector<size_t> refIndices_;
    std::vector<data_ptr_t> data_;

  public:
    TupleData(data_list_t data = {});
    TupleData(type_ptr_t type, data_vec_t &&data);
    virtual ~TupleData() = default;

    static std::shared_ptr<TupleData> create(data_list_t data = {}) {
        return std::make_shared<TupleData>(data);
    }
    static std::shared_ptr<TupleData> create(type_ptr_t type, data_vec_t &&data) {
        return std::make_shared<TupleData>(type, std::move(data));
    }

    void emplace(const data_ptr_t &e);

    data_ptr_t get(size_t index) const;
    std::vector<data_ptr_t> &raw() { return data_; }
    size_t size() const { return data_.size(); }

    virtual std::vector<std::string> refs() const override;
    virtual bool resolved() const override { return refIndices_.empty(); }
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual void print(std::ostream &os) const override;
};

class VariantTuple {
  public:
    VariantTuple(const TypeCode *typeCodes, size_t count)
        : count_(count), typeCodes_(typeCodes, typeCodes + count) {
        // 计算偏移
        offsets_.resize(count_);
        size_t offset = 0;
        for (size_t i = 0; i < count_; ++i) {
            auto layout = getLayoutOfType(typeCodes_[i]);
            offset      = alignUp(offset, layout.align);
            offsets_[i] = offset;
            offset += layout.size;
        }
        totalSize_ = offset;

        // GC 分配连续内存
        dataBlock_ = static_cast<uint8_t *>(
            GlobalGC::instance().allocate(totalSize_, alignof(std::max_align_t)));
    }

    template <typename T> T &get(size_t index) {
        assert(index < count_);
        auto layout = getLayoutOfType(typeCodes_[index]);
        assert(layout.size == sizeof(T) && layout.align == alignof(T));
        return *reinterpret_cast<T *>(dataBlock_ + offsets_[index]);
    }

    template <typename T> const T &get(size_t index) const {
        assert(index < count_);
        auto layout = getLayoutOfType(typeCodes_[index]);
        assert(layout.size == sizeof(T) && layout.align == alignof(T));
        return *reinterpret_cast<const T *>(dataBlock_ + offsets_[index]);
    }

    size_t size() const { return count_; }
    const std::vector<TypeCode> &typeCodes() const { return typeCodes_; }
    uint8_t *rawData() { return dataBlock_; }
    const uint8_t *rawData() const { return dataBlock_; }

  private:
    static size_t alignUp(size_t offset, size_t align) {
        size_t remainder = offset % align;
        return remainder == 0 ? offset : offset + (align - remainder);
    }

    size_t count_;
    std::vector<TypeCode> typeCodes_;
    std::vector<size_t> offsets_;
    size_t totalSize_;
    uint8_t *dataBlock_; // GC 管理的裸内存
};
