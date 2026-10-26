// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/parse/ast/data.h"

#include <optional>
#include <string_view>

namespace camel::parse::ast {

/// The words a `match type` case names a shape with (design principles 5.5): one table, read by
/// the syntax tree builder (the word written -> the kind) and by the graph builder (the kind ->
/// the word it prints). `case array` selects every array type.
struct TypeCaseWord {
    std::string_view word;
    TypeCaseKind kind;
};

inline constexpr TypeCaseWord kTypeCaseWords[] = {
    {"struct", TypeCaseKind::Struct}, {"tuple", TypeCaseKind::Tuple},
    {"array", TypeCaseKind::Array},   {"func", TypeCaseKind::Func},
    {"#leaf", TypeCaseKind::Leaf},    {"_", TypeCaseKind::Wildcard},
};

inline std::optional<TypeCaseKind> typeCaseKindOfWord(std::string_view word) {
    for (const TypeCaseWord &entry : kTypeCaseWords) {
        if (entry.word == word) {
            return entry.kind;
        }
    }
    return std::nullopt;
}

inline std::string_view typeCaseWordOfKind(TypeCaseKind kind) {
    for (const TypeCaseWord &entry : kTypeCaseWords) {
        if (entry.kind == kind) {
            return entry.word;
        }
    }
    return {};
}

} // namespace camel::parse::ast
