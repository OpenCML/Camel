// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Built-in operators over Map and Set (design principles 11.3;
 * docs/technical/31_map_and_set.md, section 6).
 *
 * Every one is pure: it reads its arguments and returns a new value. The collection that
 * flows through is the ordinary argument, what configures the operation is a `with`
 * argument (`put<k, v>(m)`, `m->put<k, v>`), as with `contains<v>(xs)` and `slice<a, b>(xs)`;
 * an operation on two collections of equal standing takes both as ordinary arguments
 * (`merge(a, b)`), as `concat(a, b)` does. This is the one calling form (design principles
 * 11.4); `put(m, k, v)`, once accepted as well, is refused with a hint that gives the form.
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"

#include <vector>

slot_t __coll_map_idx__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_map_get__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_map_get_or__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_map_put__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_set_add__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_remove__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_has__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_len__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_keys__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_map_values__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_map_items__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_merge__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_set_intersect__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_set_difference__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_set_is_subset__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_eq__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __coll_neq__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

/**
 * The operator groups of this file. Some carry a name the globals module already has a group
 * for (`len`, `contains`, `__idx__`, `__eq__`, `__neq__`): those are further overloads of
 * that name, and the globals module exports one group holding both.
 */
const std::vector<oper_group_ptr_t> &getCollectionOperatorGroups();
