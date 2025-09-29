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
 * Updated: Sep. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "globals.h"
#include "builtin/executors/builtin.h"
#include "core/context/context.h"

#include "utils/log.h"

void GlobalsBuiltinModule::exportBinaryOp(const std::string &name, const std::string &uri) {
    auto op = makeOperator(
        name,
        makeFuncType(
            param_init_list{},
            {{"lhs", Type::Any(), nullptr, false}, {"rhs", Type::Any(), nullptr, false}},
            Type::Any()),
        uri);

    auto ops = std::make_shared<std::vector<std::shared_ptr<OperatorIndex>>>();
    ops->push_back(op);
    exportEntity(name, ops);
}

void GlobalsBuiltinModule::exportAssnOp(const std::string &name, const std::string &uri) {
    auto op = makeOperator(
        name,
        makeFuncType(
            param_init_list{},
            {{"self", Type::Any(), nullptr, true}, {"value", Type::Any(), nullptr, false}},
            Type::Any()),
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
    {"__pow__", ":op/pow"},
    {"__idx__", ":op/idx"},
};

static const std::pair<std::string, std::string> others[] = {
    {"print", ":io/print"},
    {"println", ":io/println"},
    {"input", ":io/input"},
    {"sleep", ":os/sleep"},
    {"format", ":str/format"},
    {"join", ":str/join"},
    {"exit", ":os/exit"},
    // struct
    {"len", ":struct/len"},
    {"zip", ":struct/zip"},
    {"head", ":struct/head"},
    {"tail", ":struct/tail"},
    {"range", ":struct/range"},
    {"slice", ":struct/slice"},
    {"concat", ":struct/concat"},
    {"append", ":struct/append"},
    {"extend", ":struct/extend"},
    {"contains", ":struct/contains"},
    // Marked Operators
    {"map", ":mark/map"},
    {"apply", ":mark/apply"},
    {"filter", ":mark/filter"},
    {"foreach", ":mark/foreach"},
};

GlobalsBuiltinModule::GlobalsBuiltinModule(context_ptr_t ctx) : BuiltinModule("", ctx) {
    for (const auto &[name, func] : assnOps) {
        exportAssnOp(name, func);
    }

    for (const auto &[name, func] : binaryOps) {
        exportBinaryOp(name, func);
    }

    for (const auto &[name, func] : others) {
        exportBuiltinOperator(name, param_init_list{}, param_init_list{}, Type::Any(), func);
    }
}

bool GlobalsBuiltinModule::load() {
    EXEC_WHEN_DEBUG(l.in("GlobalsBuiltinModule").info("Loading basic built-in module."));
    if (loaded_) {
        return true;
    }
    context_->registerExecutorFactory("", [&]() { return BasicBuiltinExecutor::create(context_); });
    loaded_ = true;
    return true;
}
