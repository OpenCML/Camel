// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type/base.h"
#include "camel/core/type/resolver.h"

/// A type name applied to a numeric array (`f32(xs)`, `int(xs)`): the elementwise conversion, one
/// operator per target element type (`:op/atoi`, `:op/atol`, `:op/atof`, `:op/atod`, `:op/atobf`,
/// `:op/atof6`), declared in the groups of the type names beside the scalar conversions. The
/// result has the input's extents and the target element type. Design principles 5.1.
camel::core::type::resolver_ptr_t arrayConversionResolver(camel::core::type::TypeCode target);

slot_t __atoi__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __atol__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __atof__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __atod__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __atobf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __atof6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

/// The operator of the array conversion into `target` (":op/atof"), or an empty string.
const char *arrayConversionUri(camel::core::type::TypeCode target);
