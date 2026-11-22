// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "camel/utils/assert.h"
#include "camel/utils/str.h"
#include <stdexcept>

enum Modifier : uint32_t {
    None = 0,
    Sync = 1 << 3,
};

enum class ExitType {
    Return,
    Yield,
};

inline std::string to_string(Modifier mod) {
    switch (mod) {
    case Modifier::Sync:
        return "sync";
    case Modifier::None:
        return "";
    default:
        throw std::runtime_error("Unknown Modifier");
        return "unknown";
    }
}

inline Modifier from_string(const std::string &str) {
    if (str == "sync")
        return Modifier::Sync;
    return Modifier::None;
}

inline std::string to_string(ExitType type) {
    switch (type) {
    case ExitType::Return:
        return "return";
    case ExitType::Yield:
        return "yield";
    default:
        throw std::runtime_error("Unknown ExitType");
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

    bool sync() const { return has(Modifier::Sync); }

    bool empty() const { return mask == static_cast<ModifierMask>(Modifier::None); }

    bool operator==(const ModifierSet &other) const { return mask == other.mask; }
    bool operator!=(const ModifierSet &other) const { return mask != other.mask; }

    operator std::string() const {
        std::vector<std::string> result;
        for (auto mod : {Modifier::Sync}) {
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
