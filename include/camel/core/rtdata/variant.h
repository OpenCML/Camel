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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The value of an enum: a tag and the fields of the variant it names, stored in the order
 * written (design principles 2.13). A unit variant has no field. The object does not store its
 * static type: every method is given it, which is how the collector traces the fields with the
 * types of the variant the value happens to name.
 *
 * The fields are stored inline, as a tuple's are, so constructing and matching a recursive
 * `Tree` allocates one object per node and none per field taken out of it: the extraction
 * operator reads a slot and copies nothing.
 */

#pragma once

#include "base.h"

#include "camel/utils/assert.h"

#include <cstdint>
#include <new>

namespace camel::core::rtdata {

class Variant : public Object {
  public:
    Variant(const Variant &)            = delete;
    Variant &operator=(const Variant &) = delete;

    static Variant *
    create(uint32_t tag, size_t fieldCount, camel::core::mm::IAllocator &allocator) {
        const size_t totalSize = sizeof(Variant) + sizeof(slot_t) * fieldCount;
        void *memory           = allocator.alloc(totalSize, alignof(Variant));
        if (!memory) {
            throw std::bad_alloc();
        }
        Variant *variant = new (memory) Variant(tag, fieldCount);
        return variant;
    }

    /// The position of the variant in its declaration.
    uint32_t tag() const { return tag_; }
    size_t fieldCount() const { return fieldCount_; }

    slot_t get(size_t index) const {
        ASSERT(index < fieldCount_, "Variant field index out of range");
        return data_[index];
    }
    void set(size_t index, slot_t value) {
        ASSERT(index < fieldCount_, "Variant field index out of range");
        data_[index] = value;
    }
    slot_t *data() { return data_; }
    const slot_t *data() const { return data_; }

    bool equals(const Object *other, const type::Type *type, bool deep = false) const override;

    Object *clone(camel::core::mm::IAllocator &allocator, const type::Type *type, bool deep = false)
        const override;

    void print(std::ostream &os, const type::Type *type) const override;

    void onMoved() override {}

    void updateRefs(const RefRelocator &relocate, const type::Type *type) override;

  private:
    Variant(uint32_t tag, size_t fieldCount)
        : tag_(tag), fieldCount_(static_cast<uint32_t>(fieldCount)) {}

    uint32_t tag_;
    uint32_t fieldCount_;
    slot_t data_[];
};

static_assert(sizeof(Variant) == sizeof(void *) + 2 * sizeof(uint32_t), "Variant header grew.");

} // namespace camel::core::rtdata