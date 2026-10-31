// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/compile/gir/types.h"
#include "camel/utils/assert.h"

#include <format>
#include <stdexcept>

namespace camel::compile::gir {

std::string NodeDebugFingerprint::toEntityId() const {
    return std::format("gnode:{:016x}{:016x}", word0, word1);
}

std::string to_string(LinkType type) {
    switch (type) {
    case LinkType::Norm:
        return "Norm";
    case LinkType::With:
        return "With";
    case LinkType::Ctrl:
        return "Ctrl";
    case LinkType::Order:
        return "Order";
    }
    throw std::runtime_error("Unknown LinkType");
    return "Unknown";
}

} // namespace camel::compile::gir
