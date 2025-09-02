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
 * Author: Zhenjie Wei
 * Created: Jul. 29, 2025
 * Updated: Jul. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "basic.h"
#include "builtin/operators/basic/io.h"
#include "builtin/operators/basic/ops.h"
#include "builtin/operators/basic/str.h"

void BasicBuiltinModule::exportBinaryOp(const std::string &name, operator_func_t func) {
    auto op = makeOperator(
        name,
        makeFuncType(
            param_init_list{},
            {{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
            anyTypePtr),
        func);

    auto ops = std::make_shared<std::vector<std::shared_ptr<Operator>>>();
    ops->push_back(op);
    exportEntity(name, ops);
}

void BasicBuiltinModule::exportAssnOp(const std::string &name, operator_func_t func) {
    auto op = makeOperator(
        name,
        makeFuncType(
            param_init_list{},
            {{"self", anyTypePtr, nullptr, true}, {"value", anyTypePtr, nullptr, false}},
            anyTypePtr),
        func);

    auto ops = std::make_shared<std::vector<std::shared_ptr<Operator>>>();
    ops->push_back(op);
    exportEntity(name, ops);
}

static const std::pair<std::string, operator_func_t> assnOps[] = {
    {"__assn__", __builtin__assn__},
    {"__assn_add__", __builtin__assn_add__},
    {"__assn_sub__", __builtin__assn_sub__},
    {"__assn_mul__", __builtin__assn_mul__},
    {"__assn_div__", __builtin__assn_div__},
    {"__assn_mod__", __builtin__assn_mod__},
    {"__assn_mat__", __builtin__assn_mat__},
    {"__assn_exp__", __builtin__assn_exp__},
    {"__assn_and__", __builtin__assn_and__},
    {"__assn_or__", __builtin__assn_or__}};

static const std::pair<std::string, operator_func_t> binaryOps[] = {
    {"__or__", __builtin__or__},
    {"__and__", __builtin__and__},
    {"__eq__", __builtin__eq__},
    {"__neq__", __builtin__neq__},
    {"__strict_eq__", __builtin__strict_eq__},
    {"__strict_neq__", __builtin__strict_neq__},
    {"__lt__", __builtin__lt__},
    {"__le__", __builtin__le__},
    {"__gt__", __builtin__gt__},
    {"__ge__", __builtin__ge__},
    {"__add__", __builtin__add__},
    {"__sub__", __builtin__sub__},
    {"__mul__", __builtin__mul__},
    {"__div__", __builtin__div__},
    {"__mod__", __builtin__mod__},
    {"__mat__", __builtin__mat__},
    {"__exp__", __builtin__exp__},
    {"__idx__", __builtin__idx__}};

static const std::pair<std::string, operator_func_t> others[] = {
    {"map", __not_implemented__},
    {"filter", __not_implemented__},
    {"reduce", __not_implemented__},
    {"len", __not_implemented__},
    {"range", __not_implemented__},
    {"enumerate", __not_implemented__},
    {"zip", __not_implemented__},
    {"unzip", __not_implemented__},
    {"exit", __not_implemented__}};

BasicBuiltinModule::BasicBuiltinModule() : BuiltinModule("") {
    for (const auto &[name, func] : assnOps) {
        exportAssnOp(name, func);
    }

    for (const auto &[name, func] : binaryOps) {
        exportBinaryOp(name, func);
    }

    for (const auto &[name, func] : others) {
        exportBuiltinOperator(name, param_init_list{}, param_init_list{}, anyTypePtr, func);
    }

    exportBuiltinOperator(
        "print",
        param_init_list{},
        {{"value", anyTypePtr, nullptr, false}},
        voidTypePtr,
        __print__);
    exportBuiltinOperator(
        "println",
        param_init_list{},
        {{"value", anyTypePtr, nullptr, false}},
        voidTypePtr,
        __println__);
    exportBuiltinOperator("input", param_init_list{}, param_init_list{}, voidTypePtr, __input__);
}
