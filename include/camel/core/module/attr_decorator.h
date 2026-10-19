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
 * What a library writes to give programs a decorator that grants a node attribute (design
 * principles 2.17; camel/core/node_attrs.h, OperatorTable::declareGrant). For example, a
 * module that marks the functions it must compile as kernels:
 *
 *     // exported entities (the module's constructor)
 *     exportEntity("kernel", attrDecoratorGroup("kernel", "tiles:kernel", 0, "(fn: Function) => Function"));
 *     // load()
 *     OperatorTable::instance().set("tiles:kernel", attrDecoratorTraits());
 *     OperatorTable::instance().declareGrant("tiles:kernel", {.key = "tiles.kernel"});
 *     // its executor maps "kernel" to &attrDecoratorIdentity
 *
 * and the program writes `@kernel func f(...) { ... }` or `@kernel { ... }`. A decorator with
 * with-arguments (`@relax<x, y>`) checks their types in `acceptsWith` and turns the constants
 * written into the attribute's value in AttrGrant::value.
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/operator_table.h"

#include <functional>
#include <string>

namespace camel::core::module {

/// The operator group `name` of one overload `uri`: a decorator that takes with-arguments,
/// `withCount` of them (-1: any number) whose types `acceptsWith` accepts (null: any), and one
/// function, and returns a function of the same type.
oper_group_ptr_t attrDecoratorGroup(
    const std::string &name, const std::string &uri, int withCount, std::string signature,
    std::function<bool(const type::type_vec_t &with)> acceptsWith = nullptr);

/// The traits of such a decorator: pure, never faults.
OperatorTraits attrDecoratorTraits();

/// The decorator as it runs, when the graph builder did not apply it (a decorator named
/// through a variable, with-arguments known only at run time, a call written as a call): the
/// function unchanged. Without the attribute the function computes what it was written to,
/// which an attribute only allows a library to change.
slot_t attrDecoratorIdentity(ArgsView &with, ArgsView &norm, context::Context &ctx);

} // namespace camel::core::module
