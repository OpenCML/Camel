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
    virtual ~GCObject()                                                    = default;
    virtual ObjectHeader *header() const                                   = 0;
    virtual void trace(const std::function<void(GCObject *)> &visit) const = 0;
};

class GarbageCollector {
    std::vector<GCObject *> rootSet_; // 根对象集合

  public:
    void markRoot(GCObject *obj) { rootSet_.push_back(obj); }
    void traverse(const std::function<void(ObjectHeader *)> &headerVisitor) {
        // 对于每个 GCObject，先访问其 header，然后递归 trace 内部引用
        auto objectVisitor = [&headerVisitor](this auto &&self, GCObject *obj) -> void {
            if (ObjectHeader *header = obj->header()) {
                headerVisitor(header);
            }
            obj->trace(self);
        };

        for (auto *root : rootSet_) {
            // 不把root自己的header纳入进来
            // 假定root不会分配在GC管理的堆区域内
            // 因此其本身的内存无需GC介入整理
            root->trace(objectVisitor);
        }
    }
};
