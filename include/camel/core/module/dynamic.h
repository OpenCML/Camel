/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 *
 * 动态模块 (.cmo) 加载：第三方可通过实现此 API 提供自定义 Module 子类。
 * 动态库需导出符号：camel_module_create
 */

#pragma once

#include "module.h"

class Context;

/** .cmo 动态库需实现的工厂：根据上下文创建并返回模块实例（调用方取得所有权）。 */
using CamelModuleFactory = Module *(*)(Context *);

#ifdef __cplusplus
extern "C" {
#endif
/** 第三方 .cmo 必须实现的符号：返回由 new 分配的 Module*，由运行时用 shared_ptr 接管。 */
Module *camel_module_create(Context *ctx);
#ifdef __cplusplus
}
#endif

/** 从 .cmo 路径加载动态库并调用 camel_module_create，返回模块；DLL 句柄由 ctx 持有。失败返回
 * nullptr。 */
module_ptr_t
loadCmoModule(const std::string &moduleName, const std::string &path, const context_ptr_t &ctx);
