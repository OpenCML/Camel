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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

class String : public rtdata::Object {
  public:
    String(const String &)            = delete;
    String &operator=(const String &) = delete;

    static String *create(size_t length, camel::core::mm::IAllocator &allocator) {
        size_t totalSize = sizeof(String) + (length + 1);
        void *memory     = allocator.alloc(totalSize, alignof(String));
        if (!memory)
            throw std::bad_alloc();
        auto *str          = new (memory) String(length);
        str->data_[length] = '\0';
        str->size_         = static_cast<uint32_t>(length);
        return str;
    }

    static String *from(const char *src, camel::core::mm::IAllocator &allocator) {
        size_t len = std::strlen(src);
        auto *str  = create(len, allocator);
        std::memcpy(str->data_, src, len + 1);
        return str;
    }

    static String *from(const std::string &s, camel::core::mm::IAllocator &allocator) {
        auto *str = create(s.size(), allocator);
        std::memcpy(str->data_, s.data(), s.size());
        str->data_[s.size()] = '\0';
        return str;
    }

    std::string toString() const { return std::string(data_, size_); }

    static String *
    concat(const String *a, const String *b, camel::core::mm::IAllocator &allocator) {
        size_t lenA    = a->size();
        size_t lenB    = b->size();
        size_t newLen  = lenA + lenB;
        String *result = create(newLen, allocator);
        std::memcpy(result->data_, a->data_, lenA);
        std::memcpy(result->data_ + lenA, b->data_, lenB);
        result->data_[newLen] = '\0';
        return result;
    }

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    const char *data() const { return data_; }
    const char *c_str() const { return data_; }
    std::string_view view() const noexcept { return std::string_view(data_, size_); }

    char operator[](size_t i) const {
        ASSERT(i < size_, "String index out of range");
        return data_[i];
    }

    int compare(const String *other) const {
        int cmp = std::memcmp(data_, other->data_, std::min(size_, other->size_));
        if (cmp != 0)
            return cmp;
        return (size_ < other->size_) ? -1 : (size_ > other->size_);
    }

    bool equals(const String *other) const {
        return this == other ||
               (other->size_ == size_ && std::memcmp(data_, other->data_, size_) == 0);
    }

    bool equals(const char *cstr) const { return std::strcmp(data_, cstr) == 0; }

    bool operator==(const String &other) const { return equals(&other); }
    bool operator!=(const String &other) const { return !equals(&other); }
    bool operator<(const String &other) const { return compare(&other) < 0; }

    size_t find(char ch, size_t start = 0) const {
        for (size_t i = start; i < size_; ++i)
            if (data_[i] == ch)
                return i;
        return npos;
    }

    size_t find(const String *substr, size_t start = 0) const {
        if (substr->size_ == 0 || substr->size_ > size_)
            return npos;
        for (size_t i = start; i <= size_ - substr->size_; ++i)
            if (std::memcmp(data_ + i, substr->data_, substr->size_) == 0)
                return i;
        return npos;
    }

    bool startsWith(const String *prefix) const {
        return prefix->size_ <= size_ && std::memcmp(data_, prefix->data_, prefix->size_) == 0;
    }

    bool endsWith(const String *suffix) const {
        return suffix->size_ <= size_ &&
               std::memcmp(data_ + size_ - suffix->size_, suffix->data_, suffix->size_) == 0;
    }

    bool contains(const String *substr) const { return find(substr) != npos; }

    String *substr(camel::core::mm::IAllocator &allocator, size_t pos, size_t len = npos) const {
        if (pos >= size_)
            return from("", allocator);
        if (len > size_ - pos)
            len = size_ - pos;
        String *result = create(len, allocator);
        std::memcpy(result->data_, data_ + pos, len);
        result->data_[len] = '\0';
        return result;
    }

    uint32_t hash() const {
        if (!cachedHash_)
            cachedHash_ = computeHash(data_, size_);
        return cachedHash_;
    }

    virtual bool equals(
        const rtdata::Object *other, const type::Type * /*type*/,
        bool /*deep*/ = false) const override {
        if (this == other)
            return true;

        if (!isOfSameCls(this, other))
            return false;

        const String *rhs = reinterpret_cast<const String *>(other);

        // Different lengths mean inequality.
        if (size_ != rhs->size_)
            return false;

        // Compare contents.
        return std::memcmp(data_, rhs->data_, size_) == 0;
    }

    virtual rtdata::Object *clone(
        camel::core::mm::IAllocator &allocator, const type::Type * /*type*/,
        bool /*deep*/ = false) const override {
        String *copy = String::create(size_, allocator);
        std::memcpy(copy->data_, data_, size_ + 1);
        copy->cachedHash_ = cachedHash_;
        return copy;
    }

    virtual void print(std::ostream &os, const type::Type * /*type*/) const override {
        os << data_;
    }

    virtual void onMoved() override {}
    virtual void updateRefs(
        const std::function<rtdata::Object *(rtdata::Object *)> &,
        const type::Type * /*type*/) override {}

    static constexpr size_t npos = static_cast<size_t>(-1);

  private:
    explicit String(size_t length) : size_(static_cast<uint32_t>(length)), cachedHash_(0) {}

    static uint32_t computeHash(const char *s, size_t len) {
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < len; ++i) {
            h ^= static_cast<uint8_t>(s[i]);
            h *= 16777619u;
        }
        return h;
    }

    uint32_t size_;
    mutable uint32_t cachedHash_;
    char data_[];
};
