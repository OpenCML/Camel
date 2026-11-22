// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The value of an enum: a tag and the fields of the variant it names, stored in the order
 * written (design principles 5.3). A unit variant has no field. The object does not store its
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
#include <string>

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
        GUARD(
            index < fieldCount_,
            std::string("reading field ") + std::to_string(index) + " of a Variant whose tag " +
                std::to_string(tag_) + " declares " + std::to_string(fieldCount_) +
                " field(s), so the read is past its last one");
        return data_[index];
    }
    void set(size_t index, slot_t value) {
        GUARD(
            index < fieldCount_,
            std::string("writing field ") + std::to_string(index) + " of a Variant whose tag " +
                std::to_string(tag_) + " declares " + std::to_string(fieldCount_) +
                " field(s), so the write is past its last one");
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