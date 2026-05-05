/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 05, 2026
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/mm/root_handle.h"

#include <utility>

// RootHandle adapts one C++ local pointer into the allocator's named external-root registry.

namespace camel::core::mm {

RootHandle::RootHandle(GenerationalAllocatorWithGC &allocator, std::string name)
    : allocator_(&allocator), name_(name.empty() ? "RootHandle" : std::move(name)) {
    allocator_->registerExternalRootTracer(
        this,
        name_,
        [this](const GenerationalAllocatorWithGC::RefRelocator &relocate) { trace(relocate); });
}

RootHandle::RootHandle(
    GenerationalAllocatorWithGC &allocator, rtdata::Object *object, const type::Type *type,
    std::string name)
    : RootHandle(allocator, std::move(name)) {
    reset(object, type);
}

RootHandle::~RootHandle() {
    if (allocator_) {
        allocator_->unregisterExternalRootTracer(this);
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
