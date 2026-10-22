// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

/**
 * @file values.h
 * @brief Showing a run-time value to the user of the debugger.
 *
 * A Camel value does not carry its type: a slot is shown with the static type of the node or
 * field it belongs to (docs/technical/35_debugger.md, 6.2). Everything here reads the stopped
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
