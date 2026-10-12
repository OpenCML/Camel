/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */
#pragma once

/**
 * @file values.h
 * @brief Showing a run-time value to the user of the debugger.
 *
 * A Camel value does not carry its type: a slot is shown with the static type of the node or
 * field it belongs to (docs/technical/34_debugger.md, 6.2). Everything here reads the stopped
 * program on its own thread and returns text: no pointer to a Camel object leaves these
 * functions except inside a ValueView, which is valid only until the program resumes.
 */

#include "camel/core/rtdata/base.h"
#include "camel/core/type.h"

#include <optional>
#include <string>
#include <vector>

namespace debugger::core {

/// A slot and the static type to read it with. Valid while the program is stopped.
struct ValueView {
    slot_t slot                    = NullSlot;
    camel::core::type::Type *type = nullptr;
};

struct Rendered {
    std::string value;
    std::string type;
    bool expandable = false;
};

struct Member {
    std::string name;
    ValueView value;
};

/// A type as a program writes it (`int`, not `i64`).
std::string typeText(const camel::core::type::Type *type);

/// The value as text, cut at `limit` characters, and whether it has members to show.
Rendered render(const ValueView &view, size_t limit = 240);

/// Elements, fields or captured values; at most `maxCount`. `total` receives how many exist.
std::vector<Member> members(const ValueView &view, size_t maxCount, size_t *total = nullptr);

/// The member a program names `name` after a dot or inside brackets (a field, a tuple or array
/// index); nullopt when the value has none of that name.
std::optional<ValueView> member(const ValueView &view, const std::string &name);

} // namespace debugger::core
