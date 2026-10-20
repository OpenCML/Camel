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
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Enums in the C++ a Camel program is translated into (docs/design/cpp-standalone).
 *
 * A value of an enum type is a struct the generated file declares for that type: a tag, the
 * position of the variant, and a std::variant holding the payload of that variant (monostate
 * for a unit variant). The struct declares `make<Tag>` and `get<Tag, Index>`, generated from
 * the declaration, and an appendText that writes it as the interpreter does
 * (`Shape::Circle(1.0)`, `Shape::Rect { w: 2.0, h: 3.0 }`, `Shape::Empty`).
 *
 * These two helpers are what `:enum/tag` and `:enum/field` translate to; the tag and the
 * field index of the second are constants of the program, so they are template arguments.
 */

#pragma once

#include "camel/standalone/recursive.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace camel::standalone {

/// The position of the variant a value of an enum type holds.
template <typename E> std::int64_t enumTag(const E &value) {
    return static_cast<std::int64_t>(value.tag);
}

/// Field `Index` of variant `Tag` of an enum value. The graph read the tag before this call,
/// so the value holds that variant.
template <std::size_t Tag, std::size_t Index, typename E> auto enumField(const E &value) {
    return value.template get<Tag, Index>();
}

} // namespace camel::standalone