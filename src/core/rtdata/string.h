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

#include "base.h"

struct UseGlobalGC {};
struct UseCustomAllocator {};

// 基础分配器模板
template <typename T, typename Policy = UseGlobalGC> class GCAllocator;

// 特化1：默认分配器（无状态开销，默认使用 GlobalGC）
template <typename T> class GCAllocator<T, UseGlobalGC> {
  public:
    using value_type      = T;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;
    using is_always_equal = std::true_type;

    GCAllocator() noexcept = default;

    template <typename U> GCAllocator(const GCAllocator<U, UseGlobalGC> &) noexcept {}

    T *allocate(size_type n) { return static_cast<T *>(mm::autoSpace().alloc(n * sizeof(T))); }

    void deallocate(T *p, size_type n) noexcept {
        // GC 管理，不需要手动释放
    }

    template <typename U> struct rebind {
        using other = GCAllocator<U, UseGlobalGC>;
    };

    bool operator==(const GCAllocator &) const noexcept { return true; }
    bool operator!=(const GCAllocator &) const noexcept { return false; }
};

// 特化2：自定义状态分配器（需要保存具体使用的分配器）
template <typename T> class GCAllocator<T, UseCustomAllocator> {
  public:
    using value_type      = T;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;
    using is_always_equal = std::false_type;

    explicit GCAllocator(IAllocator *allocator = nullptr) noexcept
        : allocator_(allocator ? allocator : &mm::autoSpace()) {}

    template <typename U>
    GCAllocator(const GCAllocator<U, UseCustomAllocator> &other) noexcept
        : allocator_(other.allocator_) {}

    T *allocate(size_type n) { return static_cast<T *>(allocator_->alloc(n * sizeof(T))); }

    void deallocate(T *p, size_type n) noexcept {
        // GC 管理
    }

    template <typename U> struct rebind {
        using other = GCAllocator<U, UseCustomAllocator>;
    };

    IAllocator *allocator() const noexcept { return allocator_; }

    bool operator==(const GCAllocator &other) const noexcept {
        return allocator_ == other.allocator_;
    }

    bool operator!=(const GCAllocator &other) const noexcept { return !(*this == other); }

  private:
    IAllocator *allocator_; // 8 字节开销
};

template <typename Policy = UseGlobalGC> class GCStringT : public GCObject {
  public:
    using AllocatorType = GCAllocator<char, Policy>;
    using StringType    = std::basic_string<char, std::char_traits<char>, AllocatorType>;

    // 默认构造（GlobalGC 策略）
    template <typename P = Policy, typename = std::enable_if_t<std::is_same_v<P, UseGlobalGC>>>
    GCStringT() : str_() {}

    template <typename P = Policy, typename = std::enable_if_t<std::is_same_v<P, UseGlobalGC>>>
    explicit GCStringT(const char *s) : str_(s) {}

    // 从 std::string 构造（GlobalGC 策略）
    template <typename P = Policy, typename = std::enable_if_t<std::is_same_v<P, UseGlobalGC>>>
    explicit GCStringT(const std::string &s) : str_(s.begin(), s.end()) {}

    // 自定义分配器构造（CustomAllocator 策略）
    template <
        typename P = Policy, typename = std::enable_if_t<std::is_same_v<P, UseCustomAllocator>>>
    explicit GCStringT(IAllocator *allocator) : str_(AllocatorType(allocator)) {}

    template <
        typename P = Policy, typename = std::enable_if_t<std::is_same_v<P, UseCustomAllocator>>>
    GCStringT(const char *s, IAllocator *allocator) : str_(s, AllocatorType(allocator)) {}

    // 从 std::string 构造（CustomAllocator 策略）
    template <
        typename P = Policy, typename = std::enable_if_t<std::is_same_v<P, UseCustomAllocator>>>
    GCStringT(const std::string &s, IAllocator *allocator)
        : str_(s.begin(), s.end(), AllocatorType(allocator)) {}

    // 不允许从标准string移动构造（因为分配器不同）
    explicit GCStringT(std::string &&s) = delete;

    ObjectHeader *header() const override {
        if (isSSO()) {
            return nullptr;
        }
        return reinterpret_cast<ObjectHeader *>(const_cast<char *>(str_.data())) - 1;
    }

    void trace(const std::function<void(GCObject *)> &visit) const override {
        // 普通字符数据无 GC 引用
    }

    const StringType &str() const { return str_; }
    StringType &str() { return str_; }
    const char *c_str() const { return str_.c_str(); }
    size_t length() const { return str_.length(); }
    bool empty() const { return str_.empty(); }
    size_t size() const { return str_.size(); }
    size_t capacity() const { return str_.capacity(); }

    bool operator==(const GCString &other) const { return str_ == other.str_; }
    bool operator!=(const GCString &other) const { return str_ != other.str_; }
    bool operator<(const GCString &other) const { return str_ < other.str_; }

    // 获取分配器（仅 CustomAllocator 版本）
    template <typename P = Policy>
    std::enable_if_t<std::is_same_v<P, UseCustomAllocator>, IAllocator *> allocator() const {
        return str_.get_allocator().allocator();
    }

  private:
    StringType str_;

    bool isSSO() const {
        const char *dataPtr = str_.data();
        const char *strPtr  = reinterpret_cast<const char *>(&str_);
        return dataPtr >= strPtr && dataPtr < strPtr + sizeof(StringType);
    }
};

using GCString              = GCStringT<UseGlobalGC>;
using GCStringWithAllocator = GCStringT<UseCustomAllocator>;

static_assert(
    sizeof(GCString) == sizeof(std::string) + 8,
    "GCString size should match std::string size (and a vptr)");
static_assert(
    sizeof(GCStringWithAllocator) ==
        sizeof(std::string) + sizeof(GCAllocator<char, UseCustomAllocator>) + 8,
    "GCStringWithAllocator size should match std::string size + allocator size + vptr");
