// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <unordered_map>

#include "../base.h"

namespace camel::core::error {

enum class InternalDiag : uint32_t {
    UnknownInternalError = 0x000000,
};

constexpr DiagType diagTypeOf(InternalDiag) { return DiagType::InternalDiag; }

const std::unordered_map<InternalDiag, DiagInfo> &getInternalDiagInfoMap();

} // namespace camel::core::error
