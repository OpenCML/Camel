// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

namespace opencmlrc {

/**
 * Resolve effective pass list using .opencmlrc.
 * Loads config from cwd first, then from scriptDir (script dir overrides cwd).
 * Returns: prefix + cliPassList + suffix. Fallback is not part of the list; it is
 * used only when graph != null after running this list (system default fallback: std::run, the
 * scheduling pass that runs the program).
 * If outFallbackPasses is non-null, sets it to passes.fallback from rc, or ["std::run"] when
 * unset.
 * If outRunTarget is non-null, sets it to passes.run -- the scheduling pass `std::run` stands for
 * -- and leaves it empty when the rc names none, which keeps the kernel's default.
 */
std::vector<std::string> resolvePassListWithRc(
    const std::string &cwd, const std::string &scriptDir,
    const std::vector<std::string> &cliPassList,
    std::vector<std::string> *outFallbackPasses = nullptr, std::string *outRunTarget = nullptr);

} // namespace opencmlrc
