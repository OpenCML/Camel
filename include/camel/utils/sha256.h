// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Shared SHA-256 for binary integrity and immutable checkpoint manifests.

#pragma once
#include <string>
#include <string_view>
namespace camel::utils {
std::string sha256Hex(std::string_view bytes);
} // namespace camel::utils
