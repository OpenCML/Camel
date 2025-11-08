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
 * Created: Nov. 11, 2025
 * Updated: Oct. 28, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

struct TypeInfo {
    size_t size;
    size_t align;
};

TypeInfo getTypeInfo(TypeCode code);

#include <cstddef>
#include <cstdint>
#include <vector>
#include <cassert>
#include <cstring> // memcpy
#include <utility>

class VariantTuple {
public:
    VariantTuple(const TypeCode* typeCodes, size_t count)
        : count_(count), typeCodes_(typeCodes) 
    {
        assert(count > 0);

        // 计算总大小和偏移
        offsets_.resize(count_);
        size_t offset = 0;
        for (size_t i = 0; i < count_; ++i) {
            TypeInfo ti = getTypeInfo(typeCodes_[i]);
            offset = alignUp(offset, ti.align);
            offsets_[i] = offset;
            offset += ti.size;
        }
        totalSize_ = offset;

        // 一次性分配连续内存
        dataBlock_ = new uint8_t[totalSize_];
    }

    // 拷贝构造
    VariantTuple(const VariantTuple& other)
        : count_(other.count_), 
          typeCodes_(other.typeCodes_),
          offsets_(other.offsets_),
          totalSize_(other.totalSize_)
    {
        dataBlock_ = new uint8_t[totalSize_];
        std::memcpy(dataBlock_, other.dataBlock_, totalSize_);
    }

    // 移动构造
    VariantTuple(VariantTuple&& other) noexcept
        : count_(other.count_), 
          typeCodes_(other.typeCodes_),
          offsets_(std::move(other.offsets_)),
          totalSize_(other.totalSize_),
          dataBlock_(other.dataBlock_)
    {
        other.dataBlock_ = nullptr;
        other.totalSize_ = 0;
        other.count_ = 0;
    }

    // 拷贝赋值
    VariantTuple& operator=(const VariantTuple& other) {
        if (this != &other) {
            delete[] dataBlock_;
            count_ = other.count_;
            typeCodes_ = other.typeCodes_;
            offsets_ = other.offsets_;
            totalSize_ = other.totalSize_;
            dataBlock_ = new uint8_t[totalSize_];
            std::memcpy(dataBlock_, other.dataBlock_, totalSize_);
        }
        return *this;
    }

    // 移动赋值
    VariantTuple& operator=(VariantTuple&& other) noexcept {
        if (this != &other) {
            delete[] dataBlock_;
            count_ = other.count_;
            typeCodes_ = other.typeCodes_;
            offsets_ = std::move(other.offsets_);
            totalSize_ = other.totalSize_;
            dataBlock_ = other.dataBlock_;
            other.dataBlock_ = nullptr;
            other.totalSize_ = 0;
            other.count_ = 0;
        }
        return *this;
    }

    ~VariantTuple() {
        delete[] dataBlock_; // 注意：仅释放原始内存，不调用析构函数
    }

    template <typename T>
    T& get(size_t index) {
        assert(index < count_);
        TypeInfo ti = getTypeInfo(typeCodes_[index]);
        assert(ti.size == sizeof(T) && ti.align == alignof(T) && "Type mismatch in get()");
        return *reinterpret_cast<T*>(dataBlock_ + offsets_[index]);
    }

    size_t size() const { return count_; }
    const TypeCode* typeCodes() const { return typeCodes_; }

private:
    static size_t alignUp(size_t offset, size_t align) {
        size_t remainder = offset % align;
        return remainder == 0 ? offset : offset + (align - remainder);
    }

    size_t count_;
    const TypeCode* typeCodes_;
    std::vector<size_t> offsets_;
    size_t totalSize_;
    uint8_t* dataBlock_;
};
