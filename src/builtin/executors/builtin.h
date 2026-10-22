// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <span>
#include <string_view>

#include "camel/execute/executor.h"

class BasicBuiltinExecutor : public Executor {
  public:
    BasicBuiltinExecutor(camel::core::context::context_ptr_t ctx);
    virtual ~BasicBuiltinExecutor() = default;

    static executor_ptr_t create(camel::core::context::context_ptr_t ctx);
};

/// Registers the builtin operators' traits (purity) with the core OperatorTable, then their
/// semantics (declareBuiltinOperatorSemantics).
void registerBuiltinOperatorTraits();

/// Declares the semantics of the built-in scalar arithmetic (builtin_semantics.cpp): stable
/// keys, numeric semantics and faults. Called after the traits, which it is checked against.
void declareBuiltinOperatorSemantics();

/// Declares the resources of the built-in operators that touch any (builtin_semantics.cpp;
/// docs/design/op-semantics section 8): `:var/*` and the assignments (`names`: the built-in
/// operator names, `op/assn...` among them) their variable cell, the console and the profiler
/// their ambient state. Called after the traits.
/// Whether the built-in operator `name` (without the leading ':') stores into a variable cell
/// (the assignments). The one place that names the family: purity and the resource
/// declarations both read it, nothing else infers effects from a name.
bool storesIntoCell(std::string_view name);

void declareBuiltinOperatorResources(std::span<const std::string_view> names);
