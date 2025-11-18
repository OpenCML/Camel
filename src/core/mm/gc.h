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
 * Updated: Nov. 16, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "alloc/header.h"

#include <functional>

class GCObject {
  public:
    virtual ~GCObject()                                                       = default;
    virtual void *payload() const                                             = 0;
    virtual void onMoved(void *to)                                            = 0;
    virtual void traverse(const std::function<void(GCObject *)> &visit) const = 0;
};

// 虽然类名是 GarbageCollector（垃圾回收器），但这只是历史命名和便于理解的妥协。
// 本类的职责并不是释放或回收内存，而是从根集合出发，遍历并收集所有可达对象。
//
// 需要注意：
// 1. 它不进行实际的垃圾回收（即不释放不可达对象的内存）——这部分由外部的分配器根据自身情况完成。
// 2. 出于性能考量，它收集的是可达对象（存活对象），这些对象严格来说并不是“垃圾”。
//
// 因此更准确的称呼应该是“可达性遍历器”或“存活对象枚举器”。
// 典型用途是在 GC 的“标记阶段”配合外部释放逻辑使用。
class GarbageCollector {
    std::vector<GCObject *> rootSet_; // 根对象集合

  public:
    void markRoot(GCObject *obj) { rootSet_.push_back(obj); }

    void traverse(const std::function<void(ObjectHeader *)> &headerVisitor) {
        // 对于每个 GCObject，先访问其 header，然后递归 traverse 内部引用
        auto objectVisitor = [&headerVisitor](this auto &&self, GCObject *obj) -> void {
            if (void *payload = obj->payload()) {
                headerVisitor(headerOf(payload));
            }
            obj->traverse(self);
        };

        for (auto *root : rootSet_) {
            // 不把root自己的header纳入进来
            // 假定root不会分配在GC管理的堆区域内
            // 因此其本身的内存无需GC介入整理
            root->traverse(objectVisitor);
        }
    }
};
