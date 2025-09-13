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
 * Created: Jul. 15, 2025
 * Updated: Jul. 15, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "utils/assert.h"
#include "utils/str.h"

enum Modifier : uint32_t {
    None = 0,
    Atomic = 1 << 1,
    Shared = 1 << 2,
    Sync = 1 << 3,
    Macro = 1 << 4,
};

enum class ExitType {
    Return,
    Yield,
    Raise,
    Throw,
};

inline std::string to_string(Modifier mod) {
    switch (mod) {
    case Modifier::Atomic:
        return "atomic";
    case Modifier::Shared:
        return "shared";
    case Modifier::Sync:
        return "sync";
    case Modifier::Macro:
        return "macro";
    case Modifier::None:
        return "";
    default:
        ASSERT(false, "Unknown Modifier");
        return "unknown";
    }
}

inline Modifier from_string(const std::string &str) {
    if (str == "atomic")
        return Modifier::Atomic;
    if (str == "shared")
        return Modifier::Shared;
    if (str == "sync")
        return Modifier::Sync;
    if (str == "macro")
        return Modifier::Macro;
    return Modifier::None;
}

inline std::string to_string(ExitType type) {
    switch (type) {
    case ExitType::Return:
        return "return";
    case ExitType::Yield:
        return "yield";
    case ExitType::Raise:
        return "raise";
    case ExitType::Throw:
        return "throw";
    default:
        ASSERT(false, "Unknown ExitType");
        return "unknown";
    }
}

using ModifierMask = uint32_t;

class ModifierSet {
  public:
    ModifierMask mask;

    ModifierSet() : mask(Modifier::None) {}
    ModifierSet(ModifierMask m) : mask(m) {}

    void insert(Modifier mod) { mask |= mod; }
    void remove(Modifier mod) { mask &= ~mod; }
    bool has(Modifier mod) const { return mask & mod; }
    void clear() { mask = static_cast<ModifierMask>(Modifier::None); }

    bool atomic() const { return has(Modifier::Atomic); }
    bool shared() const { return has(Modifier::Shared); }
    bool sync() const { return has(Modifier::Sync); }
    bool macro() const { return has(Modifier::Macro); }

    bool empty() const { return mask == static_cast<ModifierMask>(Modifier::None); }

    operator std::string() const {
        std::vector<std::string> result;
        for (auto mod : {Modifier::Atomic, Modifier::Shared, Modifier::Sync, Modifier::Macro}) {
            if (has(mod)) {
                result.emplace_back(to_string(mod));
            }
        }
        return strutil::join(result, " ");
    }
};

inline ModifierMask operator|(Modifier a, Modifier b) {
    return static_cast<ModifierMask>(a) | static_cast<ModifierMask>(b);
}

inline ModifierMask operator&(ModifierMask a, Modifier b) {
    return a & static_cast<ModifierMask>(b);
}

inline ModifierSet operator|(const ModifierSet &lhs, Modifier rhs) {
    return ModifierSet(lhs.mask | static_cast<ModifierMask>(rhs));
}

inline ModifierSet &operator|=(ModifierSet &lhs, Modifier rhs) {
    lhs.insert(rhs);
    return lhs;
}

inline std::ostream &operator<<(std::ostream &os, const ModifierSet &mods) {
    os << static_cast<std::string>(mods);
    return os;
}