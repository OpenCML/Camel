/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <string>

namespace camel::core::rtdata {

/**
 * Exercises the compact-element view boundary (H28 review item 1):
 *
 *   normal   a view materialised before it is stored is not a view any more;
 *   escape   a view that is not materialised is still detected as one.
 *
 * The detector (`isViewSlot`) is always compiled, so the same report comes from a release build,
 * where the store assert is compiled out, and from a debug build, where it fires.
 */
std::string debugRunViewSelfTest();

} // namespace camel::core::rtdata