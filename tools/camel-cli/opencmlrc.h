/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Mar. 08, 2026
 * Updated: Mar. 08, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
