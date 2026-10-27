// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The types a C++ module provides, by module and name (design principles 10.4).
//
// A module declares such a type in its declaration module without a right side
// (`export type Graph`, `export type Txn<F>`): the same rule as a function without a body, which
// C++ provides. The identity of the type is the module and the name, as a resource's is, so the
// declaration finds the type the module registered under that pair, and two modules that both
// write `Handle` have two types.

#pragma once

#include "camel/core/type/base.h"

#include <string_view>

namespace camel::core::type {

/// Registers `type` as the type `name` of the C++ module `module`. A second registration of the
/// same pair replaces the first (a module that is loaded again registers again).
void registerNativeType(std::string_view module, std::string_view name, Type *type);

/// The type registered under the pair, or nullptr when no module registered one.
Type *findNativeType(std::string_view module, std::string_view name);

} // namespace camel::core::type
