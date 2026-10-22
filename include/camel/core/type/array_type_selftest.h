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
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

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