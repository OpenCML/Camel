// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/mm/root_handle.h"

#include <utility>

// RootHandle adapts one C++ local pointer into the allocator's named external-root registry.

namespace camel::core::mm {

RootHandle::RootHandle(GenerationalAllocatorWithGC &allocator, std::string name)
    : allocator_(&allocator), name_(name.empty() ? "RootHandle" : std::move(name)) {
    allocator_->linkRootHandle(this);
}

RootHandle::RootHandle(
    GenerationalAllocatorWithGC &allocator, rtdata::Object *object, const type::Type *type,
    std::string name)
    : RootHandle(allocator, std::move(name)) {
    reset(object, type);
}

RootHandle::~RootHandle() {
    if (allocator_) {
        allocator_->unlinkRootHandle(this);
    }
}

void RootHandle::reset(rtdata::Object *object, const type::Type *type) {
    object_ = object;
    type_   = type;
}

void RootHandle::trace(const GenerationalAllocatorWithGC::RefRelocator &relocate) {
    if (!object_) {
        return;
    }
    object_ = relocate(
        object_,
        type_,
        rtdata::RefTraceInfo{
            .owner     = nullptr,
            .ownerType = nullptr,
            .slotType  = type_,
            .ownerKind = "RootHandle",
            .slotName  = name_,
            .slotIndex = rtdata::RefTraceInfo::npos,
        });
}

} // namespace camel::core::mm
