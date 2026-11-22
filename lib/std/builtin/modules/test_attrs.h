// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `test_attrs`: decorators that grant node attributes, for the tests of the mechanism
 * (camel/core/module/attr_decorator.h). It exports them only when the
 * by naming them in a pipeline; no environment variable gates them.
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
