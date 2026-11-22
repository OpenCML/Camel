// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

template <std::size_t N> struct ExtraStorage {
  private:
    template <std::size_t... Is> static consteval auto make_tuple_type(std::index_sequence<Is...>) {
        return std::type_identity<std::tuple<decltype(Is, (void *)nullptr)...>>{};
    }

    using ExtrasTuple = typename decltype(make_tuple_type(std::make_index_sequence<N>{}))::type;

  public:
    mutable ExtrasTuple extras{};

    constexpr ExtraStorage() = default;

    template <typename T, std::size_t Index> constexpr T *get() const {
        static_assert(Index < N, "Index out of range");
        return static_cast<T *>(std::get<Index>(extras));
    }

    template <typename T, std::size_t Index> constexpr void set(T *ptr) const {
        static_assert(Index < N, "Index out of range");
        std::get<Index>(extras) = static_cast<void *>(ptr);
    }
};
