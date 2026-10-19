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
 * `test_attrs`: decorators that grant node attributes, for the tests of the mechanism (design
 * principles 2.17; camel/core/module/attr_decorator.h). It exports them only when the
 * environment variable CAMEL_ENABLE_TEST_PASSES is set, as the test passes are.
 *
 *     mark            grants `test.mark`
 *     flags<n: int>   grants `test.flags` with the value n (n > 0; otherwise it is applied as
 *                     it runs, which grants nothing)
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
