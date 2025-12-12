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
 * Updated: Dec. 13, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "frame.h"
#include "core/rtdata/conv.h"

FrameMeta *installFrameMetaInfoForGraph(GraphIR::Graph *graph) {
    // Graph 的静态数据直接放在持久数据区，永不释放
    const auto &layout        = graph->staticDataType()->layout();
    Tuple *staticArea         = Tuple::create(layout, mm::permSpace());
    const auto &staticDataArr = graph->staticDataArr();

    for (size_t i = 1; i < layout.size(); ++i) {
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

    // 将数据存储到元数据区
    FrameMeta *meta         = constructAt<FrameMeta>(mm::metaSpace());
    meta->runtimeAreaLayout = &graph->runtimeDataType()->layout();
    meta->staticArea        = staticArea;

    // 安装到 Graph 的 extra 中以便于查找
    graph->setExtra<FrameMeta, 0>(meta);

    return meta;
}
