// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Hooks that modules register about the values of their own types.
 *
 * The properties of operators (purity, type rules, type folders) live in the operator table,
 * camel/core/operator_table.h, which this header includes for the code that used to find
 * them here.
 */

#pragma once

#include "camel/core/operator_table.h"
#include "camel/core/rtdata/base.h"

#include <functional>
#include <shared_mutex>
#include <vector>

namespace camel::core {

/**
 * The exact type of a constant, for types that do not fix everything about their values: a
 * tensor constant typed `Tensor` has a dtype and a shape. Returns nullptr when `type` is not one
 * the refiner knows. Graph rewrites give constants these types, so that shape facts flow from
 * captured weights into the operators that use them.
 */
using ValueTypeRefiner = std::function<type::Type *(slot_t value, type::Type *type)>;

class ValueTypeRefinerRegistry {
  public:
    static ValueTypeRefinerRegistry &instance();

    void add(ValueTypeRefiner refiner);

    /// The exact type of `value`, or `type` when no refiner knows more.
    type::Type *refine(slot_t value, type::Type *type) const;

  private:
    mutable std::shared_mutex mutex_;
    std::vector<ValueTypeRefiner> refiners_;
};

} // namespace camel::core
