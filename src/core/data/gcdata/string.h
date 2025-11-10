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
 * Updated: Nov. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

class GCString : public GCObject {
  public:
    using StringType = std::basic_string<char, std::char_traits<char>, GCAllocator<char>>;

    GCString() : str_(GCAllocator<char>{}) {}

    GCString(const char *s) : str_(s, GCAllocator<char>{}) {}

    GCString(const std::string &s) : str_(s.begin(), s.end(), GCAllocator<char>{}) {}

    GCString(std::string &&s) : str_(std::move(s), GCAllocator<char>{}) {}

    void trace(const std::function<void(GCObject *)> &visit) const override {
        // 普通字符数据无 GC 引用，这里不调用 visit
    }

    ObjectHeader *header() const override {
        const char *dataPtr = str_.data();
        const char *strPtr  = reinterpret_cast<const char *>(&str_);

        // 如果 data() 在 std::string 对象自身内存范围内 => SSO
        if (dataPtr >= strPtr && dataPtr < strPtr + sizeof(std::string)) {
            return nullptr;
        }

        // 否则认为是 GC 分配的堆内存
        return reinterpret_cast<ObjectHeader *>(const_cast<char *>(dataPtr) - sizeof(ObjectHeader));
    }

    const StringType &str() const { return str_; }
    StringType &str() { return str_; }

  private:
    StringType str_;
};
