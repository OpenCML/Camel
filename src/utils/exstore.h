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
 * Created: Dec. 13, 2025
 * Updated: Dec. 19, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstddef>
#include <tuple>
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
