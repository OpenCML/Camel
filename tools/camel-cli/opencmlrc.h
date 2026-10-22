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
 * used only when graph != null after running this list (system default fallback: std::default).
 * If outFallbackPasses is non-null, sets it to passes.fallback from rc, or ["std::default"] when
 * unset.
 */
std::vector<std::string> resolvePassListWithRc(
    const std::string &cwd, const std::string &scriptDir,
    const std::vector<std::string> &cliPassList,
    std::vector<std::string> *outFallbackPasses = nullptr);

} // namespace opencmlrc
