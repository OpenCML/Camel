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

void BasicBuiltinModule::exportBinaryOp(const std::string &name, const std::string &uri) {
    auto op = makeOperator(
        name,
        makeFuncType(
            param_init_list{},
            {{"lhs", anyTypePtr, nullptr, false}, {"rhs", anyTypePtr, nullptr, false}},
            anyTypePtr),
        uri);

    auto ops = std::make_shared<std::vector<std::shared_ptr<OperatorIndex>>>();
    ops->push_back(op);
    exportEntity(name, ops);
}

void BasicBuiltinModule::exportAssnOp(const std::string &name, const std::string &uri) {
    auto op = makeOperator(
        name,
        makeFuncType(
            param_init_list{},
            {{"self", anyTypePtr, nullptr, true}, {"value", anyTypePtr, nullptr, false}},
            anyTypePtr),
        uri);

    auto ops = std::make_shared<std::vector<std::shared_ptr<OperatorIndex>>>();
    ops->push_back(op);
    exportEntity(name, ops);
}

static const std::pair<std::string, std::string> assnOps[] = {
    {"__assn__", ":op/assn"},
    {"__assn_add__", ":op/assn_add"},
    {"__assn_sub__", ":op/assn_sub"},
    {"__assn_mul__", ":op/assn_mul"},
    {"__assn_div__", ":op/assn_div"},
    {"__assn_mod__", ":op/assn_mod"},
    {"__assn_mat__", ":op/assn_mat"},
    {"__assn_exp__", ":op/assn_exp"},
    {"__assn_and__", ":op/assn_and"},
    {"__assn_or__", ":op/assn_or"}};

static const std::pair<std::string, std::string> binaryOps[] = {
    {"__or__", ":op/or"},
    {"__and__", ":op/and"},
    {"__eq__", ":op/eq"},
    {"__neq__", ":op/neq"},
    {"__strict_eq__", ":op/strict_eq"},
    {"__strict_neq__", ":op/strict_neq"},
    {"__lt__", ":op/lt"},
    {"__le__", ":op/le"},
    {"__gt__", ":op/gt"},
    {"__ge__", ":op/ge"},
    {"__add__", ":op/add"},
    {"__sub__", ":op/sub"},
    {"__mul__", ":op/mul"},
    {"__div__", ":op/div"},
    {"__mod__", ":op/mod"},
    {"__mat__", ":op/mat"},
    {"__exp__", ":op/exp"},
    {"__idx__", ":op/idx"}};

static const std::pair<std::string, std::string> others[] = {
    {"map", ":not-impl"},
    {"filter", ":not-impl"},
    {"reduce", ":not-impl"},
    {"len", ":not-impl"},
    {"range", ":not-impl"},
    {"enumerate", ":not-impl"},
    {"zip", ":not-impl"},
    {"unzip", ":not-impl"},
    {"exit", ":not-impl"}};

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
        ":io/print");
    exportBuiltinOperator(
        "println",
        param_init_list{},
        {{"value", anyTypePtr, nullptr, false}},
        voidTypePtr,
        ":io/println");
    exportBuiltinOperator("input", param_init_list{}, param_init_list{}, voidTypePtr, ":io/input");
}

bool BasicBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    return true;
}