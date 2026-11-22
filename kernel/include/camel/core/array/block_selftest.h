// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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