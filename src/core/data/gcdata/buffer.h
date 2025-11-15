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
 * Created: Nov. 07, 2025
 * Updated: Nov. 15, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

class GCBuffer : public GCObject {
  public:
    explicit GCBuffer(size_t size, size_t align, IAllocator &allocator = GlobalGC::instance())
        : size_(size) {
        if (size == 0) {
            data_ = nullptr;
            return;
        }
        data_ = allocator.alloc(size, align);
    }
    ~GCBuffer() override = default;

    ObjectHeader *header() const override {
        if (!data_) {
            return nullptr;
        }
        return reinterpret_cast<ObjectHeader *>(static_cast<char *>(data_)) - 1;
    }

    void trace(const std::function<void(GCObject *)> &visit) const override {}

    void *data() const { return data_; }
    size_t size() const { return size_; }

    template <typename T> T *as() const { return reinterpret_cast<T *>(data_); }

    void clear() {
        if (data_) {
            std::memset(data_, 0, size_);
        }
    }

  private:
    void *data_;  // 指向实际数据区（header 之后）
    size_t size_; // 数据区大小
};
