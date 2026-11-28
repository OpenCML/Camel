// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace camel::parse::ast {

/// The words a resource declaration may carry, in the one order every reader writes them in: the
/// parser accepts them, the tree's bits are indexed by this order, `toString` and `geneCode` emit
/// them in it, and the kernel record is filled through the same index (design principles 3.3,
/// "资源修饰词说明写满足哪些等式").
///
/// **This table is the parse layer's whole list.** A word is added here and in the kernel's own
/// record (`camel::core::ResourceModifiers`, `kernel/core/resource_semantics.cpp`); the parser, the
/// tree builder, both printers and the AST-to-kernel mapping iterate this table, so none of them
/// spells a word and none of them can list them in another order.
inline constexpr auto kResourceModifierWords = std::to_array<std::string_view>({
    "unordered",
    "idempotent",
    "overwriting",
    "readonly",
    "observed",
});

/// How many modifiers the language has: the length of the table above, which is also the number of
/// bits a `ResourceDeclLoad` keeps.
inline constexpr size_t kResourceModifierCount = kResourceModifierWords.size();

/// The position of `word` in the table, or nothing when the word is not a modifier. The builder
/// asks this rather than comparing against words itself.
inline constexpr std::optional<size_t> resourceModifierIndex(std::string_view word) {
    for (size_t i = 0; i < kResourceModifierWords.size(); ++i) {
        if (kResourceModifierWords[i] == word) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace camel::parse::ast