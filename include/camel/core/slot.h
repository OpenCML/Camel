// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

using slot_t = uint64_t;

constexpr slot_t NullSlot = 0;
constexpr slot_t DeadSlot = 0xDEADBEAFDEADBEAFULL;
