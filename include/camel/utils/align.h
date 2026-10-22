// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>
#include <type_traits>

template <typename T, std::size_t Alignment> struct AlignedAllocator {
    static_assert(Alignment >= alignof(T), "Alignment must be at least alignof(T)");
    static_assert((Alignment & (Alignment - 1)) == 0, "Alignment must be power of two");

    using value_type         = T;
    using pointer            = T *;
    using const_pointer      = const T *;
    using void_pointer       = void *;
    using const_void_pointer = const void *;
    using size_type          = std::size_t;
    using difference_type    = std::ptrdiff_t;

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
