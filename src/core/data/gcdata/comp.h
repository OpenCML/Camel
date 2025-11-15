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

#include "layout.h"

class GCCompositeObject : public GCObject {
  public:
    explicit GCCompositeObject(
        const CompositeDataLayout *layout, IAllocator &allocator = GlobalGC::instance())
        : layout_(layout), size_(layout->elemCnt()) {
        data_ = allocator.alloc(layout_->size(), layout_->align());
    }

    template <typename T> T &at(size_t index) {
        ASSERT(index < size_, "Index out of range");
        return *reinterpret_cast<T *>(data() + layout_->offsets[index]);
    }

    TypeCode typeCode(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        return layout_->typeCode(index);
    }

    void trace(const std::function<void(GCObject *)> &visit) const override {
        for (size_t i = 0; i < size_; ++i) {
            if (isGCTraced(layout_->typeCode(i))) {
                auto objPtr = *reinterpret_cast<GCObject *const *>(data() + layout_->offset(i));
                if (objPtr) {
                    visit(objPtr);
                }
            }
        }
    }

    ObjectHeader *header() const {
        return reinterpret_cast<ObjectHeader *>(
            reinterpret_cast<uint8_t *>(data_) - sizeof(ObjectHeader));
    }

  protected:
    uint8_t *data() const { return reinterpret_cast<uint8_t *>(data_); }

    const CompositeDataLayout *layout_;
    size_t size_;
    void *data_; // 数据区起始地址
};
