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
 * Created: Aug. 18, 2024
 * Updated: Mar. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/error/diagnostics.h"
#include "camel/core/error/runtime.h"
#include "camel/core/slot.h"
#include "camel/core/source/manager.h"

#include <fstream>
#include <functional>
#include <memory>
#include <ostream>
#include <unordered_map>
#include <vector>

namespace camel::compile::gir {
class Graph;
using graph_ptr_t = std::shared_ptr<Graph>;
} // namespace camel::compile::gir

namespace GIR = camel::compile::gir;

namespace camel::core::module {
class Module;
}

// Forward declarations for execute (avoid circular include with executor.h)
class Executor;
class ExecutorManager;

namespace camel::core::context {

class Frame;
class Context;

using context_ptr_t = std::shared_ptr<Context>;
using module_ptr_t  = std::shared_ptr<module::Module>;

struct EntryConfig {
    std::string entryDir;                 // root path of the context, used for loading modules
    std::string entryFile;                // entry module path
    std::vector<std::string> searchPaths; // search paths for modules

    std::string toString() const;
};

class Context : public std::enable_shared_from_this<Context> {
    EntryConfig entryConfig_;
    error::DiagsConfig diagConfig_;

    module_ptr_t mainModule_;
    std::unordered_map<std::string, module_ptr_t> modules_;
    std::unordered_map<std::string, module_ptr_t> builtinModules_;

    std::unique_ptr<ExecutorManager> exeMgr_;
    error::diagnostic_sink_ptr_t runtimeDiagSink_;
    error::runtime_error_reporter_ptr_t runtimeErrorReporter_;
    camel::source::source_context_ptr_t sourceContext_;
    std::unordered_map<std::string, void *> loadedDllHandles_; // .cmo 路径 -> 句柄，保证 DLL 不卸载
    std::optional<int> processExitCode_;
    /** 最近一次“找到 .cmo 但加载失败”的原因，供 ModuleNotFound 报错详情使用。 */
    mutable std::string lastCmoLoadError_;

    std::optional<module_ptr_t> getBuiltinModule(const std::string &name);
    std::vector<std::string>
    getModuleNameCandidates(const std::string &currentModule, const std::string &rawImportName);
    std::string
    resolveRelativeModuleName(const std::string &currentModule, const std::string &importName);
    /** 查找模块对应文件：任意后缀；.cmo 视为动态库，其余视为源码。first 为空表示未找到。 */
    std::pair<std::string, bool> getModulePathAndKind(const std::string &moduleName);
    std::string getModulePath(const std::string &moduleName);
    bool moduleFileExists(const std::string &moduleName);
    module_ptr_t tryLoadModule(const std::string &moduleName);
    /** 收集“未找到模块”时各搜索路径的尝试结果，用于报错详情。 */
    std::vector<std::string> getModuleSearchDiagnostics(const std::string &moduleName) const;
    /** 返回实际参与查找的搜索基路径（绝对路径）列表，用于 .cmo 加载失败时仅列出路径。 */
    std::vector<std::string> getSearchPathBases() const;

    Context(const EntryConfig &entryConf, const error::DiagsConfig &diagConf);

  public:
    ~Context();

    static context_ptr_t create(
        const EntryConfig &entryConf       = EntryConfig(),
        const error::DiagsConfig &diagConf = error::DiagsConfig());

    const std::string &entryDir() const { return entryConfig_.entryDir; }
    error::DiagsConfig diagConfig() const { return diagConfig_; }
    module_ptr_t mainModule() const { return mainModule_; }
    error::diagnostics_ptr_t rtmDiags() const { return runtimeDiagSink_; }
    error::diagnostic_sink_ptr_t runtimeDiagSink() const { return runtimeDiagSink_; }
    error::runtime_error_reporter_ptr_t runtimeErrorReporter() const {
        return runtimeErrorReporter_;
    }
    camel::source::source_context_ptr_t sourceContext() const { return sourceContext_; }
    GIR::graph_ptr_t rootGraph() const;
    GIR::graph_ptr_t mainGraph() const;
    const ExecutorManager &execMgr() const { return *exeMgr_; }

    void setMainModule(module_ptr_t module) { mainModule_ = module; }

    /// Returns main module plus all UserDefinedModules from modules_ (excluding builtin)
    std::vector<module_ptr_t> allUserModules() const;

    /// Dump diagnostics from main and all imported modules that have errors; json=true for JSON
    /// format
    void dumpAllModuleDiagnostics(std::ostream &os, bool json) const;

    void registerExecutorFactory(std::string name, std::function<std::shared_ptr<Executor>()> fact);
    void eval(std::string uri, GIR::Node *self, Frame &frame);

    void clearProcessExitCode() { processExitCode_.reset(); }
    void captureProcessExitCode(GIR::Graph *graph, slot_t result);
    std::optional<int> processExitCode() const { return processExitCode_; }
    int processExitCodeOr(int fallback = 0) const { return processExitCode_.value_or(fallback); }

    /** 由 loadCmoModule 调用，保存已加载 .cmo 的句柄以防被卸载。 */
    void addLoadedDll(const std::string &path, void *handle) { loadedDllHandles_[path] = handle; }

    module_ptr_t
    importModule(const std::string &rawModuleName, const std::string &currentModuleName = "");
};

} // namespace camel::core::context
