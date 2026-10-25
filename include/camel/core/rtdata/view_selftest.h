// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

namespace camel::core::rtdata {

/**
 * Exercises the compact-element view boundary (review item 1):
 *
 *   normal   a view materialised before it is stored is not a view any more;
 *   escape   a view that is not materialised is still detected as one.
 *
 * The detector (`isViewSlot`) is always compiled, so the same report comes from a release build,
 * where the store assert is compiled out, and from a debug build, where it fires.
 */
std::string debugRunViewSelfTest();

/**
 * The same escape, deliberately, to prove the debug guard actually aborts. A compact
 * element view is read and handed to the reduce accumulator store, which asserts in a debug
 * build: the process aborts with the assertion message instead of keeping a view that names the
 * next element. A release build compiles the assertion out, materialises the view in the store
 * and returns a "not-caught" line, which is how a debug-layer test fails loudly when it is
 * pointed at a release binary.
 *
 * Runs only from the `std::gc::views_escape` pass, which exists solely for the debug test layer.
 */
std::string debugRunViewEscapeSelfTest();

} // namespace camel::core::rtdata