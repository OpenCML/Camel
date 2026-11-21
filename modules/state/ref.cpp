// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "ref.h"

#include "camel/utils/assert.h"

namespace camel::statemod {

RefBox::RefBox(type::Type *contentType, slot_t value) : contentType_(contentType) {
    ASSERT(contentType_ != nullptr, "ref: the content has no type");
    set(value);
}

RefBox::~RefBox() = default;

slot_t RefBox::get() const {
    if (root_ != nullptr) {
        return rtdata::toSlot(root_->get());
    }
    return inline_;
}

void RefBox::set(slot_t value) {
    if (!contentType_->isGCTraced()) {
        inline_ = value;
        return;
    }
    // The content is an object: the root is what keeps it alive and what the collector rewrites.
    // Creating or resetting one takes the allocator's lock once, which is the cost of a write to a
    // reference.
    auto *object = rtdata::fromSlot<rtdata::Object *>(value);
    if (object == nullptr) {
        root_.reset();
        inline_ = value;
        return;
    }
    if (root_ == nullptr) {
        root_ = std::make_unique<mm::RootHandle>(mm::autoSpace(), object, contentType_, "state::Ref");
        return;
    }
    root_->reset(object, contentType_);
}

} // namespace camel::statemod