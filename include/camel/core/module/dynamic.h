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
 * Created: Feb. 23, 2026
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "module.h"

#include "camel/core/context/context.h"

namespace camel::core::module {

/** 宿主与 .cmo 的 ABI 版本号；不匹配时拒绝加载以避免解释器/模块版本不一致导致的崩溃。 */
#define CAMEL_MODULE_ABI_VERSION 1

/** .cmo 动态库需实现的工厂：根据上下文创建并返回模块实例（调用方取得所有权）。 */
using CamelModuleFactory = Module *(*)(camel::core::context::Context *);

#ifdef __cplusplus
extern "C" {
#endif
/** .cmo 必须导出：返回 CAMEL_MODULE_ABI_VERSION，用于加载前版本校验。 */
int camel_module_abi_version(void);
/** 第三方 .cmo 必须实现的符号：返回由 new 分配的 Module*，由运行时用 shared_ptr 接管。 */
Module *camel_module_create(camel::core::context::Context *ctx);
#ifdef __cplusplus
}
#endif

/** 从 .cmo 路径加载动态库并调用 camel_module_create，返回模块；DLL 句柄由 ctx 持有。失败返回
 * nullptr；若 outError 非空则写入失败原因（如 DLL 加载失败、ABI 版本不匹配等）。 */
module_ptr_t loadCmoModule(
    const std::string &moduleName, const std::string &path,
    const camel::core::context::context_ptr_t &ctx, std::string *outError = nullptr);

} // namespace camel::core::module
