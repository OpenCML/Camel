// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The diagnostic self-test of the core array type's dimensions (`std::type::array`). It checks
 * the invariants the rest of the system relies on: one interned object per shape, the relations
 * (equality, assignability, the checked conversion, unification, widening), the call-site
 * binding of dimension names and packs, and the runtime shape check that moved from the tensor
 * module into this type. It throws when an invariant is broken; the pass prints its summary.
 *
 * The self-test is a program-independent pass, like `std::gc::blocks`: it needs no source file
 * beyond a module that loads, so it can run in a plan and its output is one line a test asserts.
 */

#pragma once

#include <string>

namespace camel::core::type {

/// Runs the self-test; returns the one-line summary the pass prints (including the diagnostic
/// text of the moved shape check). Throws std::runtime_error on the first broken invariant.
std::string debugRunArrayTypeSelfTest();

} // namespace camel::core::type