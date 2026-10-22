// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What the device code of this module tells `std::profile` (camel/profile/profile.h), without
 * depending on the core: the sources of the CUDA path are also built into the module's own
 * check executables, which link neither libcamel nor its headers.
 *
 * The module registers the hooks where it is loaded (module.cpp). Each hook checks whether the
 * session is recording itself, so a call site is one null check when the module did not register
 * them, and one indirect call otherwise. Only the module's own runs register them: a check
 * executable that calls tiles::execute directly records nothing.
 */

#pragma once

#include <cstdint>

namespace camel::tiles::profile_hooks {

/// One kernel was launched (`label`: the kernel's name).
inline void (*kernel)(const char *label) = nullptr;
/// `bytes` were copied host->device / device->host.
inline void (*toDevice)(uint64_t bytes) = nullptr;
inline void (*toHost)(uint64_t bytes)   = nullptr;
/// The host waited for the device (`what`: what it waited for).
inline void (*synchronise)(const char *what) = nullptr;
/// The module compiled device code at run time (NVRTC), `ns` nanoseconds of it.
inline void (*deviceCompile)(const char *what, uint64_t ns) = nullptr;
/// The device the module is running on ("cuda:0").
inline void (*device)(const char *name) = nullptr;

} // namespace camel::tiles::profile_hooks