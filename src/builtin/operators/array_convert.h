// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type/base.h"
#include "camel/core/type/resolver.h"

/// A type name applied to a numeric array (`f32(xs)`, `int(xs)`): the elementwise conversion, one
/// operator per target element type (`:op/atoi`, `:op/atol`, `:op/atof`, `:op/atod`, `:op/atobf`,
/// `:op/atof6`). Their signatures are the declarations in `stdlib/prelude.cml` (design principles
/// 6.4): the result has the input's extents and the target element type.

slot_t __atoi__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __atol__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __atof__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __atod__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __atobf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __atof6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

/// The operator of the array conversion into `target` (":op/atof"), or an empty string.
const char *arrayConversionUri(camel::core::type::TypeCode target);
