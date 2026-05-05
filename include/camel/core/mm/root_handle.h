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

#pragma once

#include "camel/core/mm/gen.h"

#include <string>

// RAII root for C++ locals that hold auto-space objects across explicit safepoints.

namespace camel::core::mm {

class RootHandle final {
  public:
    explicit RootHandle(GenerationalAllocatorWithGC &allocator, std::string name = "RootHandle");
    RootHandle(
        GenerationalAllocatorWithGC &allocator, rtdata::Object *object, const type::Type *type,
        std::string name = "RootHandle");
    ~RootHandle();

    RootHandle(const RootHandle &)            = delete;
    RootHandle &operator=(const RootHandle &) = delete;
    RootHandle(RootHandle &&)                 = delete;
    RootHandle &operator=(RootHandle &&)      = delete;

    rtdata::Object *get() const noexcept { return object_; }

    template <typename T> T *getAs() const noexcept { return static_cast<T *>(object_); }

    const type::Type *type() const noexcept { return type_; }

    void reset(rtdata::Object *object, const type::Type *type);

  private:
    void trace(const GenerationalAllocatorWithGC::RefRelocator &relocate);

    GenerationalAllocatorWithGC *allocator_ = nullptr;
    rtdata::Object *object_                 = nullptr;
    const type::Type *type_                 = nullptr;
    std::string name_;
};

} // namespace camel::core::mm
