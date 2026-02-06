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
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "frame.h"
#include "core/rtdata/conv.h"

FrameMeta *installFrameMetaInfoForGraph(GraphIR::Graph *graph) {
    const TupleType *runtimeDataType = graph->runtimeDataType();
    const TupleType *staticDataType  = graph->staticDataType();
    Tuple *staticArea                = Tuple::create(staticDataType->size(), mm::permSpace());
    const auto &staticDataArr        = graph->staticDataArr();

    for (size_t i = 1; i < staticDataType->size(); ++i) {
        const auto &elem = staticDataArr[i];
        if (elem->type()->isGCTraced()) {
            Object *elemRef = makeGCRefFromGCTracedData(elem, mm::permSpace());
            staticArea->set<Object *>(i, elemRef);
        } else if (elem->type()->isPrimitive()) {
            slot_t slot = makeSlotFromPrimitiveData(elem);
            staticArea->set<slot_t>(i, slot);
        } else {
            ASSERT(false, "Unsupported element type.");
        }
    }

    FrameMeta *meta       = constructAt<FrameMeta>(mm::metaSpace());
    meta->frameSize       = sizeof(Frame) + sizeof(slot_t) * runtimeDataType->size();
    meta->runtimeDataType = runtimeDataType;
    meta->staticArea      = staticArea;

    graph->setExtra<FrameMeta, 0>(meta);

    return meta;
}
