// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Crash-safe replacement of a file, separate from the kernels of the module.

#pragma once
#include <filesystem>
#include <string_view>

void fsAtomicReplace(const std::filesystem::path &path, std::string_view bytes);
