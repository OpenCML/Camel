// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "state.h"

#include <string>
#include <utility>

namespace debugger {

struct CompilationState {
    context_ptr_t ctx;
    std::shared_ptr<camel::parse::CamelParser> parser;
    std::shared_ptr<UserDefinedModule> mainModule;
};

using CompileStage = camel::core::module::CompileStage;

CompilationState createCompilationStateForPath(const std::string &path);
/// 返回 (JSON 字符串, 错误信息)。graphId 为空表示根图摘要，非空表示展开该图。
std::pair<std::string, std::string> getGirJson(const std::string &path, const std::string &graphId);

} // namespace debugger
