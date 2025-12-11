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
 * Created: Sep. 16, 2025
 * Updated: Dec. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "frame.h"
#include "core/rtdata/conv.h"

FrameTemplate::FrameTemplate(
    GraphIR::Graph *graph, IAllocator &staticAllocator, IAllocator &runtimeAllocator)
    : graph_(graph), staticAllocator_(staticAllocator), runtimeAllocator_(runtimeAllocator) {
    const auto &layout        = graph_->staticDataType()->layout();
    staticArea_               = Tuple::create(layout, staticAllocator_);
    const auto &staticDataArr = graph_->staticDataArr();
    for (size_t i = 1; i < layout.size(); ++i) {
        const auto &elem = staticDataArr[i];
        if (elem->type()->isGCTraced()) {
            Object *elemRef = makeGCRefFromGCTracedData(elem, staticAllocator_);
            staticArea_->set<Object *>(i, elemRef);
        } else if (elem->type()->isPrimitive()) {
            slot_t slot = makeSlotFromPrimitiveData(elem);
            staticArea_->set<slot_t>(i, slot);
        } else {
            ASSERT(false, "Unsupported element type.");
        }
    }
    runtimeDataLayout_ = &graph_->runtimeDataType()->layout();
}

Tuple *FrameTemplate::makeDynamicArea() const {
    return Tuple::create(*runtimeDataLayout_, runtimeAllocator_);
}
