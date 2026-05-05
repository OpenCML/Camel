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

#include "camel/core/mm/root_handle.h"

#include <string>

// Native/FFI handle ABI for Camel objects. Borrowed handles document short-lived access, rooted
// handles keep moving-GC objects live and updated, and pinned handles additionally require a
// non-moving address before raw native exposure.

namespace camel::core::mm {

class ForeignBorrowedHandle final {
  public:
    ForeignBorrowedHandle(rtdata::Object *object, const type::Type *type) noexcept
        : object_(object), type_(type) {}

    rtdata::Object *get() const noexcept { return object_; }
    template <typename T> T *getAs() const noexcept { return static_cast<T *>(object_); }
    const type::Type *type() const noexcept { return type_; }

  private:
    rtdata::Object *object_ = nullptr;
    const type::Type *type_ = nullptr;
};

class ForeignRootedHandle final {
  public:
    ForeignRootedHandle(
        GenerationalAllocatorWithGC &allocator, rtdata::Object *object, const type::Type *type,
        std::string name = "ForeignRootedHandle");
    ~ForeignRootedHandle();

    ForeignRootedHandle(const ForeignRootedHandle &)            = delete;
    ForeignRootedHandle &operator=(const ForeignRootedHandle &) = delete;
    ForeignRootedHandle(ForeignRootedHandle &&)                 = delete;
    ForeignRootedHandle &operator=(ForeignRootedHandle &&)      = delete;

    rtdata::Object *get() const noexcept { return root_.get(); }
    template <typename T> T *getAs() const noexcept { return root_.getAs<T>(); }
    const type::Type *type() const noexcept { return root_.type(); }

  private:
    RootHandle root_;
};

class ForeignPinnedHandle final {
  public:
    ForeignPinnedHandle(
        GenerationalAllocatorWithGC &allocator, rtdata::Object *object, const type::Type *type,
        std::string name = "ForeignPinnedHandle");
    ~ForeignPinnedHandle();

    ForeignPinnedHandle(const ForeignPinnedHandle &)            = delete;
    ForeignPinnedHandle &operator=(const ForeignPinnedHandle &) = delete;
    ForeignPinnedHandle(ForeignPinnedHandle &&)                 = delete;
    ForeignPinnedHandle &operator=(ForeignPinnedHandle &&)      = delete;

    rtdata::Object *get() const noexcept { return root_.get(); }
    template <typename T> T *getAs() const noexcept { return root_.getAs<T>(); }
    const type::Type *type() const noexcept { return root_.type(); }
    void *address() const noexcept { return pinnedAddress_; }

  private:
    RootHandle root_;
    void *pinnedAddress_ = nullptr;
};

bool debugRunForeignResourceSelfTest(GenerationalAllocatorWithGC &allocator);

} // namespace camel::core::mm
