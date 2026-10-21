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
 * Author: Camel Contributors
 * Created: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace camel::utils {

/// A set of non-null pointers kept in one array (open addressing, linear probing), for the
/// walks over the program's graphs that insert every graph once: a std::unordered_set allocates
/// a node per element, which dominated those walks. Elements cannot be removed.
template <typename T> class PointerSet {
  public:
    explicit PointerSet(size_t expected = 0) { reserve(expected); }

    size_t size() const { return size_; }

    void reserve(size_t expected) {
        size_t capacity = std::bit_ceil(std::max<size_t>(16, expected * 2));
        if (capacity > slots_.size()) {
            rehash(capacity);
        }
    }

    /// True when `ptr` was not in the set.
    bool insert(const T *ptr) {
        if ((size_ + 1) * 2 > slots_.size()) {
            rehash(std::max<size_t>(16, slots_.size() * 2));
        }
        size_t at = position(ptr);
        while (slots_[at] != nullptr) {
            if (slots_[at] == ptr) {
                return false;
            }
            at = (at + 1) & (slots_.size() - 1);
        }
        slots_[at] = ptr;
        ++size_;
        return true;
    }

    bool contains(const T *ptr) const {
        if (slots_.empty()) {
            return false;
        }
        size_t at = position(ptr);
        while (slots_[at] != nullptr) {
            if (slots_[at] == ptr) {
                return true;
            }
            at = (at + 1) & (slots_.size() - 1);
        }
        return false;
    }

  private:
    size_t position(const T *ptr) const {
        const uint64_t mixed = (reinterpret_cast<uintptr_t>(ptr) >> 4) * 0x9E3779B97F4A7C15ull;
        return static_cast<size_t>(mixed >> 20) & (slots_.size() - 1);
    }

    void rehash(size_t capacity) {
        std::vector<const T *> old(capacity, nullptr);
        old.swap(slots_);
        for (const T *ptr : old) {
            if (ptr != nullptr) {
                size_t at = position(ptr);
                while (slots_[at] != nullptr) {
                    at = (at + 1) & (slots_.size() - 1);
                }
                slots_[at] = ptr;
            }
        }
    }

    std::vector<const T *> slots_;
    size_t size_ = 0;
};

} // namespace camel::utils
