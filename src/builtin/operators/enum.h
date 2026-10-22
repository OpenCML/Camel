// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The lowering targets of the enum (design principles 5.3), inserted by the GIR builder and
 * never written by a program:
 *
 *   __enum_make__(tag: int, field0, field1, ...) => E   a value of the variant `tag`
 *   __enum_tag__(value: E) => int                        the tag of a value
 *   __enum_field__(index: int, value: E) => T            field `index` of the variant
 *
 * The result type of `__enum_make__` and `__enum_field__` is known only to the builder that
 * inserts the call (it is the enum instance at that position and the field's type), which
 * replaces the type the resolver gives the node.
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"

#include <vector>

slot_t __enum_make__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __enum_tag__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __enum_field__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

/// The operator groups of this file, exported by the globals module beside its own.
const std::vector<oper_group_ptr_t> &getEnumOperatorGroups();