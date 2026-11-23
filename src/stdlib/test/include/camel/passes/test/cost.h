// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/runtime/writings.h"

namespace camel::test {

/// The cost a test pass extracts by: one per node, and the cheapest writing of a value is the
/// smallest sum of its inputs' (the `CostModel` default's total). It is written here because the
/// kernel keeps no cost of its own any more (D-1123-78): the cost of a writing is the judgement
/// of the library that asks for the extraction, and a test module that wants "a writing costs its
/// size" says so itself.
class NodeCountCost final : public camel::runtime::CostModel {
  public:
    double nodeCost(const camel::runtime::CostContext &, camel::runtime::gc_node_ref_t)
        const override {
        return 1.0;
    }
};

} // namespace camel::test