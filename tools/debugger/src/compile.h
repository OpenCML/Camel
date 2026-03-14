/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Feb. 22, 2026
 * Updated: Mar. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
