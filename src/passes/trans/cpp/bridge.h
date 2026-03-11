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
 * Created: Mar. 11, 2026
 * Updated: Mar. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/data/base.h"
#include "camel/core/type/base.h"
#include "lower.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

struct CppBridgeRuntimeOperator {
    std::string uri;
    std::string name;
};

struct CppBridgeTypeEntry {
    camel::core::type::Type *type = nullptr;
    std::string name;
};

struct CppBridgePlan {
    bool usesRtdataNamespace = false;
    bool usesMmNamespace     = false;
    bool usesStringHeader    = false;
    bool usesChronoSupport   = false;
    bool usesRuntimeBridge   = false;
    std::vector<std::string> importedModules;
    std::vector<CppBridgeRuntimeOperator> runtimeOperators;
    std::vector<CppBridgeTypeEntry> cachedTypes;
};

bool isPureScalarTypeForCpp(camel::core::type::Type *type);
bool isBridgeableTypeForCpp(camel::core::type::Type *type);
std::string cppTypeFor(camel::core::type::Type *type);
std::optional<std::string> cppLiteralFor(const camel::core::data::data_ptr_t &data);

CppBridgePlan collectCppBridgePlan(
    const camel::core::context::context_ptr_t &ctx, const std::vector<GraphLoweringPlan> &plans);
std::string emitCppBridgePreamble(const CppBridgePlan &plan, bool includeIostream);
std::string emitCppBridgeSupport(const CppBridgePlan &plan);
std::string cppBridgeAccessorNameForUri(std::string_view uri);
std::string cppBridgeTypeAccessorName(const CppBridgePlan &plan, camel::core::type::Type *type);
bool isAtomicTypeForCpp(camel::core::type::Type *type);
std::string cppBridgeTypeExprFor(const CppBridgePlan &plan, camel::core::type::Type *type);
std::string emitCppRuntimeFallbackExpr(
    const CppBridgePlan &plan, GIR::OperNode *node,
    const std::function<std::string(GIR::Node *)> &emitExpr);
/// 生成 main 中的 initialize 调用；usesRuntimeBridge 为 false 时返回空。
std::string emitCppBridgeInitializeCall(const CppBridgePlan &plan);
