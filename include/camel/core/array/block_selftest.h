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
 * Author: Camel Contributors
 * Created: Oct. 20, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The diagnostic self-test of the data block mechanism (`std::gc::blocks`). It checks the
 * invariants the rest of the system relies on: an inline value allocates no block, promotion
 * happens once and is shared, and DLPack export/import share the same address. It throws when
 * an invariant is broken; the pass prints its counters.
 */

#pragma once

namespace camel::core::array {

bool debugRunBlockSelfTest();

} // namespace camel::core::array