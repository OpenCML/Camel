// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <string_view>

/*
 * Work that may be done once for the whole process, whichever module of the process asks first.
 *
 * A module is a separate shared object (`.cmo`), loaded with RTLD_LOCAL. A guard declared in one
 * module — a function-local `std::once_flag` in its own translation unit — is a different object
 * in every module, so two modules whose first use runs on two threads could enter the same
 * guarded work at the same time. The state therefore lives in libcamel, which every module links,
 * and `key` is what the modules of a process share.
 *
 * `action` runs at most once for `key`. A throw leaves the action unrun: the next call tries
 * again (`std::call_once`'s rule), which is what a failure that the environment may have caused
 * wants — the first call reports the fault, and a later call in the same process may succeed.
 */

namespace camel::utils {

/// Runs `action` once for `key` in this process; a throw leaves it to be tried again.
void runOnce(std::string_view key, const std::function<void()> &action);

} // namespace camel::utils