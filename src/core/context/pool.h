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
 * Created: Dec. 11, 2025
 * Updated: Dec. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

// #include "compile/gir.h"
// #include "core/rtdata/data.h"
// #include "utils/brpred.h"
// #include "utils/log.h"

// class Frame : public Object {
//   public:
//     Frame(Tuple *staticArea, const TupleTypeLayout *runtimeLayout, IAllocator &allocator)
//         : staticArea_(staticArea), runtimeLayout_(runtimeLayout),
//           runtimeSize_(runtimeLayout_->size()) {
//         // 在连续内存中分配动态区
//         dynamicData_ = reinterpret_cast<slot_t *>(this + 1);

//         // 初始化动态区（默认 NullRef 或零）
//         for (size_t i = 0; i < runtimeSize_; ++i) {
//             if (isGCTraced(runtimeLayout_->typeAt(i))) {
//                 dynamicData_[i] = toSlot(NullRef);
//             } else {
//                 dynamicData_[i] = {};
//             }
//         }
//     }

//     Tuple *staticArea() const { return staticArea_; }

//     template <typename T> T getDynamic(size_t idx) const { return fromSlot<T>(dynamicData_[idx]);
//     }

//     template <typename T> void setDynamic(size_t idx, T value) {
//         dynamicData_[idx] = toSlot(value);
//     }

//     virtual void updateRefs(const std::function<Object *(Object *)> &relocate) override {
//         Object **refArr = reinterpret_cast<Object **>(dynamicData_);
//         for (size_t i = 0; i < runtimeSize_; ++i) {
//             if (isGCTraced(runtimeLayout_->typeAt(i)) && refArr[i]) {
//                 refArr[i] = relocate(refArr[i]);
//             }
//         }
//     }

//   private:
//     Tuple *staticArea_;                    // 共享静态区
//     const TupleTypeLayout *runtimeLayout_; // 动态区布局
//     size_t runtimeSize_;                   // 动态区大小
//     slot_t *dynamicData_;                  // 动态区起始地址
// };
