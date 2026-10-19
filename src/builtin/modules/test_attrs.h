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
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * `test_attrs`: decorators that grant node attributes, for the tests of the mechanism
 * (camel/core/module/attr_decorator.h). It exports them only when the
 * environment variable CAMEL_ENABLE_TEST_PASSES is set, as the test passes are.
 *
 *     mark            grants `test.mark` (a mark: inner wins, merges only with itself)
 *     flags<n: int>   grants bit n of `test.flags` (flags of any width: union, intersection
 *                     on merge); n from 0 to 1000, otherwise it is applied as it runs, which
 *                     grants nothing
 *     level<n: int>   grants `test.level` = n (an integer: inner wins, merges only when equal)
 *     plain           the same decorator not declared to grant anything (a decorator applied
 *                     as it runs, returning its function)
 */

#pragma once

#include "camel/core/module/builtin.h"

class TestAttrsBuiltinModule : public camel::core::module::BuiltinModule {
  public:
    TestAttrsBuiltinModule(camel::core::context::context_ptr_t ctx);
    virtual ~TestAttrsBuiltinModule() = default;

    virtual bool load() override;

    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
