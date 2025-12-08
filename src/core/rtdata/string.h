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
 * Updated: Dec. 08, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

class GCString : public GCObject {
  public:
    GCString(const GCString &)            = delete;
    GCString &operator=(const GCString &) = delete;

    static GCString *create(size_t length, IAllocator &allocator = mm::autoSpace()) {
        size_t totalSize = offsetof(GCString, data_) + (length + 1);
        void *memory     = allocator.alloc(totalSize, alignof(GCString));
        if (!memory)
            throw std::bad_alloc();
        auto *str          = new (memory) GCString(length);
        str->data_[length] = '\0';
        str->size_         = static_cast<uint32_t>(length);
        return str;
    }

    static GCString *from(const char *src, IAllocator &allocator = mm::autoSpace()) {
        size_t len = std::strlen(src);
        auto *str  = create(len, allocator);
        std::memcpy(str->data_, src, len + 1);
        return str;
    }

    static GCString *from(const std::string &s, IAllocator &allocator = mm::autoSpace()) {
        auto *str = create(s.size(), allocator);
        std::memcpy(str->data_, s.data(), s.size());
        str->data_[s.size()] = '\0';
        return str;
    }

    std::string toString() const { return std::string(data_, size_); }

    static GCString *
    concat(const GCString *a, const GCString *b, IAllocator &allocator = mm::autoSpace()) {
        size_t lenA      = a->size();
        size_t lenB      = b->size();
        size_t newLen    = lenA + lenB;
        GCString *result = create(newLen, allocator);
        std::memcpy(result->data_, a->data_, lenA);
        std::memcpy(result->data_ + lenA, b->data_, lenB);
        result->data_[newLen] = '\0';
        return result;
    }

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    const char *data() const { return data_; }
    const char *c_str() const { return data_; }

    char operator[](size_t i) const {
        assert(i < size_ && "GCString index out of range");
        return data_[i];
    }

    int compare(const GCString *other) const {
        int cmp = std::memcmp(data_, other->data_, std::min(size_, other->size_));
        if (cmp != 0)
            return cmp;
        return (size_ < other->size_) ? -1 : (size_ > other->size_);
    }

    bool equals(const GCString *other) const {
        return this == other ||
               (other->size_ == size_ && std::memcmp(data_, other->data_, size_) == 0);
    }

    bool equals(const char *cstr) const { return std::strcmp(data_, cstr) == 0; }

    bool operator==(const GCString &other) const { return equals(&other); }
    bool operator!=(const GCString &other) const { return !equals(&other); }
    bool operator<(const GCString &other) const { return compare(&other) < 0; }

    size_t find(char ch, size_t start = 0) const {
        for (size_t i = start; i < size_; ++i)
            if (data_[i] == ch)
                return i;
        return npos;
    }

    size_t find(const GCString *substr, size_t start = 0) const {
        if (substr->size_ == 0 || substr->size_ > size_)
            return npos;
        for (size_t i = start; i <= size_ - substr->size_; ++i)
            if (std::memcmp(data_ + i, substr->data_, substr->size_) == 0)
                return i;
        return npos;
    }

    bool startsWith(const GCString *prefix) const {
        return prefix->size_ <= size_ && std::memcmp(data_, prefix->data_, prefix->size_) == 0;
    }

    bool endsWith(const GCString *suffix) const {
        return suffix->size_ <= size_ &&
               std::memcmp(data_ + size_ - suffix->size_, suffix->data_, suffix->size_) == 0;
    }

    bool contains(const GCString *substr) const { return find(substr) != npos; }

    GCString *substr(size_t pos, size_t len = npos, IAllocator &allocator = mm::autoSpace()) const {
        if (pos >= size_)
            return from("", allocator);
        if (len > size_ - pos)
            len = size_ - pos;
        GCString *result = create(len, allocator);
        std::memcpy(result->data_, data_ + pos, len);
        result->data_[len] = '\0';
        return result;
    }

    uint32_t hash() const {
        if (!cachedHash_)
            cachedHash_ = computeHash(data_, size_);
        return cachedHash_;
    }

    virtual GCObject *clone(IAllocator &allocator, bool /*deep*/) const override {
        GCString *copy = GCString::create(size_, allocator);
        std::memcpy(copy->data_, data_, size_ + 1);
        copy->cachedHash_ = cachedHash_;
        return copy;
    }

    virtual void onMoved() override {}
    virtual void updateRefs(const std::function<GCRef(GCRef)> &) override {}

    static constexpr size_t npos = static_cast<size_t>(-1);

  private:
    explicit GCString(size_t length) : size_(static_cast<uint32_t>(length)), cachedHash_(0) {}

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
