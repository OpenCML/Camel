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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Oct. 23, 2025
 * Updated: Oct. 23, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>
#include <type_traits>

template <typename T, std::size_t Alignment> struct AlignedAllocator {
    static_assert(Alignment >= alignof(T), "Alignment must be at least alignof(T)");
    static_assert((Alignment & (Alignment - 1)) == 0, "Alignment must be power of two");

    using value_type = T;
    using pointer = T *;
    using const_pointer = const T *;
    using void_pointer = void *;
    using const_void_pointer = const void *;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <typename U> struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };

    AlignedAllocator() noexcept = default;

    template <typename U> AlignedAllocator(const AlignedAllocator<U, Alignment> &) noexcept {}

    [[nodiscard]] pointer allocate(size_type n) {
        if (n == 0)
            return nullptr;

        void *ptr = nullptr;

        // _aligned_malloc is available on Windows
        ptr = _aligned_malloc(n * sizeof(T), Alignment);
        if (!ptr)
            throw std::bad_alloc();

        return static_cast<pointer>(ptr);
    }

    void deallocate(pointer p, size_type) noexcept { _aligned_free(p); }

    template <typename U, typename... Args> void construct(U *p, Args &&...args) {
        ::new ((void *)p) U(std::forward<Args>(args)...);
    }

    template <typename U> void destroy(U *p) { p->~U(); }

    bool operator==(const AlignedAllocator &) const noexcept { return true; }
    bool operator!=(const AlignedAllocator &) const noexcept { return false; }
};
